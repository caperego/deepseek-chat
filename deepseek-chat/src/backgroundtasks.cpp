#include "appcontroller.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>
#ifndef Q_OS_WIN
#include <signal.h>
#include <unistd.h>
#endif

QVariantList AppController::backgroundTasks() const
{
    QVariantList items;
    for (const auto *task : m_backgroundTasks) {
        items.prepend(QVariantMap{{"id", task->id}, {"command", task->command},
            {"workdir", task->workdir}, {"status", task->status},
            {"running", task->process != nullptr}, {"exitCode", task->exitCode},
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
        task->output.append(process->readAllStandardOutput());
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
        task->status = QStringLiteral("Em execução");
        emit backgroundTasksChanged();
    });
    auto done = [this, task, process](int code, bool failed) {
        if (task->process != process) return;
        task->output.append(process->readAllStandardOutput());
        // A command that exits must not leave an orphan server/child behind.
#ifndef Q_OS_WIN
        if (task->pid > 0) ::kill(-task->pid, SIGKILL);
#endif
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
#ifndef Q_OS_WIN
    if (task->pid > 0) ::kill(-task->pid, SIGKILL);
#endif
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
#ifndef Q_OS_WIN
            if (task->pid > 0) ::kill(-task->pid, SIGKILL);
#endif
            process->kill();
            process->waitForFinished(1000);
            delete process;
        }
        delete task;
    }
    m_backgroundTasks.clear();
    emit backgroundTasksChanged();
}
