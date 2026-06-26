#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QString>
#include <QTranslator>
#include <QtQml/QQmlExtensionPlugin>

#include "appcore.h"
#include "appengine.h"

using namespace Qt::StringLiterals;

Q_IMPORT_QML_PLUGIN(OpcUaManagerPlugin)
Q_IMPORT_QML_PLUGIN(BasePlugin)

int main(int argc, char *argv[])
{
    // Useful diagnostics when investigating deployment or OPC UA startup:
    // qputenv("QT_DEBUG_PLUGINS", "1");
    // qputenv("QT_LOGGING_RULES", "qt.qml.*=true;qt.quick.*=true;qt.opcua.*=true");

    AppCore::setMessagePattern();
    AppCore app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("OpcUaManager"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OPCUAMANAGER_VERSION));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = QStringLiteral("OpcUaManager_") + QLocale(locale).name();
        if (translator.load(QStringLiteral(":/translations/") + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Qt OPC UA client for browsing and testing OPC UA servers."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("url"),
        QStringLiteral("Initial OPC UA discovery URL."));
    parser.process(app);

    const QString initialUrl = parser.positionalArguments().value(
        0,
        QStringLiteral("opc.tcp://127.0.0.1:4840"));

    AppEngine engine(initialUrl.trimmed());

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("OpcUaManager", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Application startup failed because no QML root objects were created.";
        return -1;
    }

    if (!engine.startOpcUaBackend()) {
        qCritical() << "Application startup failed because OPC UA backend startup could not be queued.";
        return -1;
    }

    return app.exec();
}
