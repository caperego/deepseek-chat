#include "appcontroller.h"
#include "config.h"
#include "qmltheme.h"
#include <QApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <QLoggingCategory>

// Render the actual application resources using fictional data only.
// Never load the developer's config, account, MCP processes, or remote API.
class UiPreview {
public:
    static void prepare(AppController &app, const QString &path) {
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
