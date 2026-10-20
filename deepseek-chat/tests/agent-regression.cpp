#include "appcontroller.h"
#include "config.h"
#include "pathpolicy.h"
#include "boundedoutput.h"
#include "automationstore.h"
#include "streamclient.h"
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

class AgentRegression : public QObject {
    Q_OBJECT
private slots:
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
        const auto python = QStandardPaths::findExecutable("python3");
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
            if (messages.last().toObject().value("role") == "tool") return answer("Pronto");
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
    void finalResponseAfterStepLimit() {
        MockApi api; QTemporaryDir dir;
        api.response = [&api, &dir](const QJsonObject &) {
            return api.requests.size() <= 12 ? tool("list_files", {{"path", dir.path()}}, api.requests.size()) : answer("Concluído com os resultados disponíveis.");
        };
        Config::instance().api().baseUrl = api.url();
        AppController controller; controller.setCurrentModel("deepseek-flash");
        connect(&controller, &AppController::approvalRequested, &controller, [&controller] {
            QTimer::singleShot(0, &controller, [&controller] { controller.resolveApproval(true, false); });
        });
        controller.send("Teste de etapas");
        QTRY_VERIFY_WITH_TIMEOUT(!controller.streaming(), 8000);
        QCOMPARE(api.requests.size(), 13);
        QVERIFY(!api.requests.last().contains("tools"));
        QCOMPARE(api.requests.last().value("model").toString(), QString("deepseek-flash"));
        const auto messages = api.requests.last().value("messages").toArray();
        int assistants = 0, results = 0;
        for (const auto &entry : messages) {
            const auto item = entry.toObject();
            if (item.value("role") == "assistant") {
                ++assistants;
                QVERIFY(item.value("reasoning_content").toString().startsWith("Contexto da etapa"));
            }
            if (item.value("role") == "tool") ++results;
        }
        QCOMPARE(assistants, 12); QCOMPARE(results, 12);
        const auto *model = controller.messageModel();
        QCOMPARE(model->data(model->index(model->rowCount() - 1, 0), MessageListModel::ContentRole).toString(), QString("Concluído com os resultados disponíveis."));
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
        const QJsonArray messages = api.requests.last().value("messages").toArray();
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
        const auto messages = api.requests.last().value("messages").toArray();
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
