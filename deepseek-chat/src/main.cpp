#include "appcontroller.h"
#include "assets.h"
#include "config.h"
#include "qmltheme.h"

#include <QApplication>
#include <QtWebEngineQuick>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QtWebEngineQuick::initialize();
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

    QmlTheme theme;
    AppController controller;
    controller.setTheme(&theme);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        controller.setWindow(window);
        // Paint the application before starting requests that use nested event loops.
        QObject::connect(window, &QQuickWindow::frameSwapped,
                         &controller, &AppController::tryAutoLogin,
                         Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
    } else {
        QTimer::singleShot(0, &controller, &AppController::tryAutoLogin);
    }

    return app.exec();
}
