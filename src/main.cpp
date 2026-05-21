#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QLocale>
#include <QString>
#include <QTranslator>

#include "core/appcore.h"
#include "gui/appengine.h"

using namespace Qt::StringLiterals;

int main(int argc, char* argv[])
{
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));

    // qputenv("QT_DEBUG_PLUGINS", "1");
    // qputenv("QT_LOGGING_RULES", "qt.qml.*=true;qt.quick.*=true;qt.opcua.*=true");


    AppCore::setMessagePattern();
    AppCore app(argc, argv);
    AppCore::setApplicationVersion(QLatin1String("1.0.0"));
    AppCore::setApplicationName(QLatin1String("TestApp-MANAGER"));
    AppCore::setOrganizationName(QLatin1String("TestApp GmbH"));
    AppCore::setOrganizationDomain(QLatin1String("TestApp.url.com"));

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for(const QString& locale : uiLanguages)
    {
        const QString baseName = "OpcUaManager_" + QLocale(locale).name();
        if(translator.load(":/translations/" + baseName))
        {
            app.installTranslator(&translator);
            break;
        }
    }

    if(QFontDatabase::addApplicationFont(":/font/fontello.ttf") == -1)
        qWarning() << "Failed to load fontello.ttf";

    QCommandLineParser parser;
    parser.setApplicationDescription("This application is intended for studying and testing new features of Qt.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("url"_L1, "The url to open."_L1);
    // parser.addOptions({{{"u", "url"}, "Sets url of the OPC UA Server to connect", "opc.tcp://ip:port"_L1, "opc.tcp://127.0.0.1:4840"_L1}});

    // Check if command line arguments are sane. Otherwise show help and exit.
    if(!parser.parse(app.arguments()))
    {
        parser.showHelp(1);
    }

    parser.process(app);
    const auto positionalArguments = parser.positionalArguments();
    const auto initialUrl          = positionalArguments.value(0, "opc.tcp://127.0.0.1:4840"_L1);
    // const auto initialUrl = parser.value("url"); // QUrl("opc.tcp://127.0.0.1:4840")

    AppEngine engine(initialUrl.trimmed());

    engine.addImportPath(":/qml");

#ifdef QMAKE_PROJECT
    const QUrl url(u"qrc:/qml/Main.qml"_s);
    QObject::connect(
        &engine, &AppEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);
#else
    QObject::connect(
        &engine, &AppEngine::objectCreated, &app,
        [](QObject *obj, const QUrl &objUrl) {
            Q_UNUSED(objUrl);
            if (!obj)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.loadFromModule("OpcUaManager", "Main");
#endif

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
