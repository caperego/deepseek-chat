#include "appcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSocketNotifier>

#include <atomic>
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <thread>
#else
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

// Interactive terminal: ConPTY on Windows, a pseudo terminal elsewhere, with a
// plain QProcess shell as the fallback when neither can be created.
namespace {

QString terminalShellProgram()
{
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe");
#else
    // A linear transcript must not load shell themes that rely on cursor positioning.
    return QFileInfo::exists(QStringLiteral("/bin/bash"))
        ? QStringLiteral("/bin/bash") : QStringLiteral("/bin/sh");
#endif
}

#ifdef Q_OS_WIN
typedef void *DsHPCON;
typedef HRESULT(WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, DsHPCON *);
typedef VOID(WINAPI *ClosePseudoConsoleFn)(DsHPCON);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 22
#endif

class ConPty {
public:
    ConPty(QObject *receiver, std::function<void(const QByteArray &)> onData,
           std::function<void(int)> onExit)
        : m_receiver(receiver),
          m_onData(std::move(onData)),
          m_onExit(std::move(onExit))
    {
    }

    bool start()
    {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        const auto create = reinterpret_cast<CreatePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "CreatePseudoConsole")));
        const auto closePty = reinterpret_cast<ClosePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "ClosePseudoConsole")));
        if (!create || !closePty)
            return false;
        m_closePty = closePty;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE inRead = nullptr;
        HANDLE inWrite = nullptr;
        HANDLE outRead = nullptr;
        HANDLE outWrite = nullptr;
        if (!CreatePipe(&inRead, &inWrite, &sa, 0))
            return false;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
            CloseHandle(inRead);
            CloseHandle(inWrite);
            return false;
        }

        COORD size{120, 30};
        DsHPCON pty = nullptr;
        if (FAILED(create(size, inRead, outWrite, 0, &pty))) {
            CloseHandle(inRead);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            CloseHandle(outWrite);
            return false;
        }
        CloseHandle(inRead);
        CloseHandle(outWrite);

        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        // Null std handles: otherwise a parent with redirected stdio leaks them
        // to the shell, which then bypasses the pseudo console.
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, attrSize));
        if (!si.lpAttributeList
            || !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)
            || !UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                          PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                          pty, sizeof(pty), nullptr, nullptr)) {
            closePty(pty);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            if (si.lpAttributeList)
                HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
            return false;
        }

        wchar_t command[] = L"cmd.exe";
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
                                            EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                            nullptr, &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        if (!created) {
            closePty(pty);
            CloseHandle(inWrite);
            CloseHandle(outRead);
            return false;
        }
        CloseHandle(pi.hThread);

        auto session = std::make_shared<Session>();
        session->pty = pty;
        session->inWrite = inWrite;
        session->outRead = outRead;
        session->process = pi.hProcess;
        m_session = session;

        std::thread([this, session] { readLoop(session); }).detach();
        return true;
    }

    void write(const QByteArray &data)
    {
        auto session = m_session;
        if (!session || !session->running.load())
            return;
        DWORD written = 0;
        WriteFile(session->inWrite, data.constData(),
                  static_cast<DWORD>(data.size()), &written, nullptr);
    }

    void close()
    {
        auto session = m_session;
        m_session.reset();
        if (!session)
            return;
        session->running.store(false);
        if (session->pty) {
            m_closePty(session->pty);
            session->pty = nullptr;
        }
        if (session->process) {
            TerminateProcess(session->process, 0);
            CloseHandle(session->process);
            session->process = nullptr;
        }
        if (session->inWrite) {
            CloseHandle(session->inWrite);
            session->inWrite = nullptr;
        }
    }

private:
    struct Session {
        DsHPCON pty = nullptr;
        HANDLE inWrite = nullptr;
        HANDLE outRead = nullptr;
        HANDLE process = nullptr;
        std::atomic<bool> running{true};
    };

    void readLoop(std::shared_ptr<Session> session)
    {
        char buffer[4096];
        DWORD read = 0;
        while (session->running.load()
               && ReadFile(session->outRead, buffer, sizeof(buffer), &read, nullptr)
               && read > 0) {
            const QByteArray data(buffer, static_cast<int>(read));
            QMetaObject::invokeMethod(m_receiver,
                [this, data] {
                    if (m_onData)
                        m_onData(data);
                }, Qt::QueuedConnection);
        }
        if (session->outRead) {
            CloseHandle(session->outRead);
            session->outRead = nullptr;
        }
        int code = 0;
        if (session->process) {
            DWORD exitCode = 0;
            GetExitCodeProcess(session->process, &exitCode);
            code = static_cast<int>(exitCode);
        }
        QMetaObject::invokeMethod(m_receiver,
            [this, code] {
                if (m_onExit)
                    m_onExit(code);
            }, Qt::QueuedConnection);
    }

    QObject *m_receiver = nullptr;
    std::function<void(const QByteArray &)> m_onData;
    std::function<void(int)> m_onExit;
    std::shared_ptr<Session> m_session;
    ClosePseudoConsoleFn m_closePty = nullptr;
};
#endif

} // namespace

bool AppController::terminalRunning() const
{
    return m_terminalRunning;
}

void AppController::runTerminalCommand(const QString &command)
{
    const QString trimmed = m_terminalRunning ? command : command.trimmed();
    if (trimmed.isEmpty() && !m_terminalRunning)
        return;

#ifdef Q_OS_WIN
    auto *pty = static_cast<ConPty *>(m_conpty);
    if (pty && !m_terminalRunning) {
        m_terminalText.reset();
        if (!pty->start()) {
            delete pty;
            m_conpty = nullptr;
        } else {
            m_terminalRunning = true;
            emit terminalChanged();
        }
    }
    if (!pty && !m_conpty) {
        auto *created = new ConPty(this,
            [this](const QByteArray &data) { emit terminalOutput(m_terminalText.consume(data)); },
            [this](int code) {
                m_terminalRunning = false;
                emit terminalOutput(
                    QStringLiteral("\n[processo terminou: %1]\n").arg(code));
                emit terminalChanged();
            });
        m_terminalText.reset();
        if (created->start()) {
            m_conpty = created;
            m_terminalRunning = true;
            emit terminalChanged();
            emit terminalOutput(QStringLiteral("DeepSeek Chat — terminal (ConPTY, cmd.exe)\n"));
        } else {
            delete created;
            emit terminalOutput(QStringLiteral(
                "[ConPTY indisponível — usando modo simples]\n"));
        }
    }
    if (m_conpty) {
        emit terminalOutput(QStringLiteral("> ") + trimmed + QLatin1Char('\n'));
        static_cast<ConPty *>(m_conpty)->write(trimmed.toUtf8()
                                               + QByteArrayLiteral("\r\n"));
        return;
    }
#endif

#ifndef Q_OS_WIN
    if (!m_terminal && m_ptyMaster < 0) {
        int master = -1;
        int slave = -1;
        struct winsize size = {30, 120, 0, 0};
        if (openpty(&master, &slave, nullptr, nullptr, &size) == 0) {
            m_terminalText.reset();
            auto *process = new QProcess(this);
            process->setProcessChannelMode(QProcess::ForwardedChannels);
            auto environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
            environment.insert(QStringLiteral("PS1"), QStringLiteral("\\w $ "));
            environment.insert(QStringLiteral("PROMPT"), QStringLiteral("%~ %# "));
            environment.insert(QStringLiteral("RPROMPT"), QString());
            environment.remove(QStringLiteral("PROMPT_COMMAND"));
            process->setProcessEnvironment(environment);
            const int slaveFd = slave;
            process->setChildProcessModifier([slaveFd]() {
                setsid();
                ioctl(slaveFd, TIOCSCTTY, 0);
                dup2(slaveFd, 0);
                dup2(slaveFd, 1);
                dup2(slaveFd, 2);
                if (slaveFd > 2)
                    close(slaveFd);
            });
            process->setWorkingDirectory(m_activeProject.workdir.isEmpty()
                ? QDir::homePath() : m_activeProject.workdir);
            const QString shell = terminalShellProgram();
            const QString name = QFileInfo(shell).fileName();
            QStringList arguments;
            if (name == QLatin1String("bash")) arguments = {"--noprofile", "--norc", "-i"};
            else if (name == QLatin1String("zsh")) arguments = {"-f", "-i"};
            else arguments = {"-i"};
            process->start(shell, arguments);
            if (process->waitForStarted(5000)) {
                ::close(slave);
                m_ptyMaster = master;
                m_terminal = process;
                auto *notifier = new QSocketNotifier(master, QSocketNotifier::Read, this);
                connect(notifier, &QSocketNotifier::activated, this, [this] {
                    char buffer[4096];
                    const ssize_t n = ::read(m_ptyMaster, buffer, sizeof(buffer));
                    if (n > 0)
                        emit terminalOutput(m_terminalText.consume(QByteArray(buffer, static_cast<int>(n))));
                });
                m_ptyNotifier = notifier;
                connect(process, QOverload<int, QProcess::ExitStatus>::of(
                                     &QProcess::finished),
                        this, [this](int code, QProcess::ExitStatus) {
                            terminalOutput(QStringLiteral("\n[processo terminou: %1]\n")
                                               .arg(code));
                            if (m_ptyNotifier) {
                                m_ptyNotifier->setEnabled(false);
                                m_ptyNotifier->deleteLater();
                                m_ptyNotifier = nullptr;
                            }
                            if (m_ptyMaster >= 0) {
                                ::close(m_ptyMaster);
                                m_ptyMaster = -1;
                            }
                            if (m_terminal) {
                                m_terminal->deleteLater();
                                m_terminal = nullptr;
                            }
                            m_terminalRunning = false;
                            emit terminalChanged();
                        });
                m_terminalRunning = true;
                emit terminalChanged();
            } else {
                ::close(master);
                ::close(slave);
                process->deleteLater();
            }
        }
    }
    if (m_ptyMaster >= 0) {
        const QByteArray payload = trimmed.toUtf8() + QByteArrayLiteral("\n");
        const ssize_t written = ::write(m_ptyMaster, payload.constData(),
                                        static_cast<size_t>(payload.size()));
        Q_UNUSED(written);
        return;
    }
#endif

    if (!m_terminal) {
        m_terminal = new QProcess(this);
        m_terminal->setProcessChannelMode(QProcess::MergedChannels);
        m_terminal->setWorkingDirectory(m_activeProject.workdir.isEmpty()
                                            ? QDir::homePath()
                                            : m_activeProject.workdir);
        connect(m_terminal, &QProcess::readyRead, this, [this] {
            if (m_terminal)
                emit terminalOutput(QString::fromUtf8(m_terminal->readAll()));
        });
        connect(m_terminal, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    emit terminalOutput(
                        QStringLiteral("\n[processo terminou: %1]\n").arg(code));
                    m_terminal->deleteLater();
                    m_terminal = nullptr;
                    m_terminalRunning = false;
                    emit terminalChanged();
                });
        m_terminal->start(terminalShellProgram());
        if (!m_terminal->waitForStarted(5000)) {
            emit terminalOutput(QStringLiteral("[falha ao iniciar o shell]\n"));
            m_terminal->deleteLater();
            m_terminal = nullptr;
            m_terminalRunning = false;
            emit terminalChanged();
            return;
        }
        m_terminalRunning = true;
        emit terminalChanged();
    }

    emit terminalOutput(QStringLiteral("> ") + trimmed + QLatin1Char('\n'));
    m_terminal->write(trimmed.toUtf8() + QByteArrayLiteral("\r\n"));
}

void AppController::sendTerminalInput(const QString &text)
{
#ifdef Q_OS_WIN
    if (m_conpty) {
        static_cast<ConPty *>(m_conpty)->write(text.toUtf8() + QByteArrayLiteral("\r\n"));
        return;
    }
#else
    if (m_ptyMaster >= 0) {
        const QByteArray payload = text.toUtf8() + QByteArrayLiteral("\n");
        const ssize_t written = ::write(m_ptyMaster, payload.constData(),
                                        static_cast<size_t>(payload.size()));
        Q_UNUSED(written);
        return;
    }
#endif
    if (!m_terminal)
        return;
    m_terminal->write(text.toUtf8() + QByteArrayLiteral("\r\n"));
}

void AppController::killTerminal()
{
#ifdef Q_OS_WIN
    if (m_conpty) {
        static_cast<ConPty *>(m_conpty)->close();
        return;
    }
#endif
    if (!m_terminal)
        return;
#ifndef Q_OS_WIN
    if (m_ptyMaster >= 0) {
        const pid_t foreground = ::tcgetpgrp(m_ptyMaster);
        if (foreground > 0 && foreground != ::getpgrp()) ::kill(-foreground, SIGKILL);
    }
#endif
    m_terminal->kill();
}

void AppController::interruptTerminal()
{
    // Ctrl+C reaches the foreground program as a raw byte, without a line ending.
#ifdef Q_OS_WIN
    if (m_conpty) {
        static_cast<ConPty *>(m_conpty)->write(QByteArrayLiteral("\x03"));
        return;
    }
#else
    if (m_ptyMaster >= 0) {
        const ssize_t written = ::write(m_ptyMaster, "\x03", 1);
        Q_UNUSED(written);
        return;
    }
#endif
    killTerminal();
}
