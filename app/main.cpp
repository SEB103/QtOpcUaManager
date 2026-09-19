#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QString>
#include <QtQml/QQmlExtensionPlugin>
#include <QtWebView/QtWebView>

#include "appcore.h"
#include "appengine.h"
#include "localecontroller.h"
#include "updatecontroller.h"
#include "core/apppaths.h"
#include "productinfo.h"

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

    // Initialize QtWebView before the QML engine loads any WebView; the offline
    // Help window (HelpViewerWindow.qml) renders the bundled documentation with it.
    QtWebView::initialize();

    // Identity comes from the centralized product metadata. The organization,
    // domain and application name are the STABLE identifier (not the display
    // name), so QSettings paths and per-user data directories are unaffected by
    // a future product rename.
    QCoreApplication::setOrganizationName(QStringLiteral(PRODUCT_IDENTIFIER));
    QCoreApplication::setOrganizationDomain(QStringLiteral(PRODUCT_ORG_DOMAIN));
    QCoreApplication::setApplicationName(QStringLiteral(PRODUCT_IDENTIFIER));
    QCoreApplication::setApplicationVersion(QStringLiteral(PRODUCT_VERSION));

    // Resolve installed/portable data locations once, then seed the writable
    // database and migrate any legacy settings before anything opens them.
    AppPaths::instance().initialize();
    AppPaths::instance().ensureSeededOnFirstRun();

    // Set the application icon used for the window title bar, the taskbar and
    // Alt+Tab. The multi-size .ico is bundled via resources/CMakeLists.txt; Qt
    // selects the frame matching the requested size. The Windows .exe carries
    // the same icon through resources/images/app/app.rc.
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/images/app/OpcUaManager.ico")));

    // Create the INI settings store in an ini/ folder under the resolved config
    // directory (next to the executable in portable mode, AppConfigLocation when
    // installed). It persists the last connection and the pinned focus node.
    app.createSettings(QCoreApplication::applicationName(),
                       AppPaths::instance().configDir());

    // Install the UI language before any QML is created. The controller resolves
    // the saved preference (falling back to the system locale, then English) and
    // outlives the engine so it can also switch languages live at runtime.
    LocaleController localeController(app.settings());
    localeController.applyInitialLanguage();

    // Update checker exposed to QML as cppUpdate. It persists the automatic-check
    // preference through the same INI store and outlives the engine.
    UpdateController updateController(app.settings());

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
    engine.setSettings(app.settings());

    // Expose the language selector to QML and let it retranslate the engine on a
    // live switch. Wiring happens before loadFromModule so cppLocale is available
    // to the first objects the engine creates.
    engine.setLocaleController(&localeController);
    localeController.setEngine(&engine);
    engine.setUpdateController(&updateController);

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
