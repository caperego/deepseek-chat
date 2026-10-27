#include "appcontroller.h"
#include "config.h"
#include "pathpolicy.h"
#include "boundedoutput.h"
#include "automationstore.h"
#include "streamclient.h"
#include "toolprotocol.h"
#include <QApplication>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QProcess>
#include <QStandardPaths>

// No real account, provider, configuration file, or remote database is used.
class MockApi : public QTcpServer {
public:
    QVector<QJsonObject> requests;
    std::function<QByteArray(const QJsonObject &)> response;
    int status = 200;
    bool truncate = false;
    int newConversationId = 0;
    MockApi() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto buffer = QSharedPointer<QByteArray>::create();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const int split = buffer->indexOf("\r\n\r\n");
                    if (split < 0) return;
                    int length = 0;
                    for (const auto &line : buffer->left(split).split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toInt();
                    if (buffer->size() < split + 4 + length) return;
                    const bool stream = buffer->startsWith("POST /chat/stream ");
                    QByteArray body = "{}";
                    if (newConversationId > 0 && buffer->startsWith("POST /conversations "))
                        body = QJsonDocument(QJsonObject{{"id", newConversationId}}).toJson(QJsonDocument::Compact);
                    if (stream) {
                        const auto request = QJsonDocument::fromJson(buffer->mid(split + 4, length)).object();
                        requests.append(request);
                        body = response ? response(request) : QByteArray();
                    }
                    socket->disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    const int httpStatus = stream ? status : 200;
                    const QByteArray headers = "HTTP/1.1 " + QByteArray::number(httpStatus)
                        + " Mock\r\nContent-Type: " + (stream ? QByteArray("text/event-stream") : QByteArray("application/json"))
                        + "\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(body.size() + (stream && truncate ? 100 : 0)) + "\r\n\r\n";
                    socket->write(headers);
                    // Split a chunk across packets to exercise incremental parsing.
                    socket->write(body.left(body.size() / 2));
                    QTimer::singleShot(5, socket, [socket, body] {
                        socket->write(body.mid(body.size() / 2));
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    QString url() const { return "http://127.0.0.1:" + QString::number(serverPort()); }
};

static QByteArray delta(const QJsonObject &change, const QString &reason = {}) {
    QJsonObject choice{{"delta", change}};
    if (!reason.isEmpty()) choice.insert("finish_reason", reason);
    return "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray{choice}}}).toJson(QJsonDocument::Compact) + "\r\n\r\n";
}
static QByteArray answer(const QString &text) {
    return delta({{"content", text}}, "stop") + "data: [DONE]\n\n";
}
static QByteArray tool(const QString &name, const QJsonObject &args, int number = 1) {
    QJsonObject call{{"index", 0}, {"id", "call_" + QString::number(number)},
        {"function", QJsonObject{{"name", name}, {"arguments", QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))}}}};
    return delta({{"reasoning_content", "Contexto da etapa " + QString::number(number)}})
        + delta({{"tool_calls", QJsonArray{call}}}, "tool_calls") + "data: [DONE]\n\n";
}

// The same scenario written for the platform shell: /bin/sh or cmd.exe.
static QString shell(const char *posix, const char *windows) {
#ifdef Q_OS_WIN
    Q_UNUSED(posix);
    return QString::fromUtf8(windows);
#else
    Q_UNUSED(windows);
    return QString::fromUtf8(posix);
#endif
}

// On Windows "python3" may be the Microsoft Store alias, which never runs a
// script; use the first interpreter that actually executes code.
static QString workingPython() {
    for (const char *name : {"python3", "python", "py"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (path.isEmpty()) continue;
        QProcess probe;
        probe.start(path, {"-c", "import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)"});
        if (probe.waitForFinished(5000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0)
            return path;
    }
    return {};
}

class AgentRegression : public QObject {
    Q_OBJECT
private slots:
    void terminalTranscriptHandlesSplitUtf8AndEscapeSequences() {
        TerminalText text;
        QCOMPARE(text.consume("\x1b[31mCor\x1b[0m\r\n"), QString("Cor\n"));
        QCOMPARE(text.consume("\x1b]7;file://secret/path"), QString());
        QCOMPARE(text.consume("\x1b\\Pronto"), QString("Pronto"));
        const auto encoded = QString("Ação").toUtf8();
        QString decoded;
        for (int i = 0; i < encoded.size(); ++i) decoded += text.consume(encoded.mid(i, 1));
        QCOMPARE(decoded, QString("Ação"));
        // Cursor positioning (as ConPTY emits) separates lines; at a line start it adds nothing.
        TerminalText conpty;
        QCOMPARE(conpty.consume("\x1b[H\x1b[2JMicrosoft\x1b[2;1HC:\\>"), QString("Microsoft\nC:\\>"));
    }
    void backgroundTasksPreserveCwdOutputAndExitStatus() {
        QTemporaryDir dir;
        AppController controller; controller.m_activeProject.workdir = dir.path();
        const int id = controller.startBackgroundTask(shell("pwd; printf TASK_OK; exit 7", "cd& echo TASK_OK& exit /b 7"));
        QVERIFY(id > 0);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_backgroundTasks.value(id)->process, 5000);
        QCOMPARE(controller.m_backgroundTasks.value(id)->exitCode, 7);
        QCOMPARE(controller.m_backgroundTasks.value(id)->status, QString("Falhou"));
        QVERIFY(controller.backgroundTaskOutput(id).contains(QDir::toNativeSeparators(dir.path())));
        QVERIFY(controller.backgroundTaskOutput(id).contains("TASK_OK"));
        controller.clearFinishedTasks(); QVERIFY(controller.backgroundTasks().isEmpty());
    }
    void backgroundTasksDetectPreviewUrlAndRestart() {
        AppController controller;
        const int id = controller.startBackgroundTask("echo Local: http://localhost:5173/");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_backgroundTasks.value(id)->process, 5000);
        QCOMPARE(controller.m_backgroundTasks.value(id)->previewUrl, QString("http://localhost:5173/"));
        QCOMPARE(controller.backgroundTasks().first().toMap().value("previewUrl").toString(), QString("http://localhost:5173/"));
        const int again = controller.restartBackgroundTask(id);
        QVERIFY(again > id);
        QCOMPARE(controller.m_backgroundTasks.value(again)->command, controller.m_backgroundTasks.value(id)->command);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_backgroundTasks.value(again)->process, 5000);
    }
    void linksOpenInTheIntegratedBrowserAndPathsAreRelative() {
        QTemporaryDir dir;
        AppController controller; controller.m_activeProject.workdir = dir.path();
        Config::instance().ui().openLinksInternally = true;
        QSignalSpy browser(&controller, &AppController::browserRequested);
        controller.openLink("https://example.invalid/docs");
        QCOMPARE(browser.size(), 1);
        QCOMPARE(browser.first().first().toString(), QString("https://example.invalid/docs"));
        QCOMPARE(controller.displayPath(dir.path() + "/src/main.cpp"),
                 QDir::toNativeSeparators(QStringLiteral("src/main.cpp")));
        QCOMPARE(controller.displayPath(dir.path()), QStringLiteral("."));
    }
    void backgroundTasksRunIndependentlyAndCanBeCancelled() {
        AppController controller;
        const int id = controller.startBackgroundTask(shell("printf STARTED; sleep 30", "echo STARTED& ping -n 31 127.0.0.1 >nul"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.backgroundTaskOutput(id).contains("STARTED"), 3000);
        controller.m_currentConversationId = 123;
        QVERIFY(controller.m_backgroundTasks.value(id)->process);
        controller.clearFinishedTasks(); QCOMPARE(controller.backgroundTasks().size(), 1);
        controller.stopBackgroundTask(id);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_backgroundTasks.value(id)->process, 5000);
        QCOMPARE(controller.m_backgroundTasks.value(id)->status, QString("Cancelada"));
    }
    void backgroundTasksBoundConcurrencyAndOutput() {
        AppController controller;
        const QString wait = shell("sleep 30", "ping -n 31 127.0.0.1 >nul");
        for (int i = 0; i < 4; ++i) QVERIFY(controller.startBackgroundTask(wait) > 0);
        QCOMPARE(controller.startBackgroundTask("echo must-not-start"), 0);
        controller.shutdownBackgroundTasks(); QVERIFY(controller.backgroundTasks().isEmpty());
        const int id = controller.startBackgroundTask(shell("head -c 400000 /dev/zero; printf THE_END",
            "(for /L %i in (1,1,9000) do @echo 01234567890123456789012345678901234567890123456789)& echo THE_END"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_backgroundTasks.value(id)->process, 20000);
        QVERIFY(controller.backgroundTaskOutput(id).size() < 206000);
        QVERIFY(controller.backgroundTaskOutput(id).contains("THE_END"));
        QVERIFY(controller.m_backgroundTasks.value(id)->output.truncated());
    }
    void backgroundToolDoesNotBlockChatAndHonorsApproval() {
        MockApi api;
        const QString command = shell("printf READY; sleep 30", "echo READY& ping -n 31 127.0.0.1 >nul");
        api.response = [&api, command](const QJsonObject &) {
            return api.requests.size() == 1
                ? tool("start_background_task", {{"command", command}})
                : answer("Tarefa iniciada, ainda em execução.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approvals(&controller, &AppController::approvalRequested);
        controller.send("Inicie uma tarefa");
        QTRY_COMPARE_WITH_TIMEOUT(approvals.size(), 1, 3000);
        QVERIFY(controller.backgroundTasks().isEmpty());
        controller.resolveApproval(true, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 2);
        QCOMPARE(controller.backgroundTasks().size(), 1);
        QVERIFY(controller.m_backgroundTasks.first()->process);
        controller.stopBackgroundTask(controller.m_backgroundTasks.firstKey());
    }
    void agentBrowserToolsDriveTheVisiblePanel() {
        MockApi api;
        api.response = [&api](const QJsonObject &) {
            return api.requests.size() == 1
                ? tool("browser_open", {{"url", "https://example.invalid/"}})
                : answer("Página aberta no navegador integrado.");
        };
        Config::instance().api().baseUrl = api.url();
        const QString previousMode = Config::instance().ui().permissionMode;
        Config::instance().ui().permissionMode = "full";
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QVariantMap seen;
        // Stands in for BrowserPane.qml, which runs the scripts in the real browser.
        connect(&controller, &AppController::agentBrowserRequest, &controller, [&](int id, const QVariantMap &request) {
            seen = request;
            QTimer::singleShot(0, &controller, [&controller, id] {
                controller.agentBrowserReply(id, {{"url", "https://example.invalid/"}, {"title", "Exemplo"},
                                                  {"content", "Conteúdo visível da página"}, {"links", QVariantList{}}});
            });
        });
        controller.send("Abra o site e navegue por ele");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 5000);
        Config::instance().ui().permissionMode = previousMode;
        QCOMPARE(seen.value("url").toString(), QString("https://example.invalid/"));
        QVERIFY(seen.value("snapshot").toString().contains("innerText"));
        QCOMPARE(api.requests.size(), 2);
        const QJsonArray messages = api.requests.at(1).value("messages").toArray();
        QVERIFY(messages.last().toObject().value("content").toString().contains("Conteúdo visível da página"));
        QStringList declared;
        for (const QJsonValue &value : api.requests.first().value("tools").toArray())
            declared << value.toObject().value("function").toObject().value("name").toString();
        for (const char *name : {"browser_open", "browser_read", "browser_click", "browser_type", "browser_back"})
            QVERIFY2(declared.contains(name), name);
        QVERIFY(api.requests.first().value("messages").toArray().first().toObject()
                    .value("content").toString().contains("Sistema operacional"));
    }
    void agentBrowserActionsNeedApprovalButReadingDoesNot() {
        AppController controller;
        AgentRun run; run.permissionMode = "ask"; run.workdir = QDir::tempPath();
        auto needsApproval = [&](const char *name, const QJsonObject &arguments) {
            ToolCall c; c.name = name;
            c.arguments = QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
            return controller.toolNeedsApproval(&run, c, nullptr);
        };
        QVERIFY(needsApproval("browser_open", {{"url", "https://example.invalid"}}));
        QVERIFY(needsApproval("browser_click", {{"text", "Entrar"}}));
        QVERIFY(needsApproval("browser_type", {{"selector", "#q"}, {"text", "x"}}));
        QVERIFY(!needsApproval("browser_read", {}));
        QVERIFY(!needsApproval("browser_back", {}));
    }
    void dangerousBackgroundToolStillRequiresApproval() {
        MockApi api; api.response = [](const QJsonObject &) {
            return tool("start_background_task", {{"command", "shutdown --help"}});
        };
        Config::instance().api().baseUrl = api.url();
        Config::instance().ui().permissionMode = "full";
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approvals(&controller, &AppController::approvalRequested);
        controller.send("Teste de confirmação");
        QTRY_COMPARE_WITH_TIMEOUT(approvals.size(), 1, 3000);
        QVERIFY(approvals.first().at(1).toString().contains("PERIGOSO"));
        QVERIFY(controller.backgroundTasks().isEmpty());
        controller.stop();
    }
    void boundedCommandOutputPreservesHeadAndTail() {
        BoundedOutput output(1024, 256);
        output.append("INICIO\n");
        for (int i = 0; i < 1000; ++i) output.append(QByteArray(4096, 'x'));
        output.append("\nFIM");
        QVERIFY(output.truncated());
        const auto text = output.data();
        QVERIFY(text.startsWith("INICIO\n"));
        QVERIFY(text.endsWith("\nFIM"));
        QVERIFY(text.size() < 1200);
        BoundedOutput small(1024, 256);
        small.append("texto completo");
        QCOMPARE(small.data(), QByteArray("texto completo"));
        QVERIFY(!small.truncated());
    }
    void mcpPublicNamesRemainDistinctAndFitProviderLimits() {
        QCOMPARE(AppController::mcpPublicName("alpha", "echo"), QString("mcp__alpha__echo"));
        QVERIFY(AppController::mcpPublicName("a-b", "echo") != AppController::mcpPublicName("a_b", "echo"));
        QVERIFY(AppController::mcpPublicName("a__b", "c") != AppController::mcpPublicName("a", "b__c"));
        QVERIFY(AppController::mcpPublicName(QString(100, 'a'), QString(100, 'b')).size() <= 64);
    }
    void streamingUpdatesAreCoalescedWithoutLosingContent() {
        MessageListModel model;
        model.beginAssistant();
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        for (int i = 0; i < 1000; ++i) model.appendChunk("a");
        model.appendReasoning("raciocínio");
        QCOMPARE(changed.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 1000);
        QCOMPARE(model.data(model.index(0), MessageListModel::ContentRole).toString(), QString(1000, 'a'));
        model.appendChunk("fim");
        model.finishAssistant("resposta final", "raciocínio final");
        QCOMPARE(model.data(model.index(0), MessageListModel::ContentRole).toString(), QString("resposta final"));
        QVERIFY(!model.data(model.index(0), MessageListModel::StreamingRole).toBool());
        model.clearMessages();
        QTest::qWait(50);
        QCOMPARE(model.rowCount(), 0);
    }
    void schedulesPersistAndDoNotDuplicateMissedRuns() {
        QTemporaryDir dir;
        AutomationStore store;
        QVERIFY(store.load(dir.path() + "/tasks.json"));
        const qint64 now = 1000000;
        QVERIFY(store.add("Relatório", "Resuma o projeto", 1, 60, 12, "deepseek-flash", "high", now));
        QVERIFY(store.claimDue(now + 59000).isEmpty());
        const auto due = store.claimDue(now + 24 * 60 * 60000);
        QCOMPARE(due.size(), 1);
        QVERIFY(store.claimDue(now + 24 * 60 * 60000).isEmpty());
        const auto id = due.first().toMap().value("id").toString();
        QVERIFY(store.complete(id, "Concluída", 42));
        AutomationStore reopened;
        QVERIFY(reopened.load(dir.path() + "/tasks.json"));
        QCOMPARE(reopened.tasks().first().toMap().value("conversationId").toLongLong(), 42);
        QVERIFY(reopened.claimDue(now + 24 * 60 * 60000).isEmpty());
        QVERIFY(reopened.toggle(id));
        QVERIFY(reopened.claimDue(now + 48 * 60 * 60000).isEmpty());
        QVERIFY(reopened.remove(id));
        QVERIFY(reopened.tasks().isEmpty());
    }
    void oneShotSchedulesRecoverWithoutReplayingClaimedRuns() {
        QTemporaryDir dir;
        AutomationStore store;
        QVERIFY(store.load(dir.path() + "/tasks.json"));
        QVERIFY(store.add("Uma vez", "Leia o arquivo", 1, 0, 0, "deepseek-flash", "high", 1000));
        QCOMPARE(store.claimDue(61000).size(), 1);
        AutomationStore reopened;
        QVERIFY(reopened.load(dir.path() + "/tasks.json"));
        QVERIFY(reopened.claimDue(1000000).isEmpty());
        QVERIFY(reopened.tasks().first().toMap().value("status").toString().contains("Interrompida"));
        QFile corrupt(dir.path() + "/bad.json");
        QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("invalid"); corrupt.close();
        QVERIFY(!reopened.load(corrupt.fileName()));
        QVERIFY(!reopened.add("Nova", "Tarefa", 1, 0, 0, "m", "high", 1000));
        QVERIFY(corrupt.open(QIODevice::ReadOnly)); QCOMPARE(corrupt.readAll(), QByteArray("invalid"));
    }
    void scheduledTaskRunsWithoutReplacingOpenConversation() {
        MockApi api;
        api.newConversationId = 77;
        api.response = [](const QJsonObject &) { return answer("Resultado agendado"); };
        QTemporaryDir dir;
        Config::instance().api().baseUrl = api.url();
        AppController controller;
        controller.m_user.id = 42;
        controller.m_automationBackend = api.url();
        controller.m_currentConversationId = 19;
        controller.m_currentTitle = "Conversa aberta";
        QVERIFY(controller.m_automations.load(dir.path() + "/tasks.json"));
        QVERIFY(controller.m_automations.add("Relatório", "Instrução agendada", 1, 0, 0,
                                             "deepseek-flash", "high",
                                             QDateTime::currentMSecsSinceEpoch() - 61000));
        controller.pollAutomations();
        QTRY_VERIFY_WITH_TIMEOUT(controller.m_runs.isEmpty(), 3000);
        QCOMPARE(api.requests.size(), 1);
        QCOMPARE(api.requests.first().value("conversation_id").toInt(), 77);
        QCOMPARE(api.requests.first().value("messages").toArray().last().toObject().value("content").toString(), QString("Instrução agendada"));
        QCOMPARE(controller.currentConversationId(), 19);
        QCOMPARE(controller.currentTitle(), QString("Conversa aberta"));
        QCOMPARE(controller.automations().first().toMap().value("status").toString(), QString("Concluída"));
        QCOMPARE(controller.automations().first().toMap().value("conversationId").toLongLong(), 77);
        controller.pollAutomations();
        QCOMPARE(api.requests.size(), 1);
        QVERIFY(controller.m_automations.add("Outro relatório", "Não enviar para outro backend", 1, 0, 0,
                                             "deepseek-flash", "high",
                                             QDateTime::currentMSecsSinceEpoch() - 61000));
        Config::instance().api().baseUrl = api.url() + "/outro";
        controller.pollAutomations();
        QCOMPARE(api.requests.size(), 1);
        QVERIFY(!controller.automations().last().toMap().value("running").toBool());
        Config::instance().api().baseUrl = api.url();
    }
    void mcpServersIsolateResponsesAndHandleFailures_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("two-servers") << QString("echo");
        QTest::newRow("server-exit") << QString("exit");
        QTest::newRow("timeout") << QString("hang");
        QTest::newRow("tool-error") << QString("error");
        QTest::newRow("cancel-pending") << QString("cancel");
    }
    void mcpServersIsolateResponsesAndHandleFailures() {
        QFETCH(QString, mode);
        const auto python = workingPython();
        if (python.isEmpty()) QSKIP("Python 3 is required for the isolated MCP test server");
        MockApi api;
        QTemporaryDir dir;
        QFile script(dir.path() + "/mcp.py");
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"PY(import sys, json, time
for line in sys.stdin:
    request = json.loads(line)
    method = request.get('method')
    if 'id' not in request: continue
    result = {}
    if method == 'initialize': result = {'capabilities': {'tools': {}}}
    elif method == 'tools/list': result = {'tools': [{'name':'echo', 'inputSchema':{'type':'object'}}]}
    elif method == 'tools/call':
        if sys.argv[2] == 'exit': sys.exit(3)
        if sys.argv[2] == 'hang': time.sleep(15)
        time.sleep(0.05)
        result = {'content': [{'type':'text','text':sys.argv[1]}], 'isError':sys.argv[2] == 'error'}
    print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}), flush=True)
)PY");
        script.close();
        api.response = [](const QJsonObject &request) {
            const auto messages = request.value("messages").toArray();
            if (messages.last().toObject().value("role") == "tool"
                || messages.last().toObject().value("role") == "system") return answer("Pronto");
            const QString server = request.value("conversation_id").toInt() == 11 ? "alpha" : "beta";
            return tool("mcp__" + server + "__echo", {});
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller;
        controller.setCurrentModel("deepseek-flash");
        for (const QString &name : {QString("alpha"), QString("beta")}) {
            auto *server = new McpServer;
            server->name = name;
            server->config = {{"command", python}, {"args", QJsonArray{script.fileName(), name, mode == "cancel" ? "hang" : mode}}, {"timeoutMs", 1000}};
            controller.m_mcpServers.append(server);
            controller.startMcpServer(server);
        }
        QTRY_COMPARE_WITH_TIMEOUT(controller.m_mcpTools.size(), 2, 3000);
        connect(&controller, &AppController::approvalRequested, &controller, [&controller] {
            QTimer::singleShot(0, &controller, [&controller] { controller.resolveApproval(true, false); });
        });
        controller.m_currentConversationId = 11;
        controller.send("Alpha");
        controller.m_currentConversationId = 12;
        controller.m_history.clear();
        controller.send("Beta");
        if (mode == "cancel") {
            QTRY_COMPARE_WITH_TIMEOUT(controller.m_mcpCallRuns.size(), 2, 2000);
            QProcess *first = controller.m_mcpServers.first()->process;
            controller.reloadMcp();
            QCOMPARE(controller.m_mcpServers.first()->process, first);
            controller.stop();
            controller.m_currentConversationId = 11;
            controller.stop();
        }
        QTRY_VERIFY_WITH_TIMEOUT(controller.m_runs.isEmpty(), 5000);
        QVERIFY(controller.m_mcpCallRuns.isEmpty());
        int results = 0;
        for (const auto &request : api.requests) {
            const auto messages = request.value("messages").toArray();
            const auto last = messages.last().toObject();
            if (last.value("role") != "tool") continue;
            ++results;
            const auto result = QJsonDocument::fromJson(last.value("content").toString().toUtf8()).object();
            if (mode == "echo") {
                const QString server = request.value("conversation_id").toInt() == 11 ? "alpha" : "beta";
                QCOMPARE(result.value("result").toString().trimmed(), server);
            } else QVERIFY(result.contains("error"));
        }
        QCOMPARE(results, mode == "cancel" ? 0 : 2);
    }
    void workspacePathsRespectSymlinks() {
        QTemporaryDir workspace, outside;
        QVERIFY(workspace.isValid()); QVERIFY(outside.isValid());
        QVERIFY(PathPolicy::insideDir(workspace.path(), workspace.path() + "/new/file.txt"));
        QVERIFY(!PathPolicy::insideDir(workspace.path(), outside.path() + "/file.txt"));
#ifndef Q_OS_WIN
        const QString link = workspace.path() + "/link";
        QVERIFY(QFile::link(outside.path(), link));
        QVERIFY(!PathPolicy::insideDir(workspace.path(), link + "/new/file.txt"));
        const QString broken = workspace.path() + "/broken";
        QVERIFY(QFile::link(outside.path() + "/does-not-exist", broken));
        QVERIFY(!PathPolicy::insideDir(workspace.path(), broken + "/new.txt"));
#endif
    }
    void streamFailures_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("truncate");
        QTest::addColumn<bool>("success");
        QTest::addColumn<QString>("detail");
        QTest::newRow("success") << answer("Pronto") << 200 << false << true << QString();
        QTest::newRow("sse-error-after-text") << (delta({{"content", "Parcial"}}) + "event: error\ndata: {\"error\":\"Falha upstream\"}\n\n") << 200 << false << false << QString("Falha upstream");
        QTest::newRow("http-json-error") << QByteArray("{\"error\":{\"message\":\"Contexto ausente\"}}") << 400 << false << false << QString("Contexto ausente");
        QTest::newRow("proxy-json-error") << QByteArray("event: meta\ndata: {\"conversation_id\":0}\n\n{\"error\":{\"message\":\"Contexto ausente\"}}") << 200 << false << false << QString("Contexto ausente");
        QTest::newRow("eof-without-terminal") << delta({{"content", "Parcial"}}) << 200 << false << false << QString("antes de concluir");
        QTest::newRow("network-drop-after-text") << delta({{"content", "Parcial"}}) << 200 << true << false << QString("interrompida");
        QTest::newRow("length") << (delta({{"content", "Parcial"}}, "length") + "data: [DONE]\n\n") << 200 << false << false << QString("limite de saída");
        QTest::newRow("reasoning-only") << (delta({{"reasoning_content", "Pensando"}}, "stop") + "data: [DONE]\n\n") << 200 << false << false << QString("sem uma resposta final");
        QTest::newRow("missing-final-newline") << QByteArray("data: {\"choices\":[{\"delta\":{\"content\":\"Pronto\"},\"finish_reason\":\"stop\"}]}") << 200 << false << true << QString();
        QTest::newRow("valid-tool") << tool("list_files", {{"path", "."}}) << 200 << false << true << QString();
        QTest::newRow("incomplete-tool") << QByteArray("data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"x\",\"function\":{\"name\":\"write_file\",\"arguments\":\"{\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n") << 200 << false << false << QString("incompleta");
    }
    void streamFailures() {
        QFETCH(QByteArray, body); QFETCH(int, status); QFETCH(bool, truncate);
        QFETCH(bool, success); QFETCH(QString, detail);
        MockApi api; api.status = status; api.truncate = truncate;
        api.response = [body](const QJsonObject &) { return body; };
        StreamClient client; client.setBaseUrl(api.url());
        QSignalSpy finished(&client, &StreamClient::finished), failed(&client, &StreamClient::failed);
        Message message; message.role = "user"; message.content = "Teste";
        client.send(0, 0, "Teste", "deepseek-flash", "high", false, {message});
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() + failed.count() == 1, 3000);
        QCOMPARE(finished.count(), success ? 1 : 0);
        QCOMPARE(failed.count(), success ? 0 : 1);
        QVERIFY(!client.isStreaming());
        if (!success) QVERIFY2(failed.first().first().toString().contains(detail), qPrintable(failed.first().first().toString()));
        if (body.contains("Parcial")) QCOMPARE(client.fullText(), QString("Parcial"));
    }
    void init() {
        Config::instance().ui().notifyOnFinish = false;
        Config::instance().ui().permissionMode = "ask";
    }
    void continuesBeyondTwelveRounds() {
        MockApi api; QTemporaryDir dir;
        api.response = [&api, &dir](const QJsonObject &) {
            return api.requests.size() <= 20 ? tool("list_files", {{"path", dir.path()}}, api.requests.size()) : answer("Concluído com os resultados disponíveis.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        connect(&controller, &AppController::approvalRequested, &controller, [&controller] {
            QTimer::singleShot(0, &controller, [&controller] { controller.resolveApproval(true, false); });
        });
        controller.send("Teste de etapas");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 8000);
        QCOMPARE(api.requests.size(), 21);
        QVERIFY(api.requests.last().contains("tools"));
        QCOMPARE(api.requests.last().value("model").toString(), QString("deepseek-flash"));
        const auto messages = api.requests.last().value("messages").toArray();
        int assistants = 0, results = 0;
        for (const auto &entry : messages) {
            const auto item = entry.toObject();
            if (item.value("role") == "assistant" && item.contains("tool_calls")) {
                ++assistants;
                QVERIFY(item.value("reasoning_content").toString().startsWith("Contexto da etapa"));
            }
            if (item.value("role") == "tool") ++results;
        }
        QCOMPARE(assistants, 20); QCOMPARE(results, 20);
        const auto *model = controller.messageModel();
        QCOMPARE(model->data(model->index(model->rowCount() - 1, 0), MessageListModel::ContentRole).toString(), QString("Concluído com os resultados disponíveis."));
    }
    void successfulToolRunEndsWithoutExtraReview() {
        MockApi api; QTemporaryDir dir;
        api.response = [&api, &dir](const QJsonObject &) {
            return api.requests.size() == 1
                ? tool("list_files", {{"path", dir.path()}}) : answer("Concluído.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        controller.m_activeProject.workdir = dir.path();
        controller.send("Liste a pasta");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 2);
        int results = 0;
        for (const Message &message : controller.m_history)
            if (message.role == "tool") ++results;
        QCOMPARE(results, 1);
    }
    void leakedToolTextIsReissuedAsNativeCall_data() {
        QTest::addColumn<QString>("marker");
        QTest::newRow("fullwidth") << QString("<｜DSML｜tool_calls>");
        QTest::newRow("same-line") << QString("Vou procurar. <｜DSML｜tool_calls>");
        QTest::newRow("ascii") << QString("<|DSML|function_calls>");
        QTest::newRow("spaced-invoke") << QString("<｜DSML｜ invoke name=\"read_file\">");
        QTest::newRow("doubled-bars") << QString("<｜｜DSML｜｜calls>\n<｜｜DSML｜｜invoke name=\"run_command\">");
        QTest::newRow("spaced-bars") << QString("< | | DSML | | calls>");
    }
    void leakedToolTextIsReissuedAsNativeCall() {
        QFETCH(QString, marker);
        MockApi api; QTemporaryDir dir;
        const QString path = dir.filePath("site.html");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("ARQUIVO_REAL_ENCONTRADO"); file.close();
        api.response = [&api, marker](const QJsonObject &) {
            if (api.requests.size() == 1) return answer(marker + "\ncomando inválido");
            if (api.requests.size() == 2) return tool("read_file", {{"path", "site.html"}});
            return answer("Arquivo encontrado e lido.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        controller.m_activeProject.workdir = dir.path();
        controller.send("Encontre o arquivo do site");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 3);
        QVERIFY(!api.requests.at(1).value("persist_user").toBool(true));
        for (const auto &message : api.requests.at(1).value("messages").toArray())
            QVERIFY(!message.toObject().value("content").toString().contains(marker));
        const auto result = api.requests.last().value("messages").toArray().last().toObject()
                                .value("content").toString();
        QVERIFY(result.contains("ARQUIVO_REAL_ENCONTRADO"));
        const auto *model = controller.messageModel();
        for (int row = 0; row < model->rowCount(); ++row)
            QVERIFY(!model->data(model->index(row), MessageListModel::ContentRole).toString().contains(marker));
    }
    void repeatedProtocolFailureStopsWithoutExecutingText() {
        MockApi api;
        api.response = [](const QJsonObject &) { return answer("<｜DSML｜tool_calls>\n<invoke name=\"run_command\">fake</invoke>"); };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        controller.send("Teste recuperação limitada");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 3); QCOMPARE(approval.count(), 0);
        const auto *model = controller.messageModel();
        QVERIFY(model->data(model->index(model->rowCount()-1), MessageListModel::ErrorRole)
                    .toString().contains("fora do protocolo"));
    }
    void plainTextAfterProtocolFailureIsNotSuccess() {
        MockApi api;
        api.response = [&api](const QJsonObject &) {
            return api.requests.size() == 1 ? answer("<｜DSML｜tool_calls>")
                                            : answer("Vou procurar o arquivo.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        controller.send("Encontre o site");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 2);
        const auto *model = controller.messageModel();
        QVERIFY(model->data(model->index(model->rowCount()-1), MessageListModel::ErrorRole)
                    .toString().contains("não reenviou"));
    }
    void dsmlCodeExamplesRemainLiteral() {
        const QString example = "Exemplo:\n```xml\n<｜DSML｜tool_calls>\n```\nFim.";
        QCOMPARE(ToolProtocol::leakedCallStart("Use `<｜DSML｜tool_calls>` como exemplo."), -1);
        QCOMPARE(ToolProtocol::leakedCallStart(example), -1);
        QCOMPARE(ToolProtocol::safeAssistantContent(example), example);
        MockApi api; api.response = [example](const QJsonObject &) { return answer(example); };
        StreamClient client; client.setBaseUrl(api.url());
        QSignalSpy finished(&client, &StreamClient::finished), failed(&client, &StreamClient::failed);
        Message prompt; prompt.role = "user"; prompt.content = "Explique a marcação";
        client.send(0,0,"Teste","deepseek-flash","high",false,{prompt});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        QCOMPARE(failed.size(), 0); QCOMPARE(client.fullText(), example);
    }
    void legacyLeakageIsExcludedFromOutgoingHistory() {
        MockApi api; api.response = [](const QJsonObject &) { return answer("Pronto."); };
        StreamClient client; client.setBaseUrl(api.url());
        Message legacy; legacy.role = "assistant";
        legacy.content = "Vou procurar.\n<｜DSML｜tool_calls>fake";
        Message prompt; prompt.role = "user"; prompt.content = "Continue";
        QSignalSpy finished(&client, &StreamClient::finished);
        client.send(0,0,"Teste","deepseek-flash","high",false,{legacy,prompt});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
        const QString submitted = api.requests.first().value("messages").toArray().first()
                                      .toObject().value("content").toString();
        QVERIFY(!submitted.contains("DSML")); QVERIFY(submitted.contains("não foi executada"));
    }
    void completedRunKeepsToolResultsDespiteTextOnlyBackend() {
        MockApi api; QTemporaryDir dir;
        api.response = [&api, &dir](const QJsonObject &) {
            return api.requests.size() == 1 ? tool("list_files", {{"path", dir.path()}})
                                            : answer("Concluído.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        controller.m_currentConversationId = 99; // GET /messages returns an empty text-only history.
        controller.m_activeProject.workdir = dir.path();
        controller.send("Liste a pasta");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        int results = 0;
        for (const Message &message : controller.m_history)
            if (message.role == "tool") { ++results; QVERIFY(!message.toolSummary.isEmpty()); }
        QCOMPARE(results, 1);
        controller.send("Continue usando os resultados anteriores");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        int submitted = 0;
        for (const auto &message : api.requests.last().value("messages").toArray())
            if (message.toObject().value("role") == "tool") ++submitted;
        QCOMPARE(submitted, 1);
    }
    void runawayLoopStopsAndKeepsToolContext() {
        MockApi api; QTemporaryDir dir;
        api.response = [&api, &dir](const QJsonObject &) {
            return tool("list_files", {{"path", dir.path()}}, api.requests.size());
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        connect(&controller, &AppController::approvalRequested, &controller, [&controller] {
            QTimer::singleShot(0, &controller, [&controller] { controller.resolveApproval(true, false); });
        });
        controller.send("Teste de ciclo");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 20000);
        QCOMPARE(api.requests.size(), 128);
        const auto *model = controller.messageModel();
        QVERIFY(model->data(model->index(model->rowCount()-1), MessageListModel::ErrorRole)
                    .toString().contains("128 rodadas"));
        int results = 0;
        for (const Message &message : controller.m_history)
            if (message.role == "tool") ++results;
        QCOMPARE(results, 128);
        api.response = [](const QJsonObject &) { return answer("Retomado."); };
        controller.send("continuar");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        int submittedResults = 0;
        for (const auto &message : api.requests.last().value("messages").toArray())
            if (message.toObject().value("role") == "tool") ++submittedResults;
        QCOMPARE(submittedResults, 128);
    }
    void outputLimitContinuationIsBounded() {
        MockApi api;
        api.response = [](const QJsonObject &) {
            return delta({{"content", "Trecho recebido"}}, "length") + "data: [DONE]\n\n";
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        controller.send("Gere um texto longo");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 5000);
        QCOMPARE(api.requests.size(), 4);
        QVERIFY(!api.requests.at(1).value("persist_user").toBool(true));
        const auto messages = api.requests.at(1).value("messages").toArray();
        QCOMPARE(messages.at(messages.size()-2).toObject().value("content").toString(), QString("Trecho recebido"));
        const auto *model = controller.messageModel();
        QVERIFY(model->data(model->index(model->rowCount()-1), MessageListModel::ErrorRole)
                    .toString().contains("limite de saída"));
    }
    void outputLimitContinuesToCompletion() {
        MockApi api;
        api.response = [&api](const QJsonObject &) {
            return api.requests.size() == 1
                ? delta({{"content", "Primeira parte"}}, "length") + "data: [DONE]\n\n"
                : answer("Segunda parte e conclusão.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        controller.send("Gere um texto longo");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 2);
        const auto *model = controller.messageModel();
        QCOMPARE(model->data(model->index(model->rowCount()-1), MessageListModel::ContentRole).toString(),
                 QString("Segunda parte e conclusão."));
        QVERIFY(model->data(model->index(model->rowCount()-1), MessageListModel::ErrorRole).toString().isEmpty());
    }
    void truncatedToolIsNeverExecutedOrRetried() {
        MockApi api; QTemporaryDir dir;
        const QString path = dir.path() + "/must-not-exist.txt";
        api.response = [path](const QJsonObject &) {
            auto body = tool("write_file", {{"path", path}, {"content", "never"}});
            body.replace("tool_calls\"}", "length\"}");
            return body;
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        controller.send("Teste ferramenta cortada");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 1);
        QCOMPARE(approval.count(), 0);
        QVERIFY(!QFile::exists(path));
    }
    void networkFailureDoesNotReplayTools() {
        MockApi api; QTemporaryDir dir;
        api.response = [&api, &dir](const QJsonObject &) {
            if (api.requests.size() == 1) return tool("list_files", {{"path", dir.path()}});
            return delta({{"content", "Parcial"}}); // Unexpected EOF, not a clean output limit.
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        connect(&controller, &AppController::approvalRequested, &controller, [&controller] {
            QTimer::singleShot(0, &controller, [&controller] { controller.resolveApproval(true, false); });
        });
        controller.send("Teste interrupção");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 3000);
        QCOMPARE(api.requests.size(), 2);
        QCOMPARE(controller.m_history.last().content, QString("Parcial"));
        QVERIFY(controller.m_history.at(controller.m_history.size()-2).role == "tool");
    }
    void stopWhileWaitingApproval() {
        MockApi api;
        api.response = [](const QJsonObject &) { return tool("run_command", {{"command", "echo should-not-execute"}}); };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        QSignalSpy closed(&controller, &AppController::approvalClosed);
        controller.send("Teste de cancelamento");
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 3000);
        QVERIFY(controller.streaming());
        controller.stop();
        QVERIFY(!controller.streaming()); QCOMPARE(closed.count(), 1);
        controller.resolveApproval(false, false); // A stale dialog has no callback.
        QCOMPARE(api.requests.size(), 1);
    }
    void dangerousCommandStillRequiresApproval() {
        MockApi api;
        api.response = [&api](const QJsonObject &) {
            if (api.requests.size() == 1)
                return tool("run_command", {{"command", "echo approved"}}, 1);
            if (api.requests.size() == 2)
                return tool("run_command", {{"command", "shutdown --help"}}, 2);
            return answer("Concluído.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        controller.send("Teste de confirmação");
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 3000);
        controller.resolveApproval(true, true);
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 2, 5000);
        QVERIFY(approval.last().at(1).toString().contains("PERIGOSO"));
        controller.resolveApproval(false, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 5000);
        QCOMPARE(api.requests.size(), 3);
    }
    void dangerousCommandRequiresApprovalInFullMode() {
        MockApi api;
        api.response = [](const QJsonObject &) { return tool("run_command", {{"command", "shutdown --help"}}); };
        Config::instance().api().baseUrl = api.url();
        Config::instance().ui().permissionMode = "full";
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        controller.send("Teste de acesso completo");
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 3000);
        controller.stop();
        QCOMPARE(api.requests.size(), 1);
    }
    void denialDoesNotEnableSessionApproval() {
        MockApi api;
        api.response = [&api](const QJsonObject &) {
            if (api.requests.size() <= 2)
                return tool("run_command", {{"command", "echo must-not-run"}}, api.requests.size());
            return answer("Concluído.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        controller.send("Teste de negação");
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 3000);
        controller.resolveApprovalEx(false, true, false);
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 2, 5000);
        controller.resolveApproval(false, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 5000);
        QCOMPARE(api.requests.size(), 3);
    }
    void writeFileCompletesBeforeReportingSuccess() {
        MockApi api; QTemporaryDir dir;
        const QString path = dir.filePath("output.txt");
        api.response = [&api, path](const QJsonObject &) {
            return api.requests.size() == 1
                ? tool("write_file", {{"path", path}, {"content", "Conteúdo salvo\n"}})
                : answer("Concluído.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        QSignalSpy approval(&controller, &AppController::approvalRequested);
        controller.send("Teste de gravação");
        QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 3000);
        controller.resolveApproval(true, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 5000);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("Conteúdo salvo\n"));
        const QJsonArray messages = api.requests.at(1).value("messages").toArray();
        const QJsonObject result = QJsonDocument::fromJson(
            messages.last().toObject().value("content").toString().toUtf8()).object();
        QCOMPARE(result.value("ok").toBool(), true);
    }
#ifdef Q_OS_WIN
    void commandQuotesAndSpaces() {
        MockApi api;
        const QString command = "\"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -Command \"Write-Output 'aspas e espaços'\"";
        api.response = [&api, command](const QJsonObject &) {
            return api.requests.size() == 1 ? tool("run_command", {{"command", command}}) : answer("Comando concluído.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        // Exercise the permission callback through the controller in an isolated test.
        connect(&controller, &AppController::approvalRequested, &controller, [&controller] {
            QTimer::singleShot(0, &controller, [&controller] { controller.resolveApproval(true, false); });
        });
        controller.send("Teste local de aspas");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 5000);
        QCOMPARE(api.requests.size(), 2);
        const auto messages = api.requests.at(1).value("messages").toArray();
        const auto result = QJsonDocument::fromJson(messages.last().toObject().value("content").toString().toUtf8()).object();
        QCOMPARE(result.value("exit_code").toInt(-1), 0);
        QVERIFY2(result.value("output").toString().contains("aspas e espaços"), qPrintable(result.value("output").toString()));
    }
#endif
};

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    AgentRegression test;
    return QTest::qExec(&test, argc, argv);
}
#include "agent-regression.moc"
