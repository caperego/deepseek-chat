#include "appcontroller.h"
#include "config.h"
#include "streamclient.h"
#include <QApplication>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

// No real account, provider, configuration file, or remote database is used.
class MockApi : public QTcpServer {
public:
    QVector<QJsonObject> requests;
    std::function<QByteArray(const QJsonObject &)> response;
    int status = 200;
    bool truncate = false;
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
