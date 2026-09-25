#include "appcontroller.h"
#include "assets.h"
#include "config.h"
#include "qmltheme.h"

#include <QApplication>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSplashScreen>

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DeepSeek Chat"));
    QApplication::setOrganizationName(QStringLiteral("DeepSeekChat"));
    QApplication::setWindowIcon(Assets::appIcon());

    QString error;
    if (!Config::instance().load(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("DeepSeek Chat"),
                              QStringLiteral("Falha ao carregar config.ini:\n") + error);
        return 1;
    }

    QSplashScreen splash(Assets::logoPixmap(320));
    splash.show();
    splash.showMessage(QStringLiteral("Conectando à API..."),
                       Qt::AlignBottom | Qt::AlignHCenter,
                       QColor(QStringLiteral("#4D6BFE")));
    app.processEvents();

    QmlTheme theme;
    AppController controller;
    controller.setTheme(&theme);

    controller.tryAutoLogin();
    app.processEvents();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().first()))
        controller.setWindow(window);

    splash.close();
    return app.exec();
}
