#include "appcontroller.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace {

// Development servers announce their address; offer it as a browser preview.
QString detectPreviewUrl(const QByteArray &output)
{
    static const QRegularExpression pattern(QStringLiteral(
        "https?://(?:localhost|127\\.0\\.0\\.1|0\\.0\\.0\\.0|\\[::1\\]):\\d{2,5}[^\\s\"'<>\\x1b]*"));
    const auto match = pattern.match(QString::fromUtf8(output));
    if (!match.hasMatch()) return {};
    QString url = match.captured(0);
    url.replace(QStringLiteral("0.0.0.0"), QStringLiteral("localhost"));
    while (url.endsWith(QLatin1Char('.')) || url.endsWith(QLatin1Char(',')) || url.endsWith(QLatin1Char(')')))
        url.chop(1);
    return url;
}

#ifdef Q_OS_WIN
// cmd.exe exits before the servers it launches; a Job Object lets us end the tree.
void *attachJob(qint64 pid)
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    const bool assigned = process && AssignProcessToJobObject(job, process);
    if (process) CloseHandle(process);
    if (!assigned) { CloseHandle(job); return nullptr; }
    return job;
}

void closeJob(BackgroundTask *task)
{
    if (!task->job) return;
    TerminateJobObject(static_cast<HANDLE>(task->job), 1);
    CloseHandle(static_cast<HANDLE>(task->job));
    task->job = nullptr;
}
#endif

void killTree(BackgroundTask *task)
{
#ifdef Q_OS_WIN
    closeJob(task);
#else
    if (task->pid > 0) ::kill(-task->pid, SIGKILL);
#endif
}

} // namespace

QVariantList AppController::backgroundTasks() const
{
    QVariantList items;
    for (const auto *task : m_backgroundTasks) {
        items.prepend(QVariantMap{{"id", task->id}, {"command", task->command},
            {"workdir", task->workdir}, {"status", task->status},
            {"running", task->process != nullptr}, {"exitCode", task->exitCode},
            {"previewUrl", task->previewUrl}, {"revision", task->revision},
            {"projectId", task->projectId}, {"conversationId", task->conversationId}});
    }
    return items;
}

QString AppController::backgroundTaskOutput(int id) const
{
    const auto *task = m_backgroundTasks.value(id);
    return task ? QString::fromUtf8(task->output.data()) : QString();
}

int AppController::startBackgroundTask(const QString &command)
{
    return launchBackgroundTask(command, m_activeProject.workdir,
                                m_activeProjectId, m_currentConversationId);
}

int AppController::restartBackgroundTask(int id)
{
    const auto *task = m_backgroundTasks.value(id);
    if (!task) return 0;
    const QString command = task->command;
    const QString workdir = task->workdir;
    const qint64 projectId = task->projectId;
    const qint64 conversationId = task->conversationId;
    if (task->process) stopBackgroundTask(id);
    return launchBackgroundTask(command, workdir, projectId, conversationId);
}

int AppController::launchBackgroundTask(const QString &command, const QString &workdir,
                                        qint64 projectId, qint64 conversationId)
{
    if (command.trimmed().isEmpty()) return 0;
    int running = 0;
    for (const auto *task : m_backgroundTasks) if (task->process) ++running;
    if (running >= 4 || m_backgroundTasks.size() >= 30) {
        emit toast(QStringLiteral("Limite: 4 tarefas simultâneas e 30 no histórico. Limpe as concluídas."));
        return 0;
    }
    const QString cwd = workdir.isEmpty() ? QDir::homePath() : workdir;
    if (!QFileInfo(cwd).isDir()) { emit toast(QStringLiteral("Pasta de trabalho inválida.")); return 0; }
    auto *task = new BackgroundTask;
    task->id = ++m_backgroundTaskId;
    task->command = command;
    task->workdir = cwd;
    task->projectId = projectId;
    task->conversationId = conversationId;
    auto *process = new QProcess(this);
    task->process = process;
    process->setWorkingDirectory(cwd);
    process->setProcessChannelMode(QProcess::MergedChannels);
#ifndef Q_OS_WIN
    process->setChildProcessModifier([] { ::setsid(); });
#endif
    m_backgroundTasks.insert(task->id, task);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, task, process] {
        const QByteArray chunk = process->readAllStandardOutput();
        task->output.append(chunk);
        ++task->revision;
        if (task->previewUrl.isEmpty()) {
            task->previewUrl = detectPreviewUrl(chunk);
            if (!task->previewUrl.isEmpty())
                emit toast(QStringLiteral("Prévia disponível: %1").arg(task->previewUrl));
        }
        if (!m_backgroundUpdatePending) {
            m_backgroundUpdatePending = true;
            QTimer::singleShot(100, this, [this] {
                m_backgroundUpdatePending = false;
                emit backgroundTasksChanged();
            });
        }
    });
    connect(process, &QProcess::started, this, [this, task, process] {
        task->pid = process->processId();
#ifdef Q_OS_WIN
        task->job = attachJob(task->pid);
#endif
        task->status = QStringLiteral("Em execução");
        emit backgroundTasksChanged();
    });
    auto done = [this, task, process](int code, bool failed) {
        if (task->process != process) return;
        task->output.append(process->readAllStandardOutput());
        ++task->revision;
        // A command that exits must not leave an orphan server/child behind.
        killTree(task);
        task->exitCode = code;
        task->status = task->stopped ? QStringLiteral("Cancelada")
            : failed ? QStringLiteral("Falhou") : QStringLiteral("Concluída");
        task->process = nullptr;
        QObject::disconnect(process, nullptr, this, nullptr);
        process->deleteLater();
        emit backgroundTasksChanged();
    };
    connect(process, &QProcess::errorOccurred, this, [task, process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        task->output.append(process->errorString().toUtf8());
        done(-1, true);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [done](int code, QProcess::ExitStatus status) { done(code, code != 0 || status == QProcess::CrashExit); });
#ifdef Q_OS_WIN
    process->setProgram(QStringLiteral("cmd.exe"));
    process->setNativeArguments(QStringLiteral("/D /S /C \"") + command + QLatin1Char('"'));
    process->start();
#else
    process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
#endif
    emit backgroundTasksChanged();
    return task->id;
}

void AppController::stopBackgroundTask(int id)
{
    auto *task = m_backgroundTasks.value(id);
    if (!task || !task->process) return;
    task->stopped = true;
    task->status = QStringLiteral("Cancelando");
    emit backgroundTasksChanged();
    killTree(task);
    task->process->kill();
}

void AppController::clearFinishedTasks()
{
    for (int id : m_backgroundTasks.keys()) {
        auto *task = m_backgroundTasks.value(id);
        if (!task->process) { m_backgroundTasks.remove(id); delete task; }
    }
    emit backgroundTasksChanged();
}

void AppController::shutdownBackgroundTasks()
{
    for (auto *task : m_backgroundTasks) {
        if (task->process) {
            auto *process = task->process;
            QObject::disconnect(process, nullptr, this, nullptr);
            killTree(task);
            process->kill();
            process->waitForFinished(1000);
            delete process;
        }
#ifdef Q_OS_WIN
        closeJob(task);
#endif
        delete task;
    }
    m_backgroundTasks.clear();
    emit backgroundTasksChanged();
}
