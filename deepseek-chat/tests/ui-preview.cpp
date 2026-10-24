#include "appcontroller.h"
#include "config.h"
#include "qmltheme.h"
#include <QApplication>
#include <QtWebEngineQuick>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QTest>
#include <cstdio>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJSValue>

// Render the actual application resources using fictional data only.
// Never load the developer's config, account, MCP processes, or remote API.
class UiPreview {
public:
    static void prepare(AppController &app, const QString &path) {
        app.m_activeProject.workdir = path;
        app.m_user.id = 1;
        app.m_user.username = QStringLiteral("demonstracao");
        app.m_user.displayName = QStringLiteral("Demonstração");
        app.m_currentConversationId = 1;
        app.m_currentTitle = QStringLiteral("Organizar o projeto");
        app.m_currentModel = QStringLiteral("deepseek-flash");
        app.m_currentEffort = QStringLiteral("high");
        app.m_efforts = {QStringLiteral("low"), QStringLiteral("high")};
        app.m_modelsList = {QVariantMap{{"id", "deepseek-flash"}}, QVariantMap{{"id", "deepseek-pro"}}};
        app.m_sidebar = {
            QVariantMap{{"type", "header"}, {"title", "Projetos"}, {"level", 0}},
            QVariantMap{{"type", "project"}, {"title", "Meu aplicativo"}, {"id", 10}},
            QVariantMap{{"type", "conv"}, {"title", "Organizar o projeto"}, {"id", 1}, {"indent", 1}},
            QVariantMap{{"type", "conv"}, {"title", "Revisar alterações"}, {"id", 2}, {"indent", 1}},
            QVariantMap{{"type", "project"}, {"title", "Análise de dados"}, {"id", 11}},
            QVariantMap{{"type", "header"}, {"title", "Conversas"}, {"level", 0}},
            QVariantMap{{"type", "conv"}, {"title", "Planejar a semana"}, {"id", 3}}};
        Message user;
        user.role = QStringLiteral("user");
        user.content = QStringLiteral("Como posso organizar as próximas etapas do meu aplicativo?");
        Message assistant;
        assistant.role = QStringLiteral("assistant");
        assistant.content = QStringLiteral("## Vamos organizar o trabalho\n\n"
            "Podemos acompanhar as mudanças em um projeto e manter cada etapa em uma conversa.\n\n"
            "1. **Revisar o código** e identificar os pontos de melhoria.\n"
            "2. **Executar os testes** antes de concluir cada alteração.\n"
            "3. **Agendar uma revisão** na área de Automações.\n\n"
            "```bash\n./build.sh\n```\n\n"
            "Use o painel de arquivos para comparar e restaurar versões quando precisar.");
        app.m_messages->setMessages({user, assistant});
        app.m_automations.load(path + QStringLiteral("/tasks.json"));
        app.m_automations.add(QStringLiteral("Revisão diária"), QStringLiteral("Revise o estado do projeto e liste as próximas etapas."),
                              60, 1440, 0, QStringLiteral("deepseek-flash"), QStringLiteral("high"), QDateTime::currentMSecsSinceEpoch());
        auto *server = new McpServer;
        server->name = QStringLiteral("arquivos-demo");
        server->status = QStringLiteral("Demonstração visual");
        app.m_mcpServers.append(server);
    }
};

int main(int argc, char **argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QtWebEngineQuick::initialize();
    QApplication application(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.warning=true\n*.critical=true"));
    if (argc < 2) return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(output);
    QTemporaryDir temporary;
    QDir::setCurrent(temporary.path());
    Config::instance().api().baseUrl = QStringLiteral("https://example.invalid/deepseek");
    Config::instance().api().token.clear();
    Config::instance().ui().fontSize = 11;
    Config::instance().ui().sidebarFontSize = 11;
    Config::instance().ui().chatFontSize = 12;
    Config::instance().ui().fontFamily = QStringLiteral("Sans Serif");
    Config::instance().ui().chatFontFamily = QStringLiteral("Sans Serif");
    QmlTheme theme;
    AppController app;
    app.setTheme(&theme);
    UiPreview::prepare(app, temporary.path());
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 1;
    window->resize(1280, 840);
    auto save = [window, output](const QString &name) {
        if (!window->grabWindow().save(output + QLatin1Char('/') + name + QStringLiteral(".png")))
            QCoreApplication::exit(1);
    };
    auto *settings = window->findChild<QObject *>(QStringLiteral("settingsDialog"));
    auto *automations = window->findChild<QObject *>(QStringLiteral("automationsDialog"));
    auto *commandEditor = window->findChild<QObject *>(QStringLiteral("globalCommandEditor"));
    if (!settings || !automations) return 1;
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) == QLatin1String("--workspace")) {
        QTimer::singleShot(300, [&] {
            auto check = [](bool ok, const char *message) {
                if (!ok) { std::fprintf(stderr, "%s\n", message); QCoreApplication::exit(1); }
                return ok;
            };
            auto *main = window->findChild<QQuickItem *>(QStringLiteral("mainView"));
            auto *tools = window->findChild<QQuickItem *>(QStringLiteral("workspaceTools"));
            if (!check(main && tools, "Painel de ferramentas ausente")) return;
            QMetaObject::invokeMethod(main, "openWorkspace", Q_ARG(QVariant, 0));
            QTest::qWait(150);
            const int id = app.startBackgroundTask("printf TASK_VISIBLE; sleep 30");
            tools->setProperty("selectedTask", id);
            QTest::qWait(250);
            if (!check(app.backgroundTaskOutput(id).contains("TASK_VISIBLE"), "Tarefa não iniciou")) return;
            main->setProperty("workspaceOpen", false);
            QTest::qWait(100);
            if (!check(app.backgroundTasks().first().toMap().value("running").toBool(), "Recolher parou a tarefa")) return;
            QMetaObject::invokeMethod(main, "openWorkspace", Q_ARG(QVariant, 2));
            app.runTerminalCommand("printf '%s_%s\\n' TERMINAL VISIVEL");
            for (int i = 0; i < 30 && !tools->property("terminalText").toString().contains("TERMINAL_VISIVEL"); ++i) QTest::qWait(100);
            if (!check(tools->property("terminalText").toString().contains("TERMINAL_VISIVEL"), "Terminal não mostrou saída")) return;
            save(QStringLiteral("terminal-integrado"));
            app.killTerminal();
            QTcpServer server;
            server.listen(QHostAddress::LocalHost);
            QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
                while (server.hasPendingConnections()) {
                    auto *socket = server.nextPendingConnection();
                    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                        socket->readAll();
                        const QByteArray body = "<html><head><title>Preview local OK</title></head><body style='font-family:sans-serif;padding:32px'><h1>Previa do projeto</h1><button id='check' onclick=\"document.title='Interacao OK'\">Testar interacao</button></body></html>";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
            QMetaObject::invokeMethod(main, "openWorkspace", Q_ARG(QVariant, 1));
            main->setProperty("workspaceExpanded", true);
            QTest::qWait(400);
            auto *browser = window->findChild<QObject *>(QStringLiteral("browserPane"));
            auto *web = window->findChild<QObject *>(QStringLiteral("workspaceWebView"));
            if (!check(browser && web, "Navegador não carregou")) return;
            const QString url = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());
            QMetaObject::invokeMethod(browser, "navigate", Q_ARG(QVariant, url));
            for (int i = 0; i < 100 && web->property("title").toString() != "Preview local OK"; ++i) QTest::qWait(100);
            if (!check(web->property("title").toString() == "Preview local OK", "Página HTTP não carregou")) return;
            engine.globalObject().setProperty("qaWeb", engine.newQObject(web));
            engine.evaluate(QStringLiteral("qaWeb.runJavaScript(\"document.getElementById('check').click()\")"));
            for (int i = 0; i < 30 && web->property("title").toString() != "Interacao OK"; ++i) QTest::qWait(100);
            if (!check(web->property("title").toString() == "Interacao OK", "JavaScript não interagiu com a página")) return;
            save(QStringLiteral("navegador-integrado"));
            QMetaObject::invokeMethod(main, "openWorkspace", Q_ARG(QVariant, 0));
            QTest::qWait(150);
            save(QStringLiteral("tarefas-segundo-plano"));
            app.stopBackgroundTask(id);
            std::fprintf(stdout, "Workspace: tarefa em segundo plano, terminal PTY, HTTP local e JavaScript verificados.\n");
            QCoreApplication::exit(0);
        });
        return application.exec();
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) == QLatin1String("--artifact")) {
        QFile file(temporary.path() + QStringLiteral("/demonstracao.md"));
        if (!file.open(QIODevice::WriteOnly)) return 1;
        file.write("# Arquivo de demonstração\n\nConteúdo para testar o painel redimensionável.\n");
        file.close();
        const QString path = file.fileName();
        QTimer::singleShot(250, [&, path] {
            auto check = [](bool condition, const char *message) {
                if (!condition) { std::fprintf(stderr, "%s\n", message); QCoreApplication::exit(1); }
                return condition;
            };
            app.openArtifact(path);
            QTest::qWait(100);
            auto *main = window->findChild<QQuickItem *>(QStringLiteral("mainView"));
            auto *panel = window->findChild<QQuickItem *>(QStringLiteral("artifactPanel"));
            auto *handle = window->findChild<QQuickItem *>(QStringLiteral("artifactResizeHandle"));
            auto *button = window->findChild<QQuickItem *>(QStringLiteral("expandArtifactButton"));
            auto *editor = window->findChild<QQuickItem *>(QStringLiteral("artifactEditor"));
            auto *conversation = window->findChild<QQuickItem *>(QStringLiteral("conversationPane"));
            if (!check(main && panel && handle && button && editor && conversation, "Painel não encontrado")) return;
            const QString draft = editor->property("text").toString() + QStringLiteral("\nEdição não salva.");
            editor->setProperty("text", draft);
            const qreal initial = panel->width();
            const QPoint start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(window, start - QPoint(100, 0), 50);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start - QPoint(100, 0));
            QTest::qWait(100);
            if (!check(panel->width() > initial + 60, "A divisória não ampliou o painel")) return;
            save(QStringLiteral("arquivo-redimensionado"));
            auto clickExpand = [&] {
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
                QTest::qWait(100);
            };
            clickExpand();
            if (!check(panel->width() >= window->width() - 4 && !conversation->isVisible(), "O painel não expandiu na janela")) return;
            if (!check(editor->property("text").toString() == draft, "A expansão perdeu o texto em edição")) return;
            save(QStringLiteral("arquivo-expandido"));
            clickExpand();
            if (!check(conversation->isVisible() && panel->width() > initial + 60, "A redução não restaurou o layout")) return;
            window->resize(920, 600);
            QTest::qWait(150);
            if (!check(panel->width() <= main->property("artifactMaximumWidth").toReal() + 2, "Painel ultrapassou a largura disponível")) return;
            if (!check(editor->property("text").toString() == draft, "O redimensionamento perdeu o texto em edição")) return;
            save(QStringLiteral("arquivo-janela-pequena"));
            clickExpand();
            app.closeArtifact();
            QTest::qWait(100);
            if (!check(conversation->isVisible() && !main->property("artifactExpanded").toBool(), "Fechar não restaurou a conversa")) return;
            std::fprintf(stdout, "Painel: arrastar, expandir, reduzir, preservar edição e fechar verificados.\n");
            QCoreApplication::quit();
        });
        return application.exec();
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) == QLatin1String("--qa")) {
        int step = 0;
        for (bool dark : {false, true}) {
            for (int section = 0; section < 9; ++section) {
                const int start = 250 + step++ * 400;
                QTimer::singleShot(start, [&, dark, section] {
                    window->resize(920, 600);
                    Config::instance().ui().theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
                    theme.setMode(Config::instance().ui().theme);
                    theme.setFontSize(14);
                    QMetaObject::invokeMethod(settings, "close");
                    QMetaObject::invokeMethod(automations, "close");
                    if (commandEditor) QMetaObject::invokeMethod(commandEditor, "close");
                    if (section < 7) { settings->setProperty("sectionIndex", section); QMetaObject::invokeMethod(settings, "open"); }
                    else if (section == 7) QMetaObject::invokeMethod(automations, "open");
                    else if (commandEditor) {
                        settings->setProperty("sectionIndex", 4);
                        QMetaObject::invokeMethod(settings, "open");
                        QMetaObject::invokeMethod(commandEditor, "open");
                    }
                });
                QTimer::singleShot(start + 250, [&, dark, section] {
                    save(QStringLiteral("qa-%1-%2").arg(dark ? "escuro" : "claro").arg(section));
                });
            }
        }
        QTimer::singleShot(250 + step * 400, &application, &QCoreApplication::quit);
        return application.exec();
    }
    QTimer::singleShot(250, [&] { theme.setMode(QStringLiteral("light")); });
    QTimer::singleShot(550, [&] { save("chat-claro"); QMetaObject::invokeMethod(settings, "open"); });
    QTimer::singleShot(1000, [&] { save("configuracoes-claro"); settings->setProperty("sectionIndex", 5); });
    QTimer::singleShot(1400, [&] { save("plugins-claro"); QMetaObject::invokeMethod(settings, "close"); QMetaObject::invokeMethod(automations, "open"); });
    QTimer::singleShot(1900, [&] { save("automacoes-claro"); QMetaObject::invokeMethod(automations, "close"); Config::instance().ui().theme = QStringLiteral("dark"); theme.setMode(QStringLiteral("dark")); });
    QTimer::singleShot(2400, [&] { save("chat-escuro"); QMetaObject::invokeMethod(settings, "open"); settings->setProperty("sectionIndex", 0); });
    QTimer::singleShot(2900, [&] { save("configuracoes-escuro"); });
    QTimer::singleShot(3300, &application, &QCoreApplication::quit);
    return application.exec();
}
