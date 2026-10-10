#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QStandardPaths>

#include "mpdcontroller.h"
#include "artworkprovider.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("MpdBee");
    QGuiApplication::setOrganizationName("MpdBee");

    QQuickStyle::setStyle("Basic");

    const QString cachePath =
        QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation);

    QDir().mkpath(cachePath + "/artwork");

    MpdController mpd;
    QQmlApplicationEngine engine;

    engine.addImageProvider(
        QStringLiteral("covers"),
        new ArtworkProvider());

    engine.rootContext()->setContextProperty(
        QStringLiteral("mpd"), &mpd);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            QCoreApplication::exit(EXIT_FAILURE);
        },
        Qt::QueuedConnection);

    engine.loadFromModule("MpdBee", "Main");

    return app.exec();
}
