#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QDesktopServices>
#include <QFileInfo>
#include <cstdio>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QtGlobal>
#include <QQmlContext>

#include "appinfo.h"
#include "licensemodel.h"
#include "qmlapi/opcuamanager.h"
#include "qmlapi/projectmanager.h"
#include "qmlapi/servernodemodel.h"
#include "qmlapi/serverruntimecontroller.h"
#include "qmlapi/serverstudio.h"
#include "models/attributesmodel.h"
#include "models/dataaccessmodel.h"
#include "models/logfiltermodel.h"
#include "models/logmodel.h"
#include "models/opcuamodel.h"
#include "core/apppaths.h"
#include "core/opcuaservice.h"
#include "core/opcuanodedata.h"
#include "core/opcuavaluedata.h"
#include "core/opcuavaluetree.h"

#include "appengine.h"

namespace {
constexpr auto kLogFileName = "app.log";
constexpr auto kRotatedLogFileName = "app.log.1";

/*!
 * \internal
 * \brief Size at which the log file is rotated, in bytes.
 *
 * The log used to grow without bound across runs. One rotation step keeps the
 * previous session's tail available while capping the disk footprint.
 */
constexpr qint64 kMaxLogFileBytes = 5 * 1024 * 1024;

QtMessageHandler g_prevQtMessageHandler = nullptr;
QFile  g_logFile;
QMutex g_logMutex;
bool   g_logInitAttempted = false;

/*!
 * \internal
 * \brief In-memory log fed by the message handler; null once the engine is gone.
 *
 * Guarded by \c g_logModelMutex because Qt message handlers run on every thread
 * the application uses, while the model itself lives in the GUI thread and is
 * therefore only ever touched through a queued invocation.
 */
LogModel* g_logModel = nullptr;
QMutex g_logModelMutex;

/*!
 * \internal
 * \brief Maps a Qt message type to the shared diagnostics severity.
 */
int diagnosticsLevelForType(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return Diagnostics::Debug;
    case QtInfoMsg: return Diagnostics::Info;
    case QtWarningMsg: return Diagnostics::Warning;
    case QtCriticalMsg:
    case QtFatalMsg: return Diagnostics::Error;
    }
    return Diagnostics::Info;
}

/*!
 * \internal
 * \brief Forwards one message to the in-memory log model, if one is installed.
 */
void appendLogLineToModel(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (QCoreApplication::closingDown())
        return;

    // Appending emits model signals, so a view reacting to them could log again
    // and re-enter this function. The guard is checked before the mutex is taken
    // because QMutex is not recursive and re-entering would otherwise deadlock.
    static thread_local bool appending = false;
    if (appending)
        return;

    appending = true;

    {
        QMutexLocker locker(&g_logModelMutex);
        if (g_logModel) {
            const QString category =
                context.category ? QString::fromUtf8(context.category) : QString();
            const int level = diagnosticsLevelForType(type);

            if (QThread::currentThread() == g_logModel->thread()) {
                // Deliver directly on the GUI thread. Posting an event would need
                // a live event dispatcher, which is not guaranteed once the
                // application is being torn down.
                g_logModel->appendEntry(level, category, msg);
            } else {
                QMetaObject::invokeMethod(g_logModel, "appendEntry", Qt::QueuedConnection,
                                          Q_ARG(int, level),
                                          Q_ARG(QString, category),
                                          Q_ARG(QString, msg));
            }
        }
    }

    appending = false;
}

/*!
 * \internal
 * \brief Extracts a compact method name from a Qt logging context.
 */
QString formatContextFunction(const QMessageLogContext& context)
{
    const QString functionInfo = context.function ? QString::fromUtf8(context.function) : QString();
    if (functionInfo.isEmpty())
        return QStringLiteral("Unknown method");
    static const QRegularExpression reMethod(QStringLiteral(R"((\w+::\w+))"));
    auto m = reMethod.match(functionInfo);
    if (m.hasMatch())
        return m.captured(1);
    return functionInfo;
}


/*!
 * \internal
 * \brief Returns the textual log level for \a type.
 */
QString messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("DEBUG");
    case QtInfoMsg: return QStringLiteral("INFO");
    case QtWarningMsg: return QStringLiteral("WARNING");
    case QtCriticalMsg: return QStringLiteral("CRITICAL");
    case QtFatalMsg: return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

/*!
 * \internal
 * \brief Opens the release log file while \c g_logMutex is held.
 */
bool ensureLogFileOpenLocked()
{
    if (g_logFile.isOpen())
        return true;
    if (g_logInitAttempted)
        return false;
    g_logInitAttempted = true;
    const QString logDirPath = AppPaths::instance().logDir();
    if (!QDir().mkpath(logDirPath))
        return false;
    const QString logFilePath = QDir(logDirPath).filePath(QString::fromLatin1(kLogFileName));

    // Rotate before appending so a long-lived installation keeps at most the
    // current file plus one previous generation.
    if (QFileInfo(logFilePath).size() >= kMaxLogFileBytes) {
        const QString rotatedPath =
            QDir(logDirPath).filePath(QString::fromLatin1(kRotatedLogFileName));
        QFile::remove(rotatedPath);
        QFile::rename(logFilePath, rotatedPath);
    }

    g_logFile.setFileName(logFilePath);
    return g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

/*!
 * \internal
 * \brief Returns the absolute log file path once the log file has been opened.
 */
QString openedLogFilePath()
{
    QMutexLocker locker(&g_logMutex);
    return g_logFile.isOpen() ? QFileInfo(g_logFile).absoluteFilePath() : QString();
}

/*!
 * \internal
 * \brief Appends one formatted \a line to the release log file.
 */
void appendLogLineToFile(const QString& line)
{
    QMutexLocker locker(&g_logMutex);
    if (ensureLogFileOpenLocked()) {
        QTextStream ts(&g_logFile);
        ts << line << '\n';
        ts.flush();
    }
}

/*!
 * \internal
 * \brief Writes Qt log messages to the application log and forwards them to the previous handler.
 */
void customLogMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (msg.startsWith(QLatin1String("Model size of")))
        return;
    const QString contextString = QStringLiteral("(%1; %2:%3)")
                                      .arg(formatContextFunction(context),
                                           context.file
                                               ? QString::fromUtf8(context.file)
                                               : QStringLiteral("<unknown>"),
                                           QString::number(context.line));
    const QString line = QStringLiteral("%1 [%2] %3 %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  messageTypeName(type),
                                  msg,
                                  contextString);
    appendLogLineToModel(type, context, msg);

    // The log file stays a release-build facility; a debug session already has
    // the message on stderr and in the debugger.
#ifdef QT_NO_DEBUG
    appendLogLineToFile(line);
#else
    Q_UNUSED(line)
#endif

    if (g_prevQtMessageHandler) {
        g_prevQtMessageHandler(type, context, msg);
        return;
    }

    // qInstallMessageHandler() returns null when the default handler was in
    // place, so there is nothing to chain to. Writing the formatted message out
    // here keeps the console output this handler would otherwise swallow.
    const QByteArray formatted = qFormatLogMessage(type, context, msg).toLocal8Bit();
    std::fputs(formatted.constData(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

} // namespace

/*!
 * \brief Creates the QML engine and registers the C++ objects used by QML.
 * The OPC UA facade remains in the GUI thread while the service owns
 * Qt OPC UA runtime objects in a dedicated worker thread.
 */
AppEngine::AppEngine(const QString& initialUrl, QObject* parent)
    : QQmlApplicationEngine(parent)
    , m_initialUrl(initialUrl)
    , m_opcUaManager(new OpcUaManager(initialUrl, this))
    , m_projectManager(new ProjectManager(this))
    , m_serverStudio(new ServerStudio(this))
    , m_appInfo(new AppInfo(this))
    , m_licenseModel(new LicenseModel(this))
    , m_logModel(new LogModel(2000, this))
    , m_logFilterModel(new LogFilterModel(this))
{
    m_logFilterModel->setSourceModel(m_logModel);

    // Publish the model before installing the handler so no message is lost.
    {
        QMutexLocker locker(&g_logModelMutex);
        g_logModel = m_logModel;
    }

    qRegisterMetaType<OpcUaNodeData>("OpcUaNodeData");
    qRegisterMetaType<QList<OpcUaNodeData>>("QList<OpcUaNodeData>");
    qRegisterMetaType<OpcUaValueUpdate>("OpcUaValueUpdate");
    qRegisterMetaType<OpcUaAttributeData>("OpcUaAttributeData");
    qRegisterMetaType<OpcUaValueTreeNode>("OpcUaValueTreeNode");

    qmlRegisterUncreatableType<AppEngine>("Cpp.AppEngine", 1, 0, "AppEngine", QStringLiteral("AppEngine is a subclass of QQmlApplicationEngine and should not be created in QML."));
    qmlRegisterUncreatableType<LogFilterModel>("Cpp.AppEngine", 1, 0, "LogFilterModel", QStringLiteral("LogFilterModel is exposed by AppEngine::logModel."));
    rootContext()->setContextProperty("cppAppEngine", this);
    qmlRegisterUncreatableType<OpcUaManager>("Cpp.OpcUaManager", 1, 0, "OpcUaManager", QStringLiteral("OpcUaManager should not be created in QML."));
    qmlRegisterUncreatableType<OpcUaModel>("Cpp.OpcUaManager", 1, 0, "OpcUaModel", QStringLiteral("OpcUaModel is exposed by OpcUaManager::treeModel."));
    qmlRegisterUncreatableType<DataAccessModel>("Cpp.OpcUaManager", 1, 0, "DataAccessModel", QStringLiteral("DataAccessModel is exposed by OpcUaManager::dataModel."));
    qmlRegisterUncreatableType<AttributesModel>("Cpp.OpcUaManager", 1, 0, "AttributesModel", QStringLiteral("AttributesModel is exposed by OpcUaManager::attributesModel."));
    rootContext()->setContextProperty("cppManagerOpcUa", m_opcUaManager);

    qmlRegisterUncreatableType<ProjectManager>("Cpp.ProjectManager", 1, 0, "ProjectManager", QStringLiteral("ProjectManager should not be created in QML."));
    m_projectManager->setOpcUaManager(m_opcUaManager);
    rootContext()->setContextProperty("cppProjectManager", m_projectManager);

    // Server Studio facade drives the headless OPC UA server runtime and reuses
    // the client facade for its "Open in Client" action. ServerRuntimeController
    // is registered uncreatable so QML can name its State enum values.
    qmlRegisterUncreatableType<ServerStudio>("Cpp.ServerStudio", 1, 0, "ServerStudio", QStringLiteral("ServerStudio should not be created in QML."));
    qmlRegisterUncreatableType<ServerRuntimeController>("Cpp.ServerStudio", 1, 0, "ServerRuntimeController", QStringLiteral("ServerRuntimeController is owned by ServerStudio."));
    qmlRegisterUncreatableType<ServerNodeModel>("Cpp.ServerStudio", 1, 0, "ServerNodeModel", QStringLiteral("ServerNodeModel is exposed by ServerStudio::nodeModel."));
    m_serverStudio->setOpcUaManager(m_opcUaManager);
    rootContext()->setContextProperty("cppServerStudio", m_serverStudio);

    // Application/build metadata and the bundled license documents shown by the
    // Help > About dialog. The license texts are embedded as resources under
    // /licenses (see resources/CMakeLists.txt), so the model scans the resource
    // directory rather than a deployed folder.
    m_licenseModel->setDirectory(QStringLiteral("qrc:/licenses/LICENSES"));
    rootContext()->setContextProperty("cppAppInfo", m_appInfo);
    rootContext()->setContextProperty("cppLicenseModel", m_licenseModel);

    // Installed in every configuration: the log panel is a debugging aid the user
    // needs in a development build too. Writing to the log file stays
    // release-only inside the handler itself.
    g_prevQtMessageHandler = qInstallMessageHandler(customLogMessageHandler);

    // First entry of every session: it dates the log, states which build wrote
    // it, and keeps the log panel from opening on an empty list.
    qInfo() << QCoreApplication::applicationName()
            << QCoreApplication::applicationVersion() << "started";

    m_logModel->setLogFilePath(openedLogFilePath());
}

/*!
 * \brief Removes every entry from the in-memory application log.
 */
void AppEngine::clearLog()
{
    if (m_logModel)
        m_logModel->clear();
}

/*!
 * \brief Opens the directory holding the log file in the system file manager.
 * \return \c false when no log file has been written in this session.
 */
bool AppEngine::showLogFileLocation()
{
    // The path is only known once the first message has been written, so refresh
    // it here rather than relying on the value captured at construction time.
    const QString path = openedLogFilePath();
    if (m_logModel)
        m_logModel->setLogFilePath(path);

    if (path.isEmpty())
        return false;

    return QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

/*!
 * \brief Injects the INI settings store into the OPC UA facade.
 * \param settings Non-owning settings store used for last-connection and focus
 *        persistence; may be null to disable persistence.
 */
void AppEngine::setSettings(QSettings* settings)
{
    if (m_opcUaManager)
        m_opcUaManager->setSettings(settings);
    if (m_projectManager)
        m_projectManager->setSettings(settings);
}

void AppEngine::createOpcUaRuntime()
{
    if (m_opcUaThread || m_opcUaService)
        return;

    m_opcUaThread = new QThread(this);
    m_opcUaThread->setObjectName(QStringLiteral("OpcUaThread"));

    QObject *opcUaBootstrap = new QObject;
    opcUaBootstrap->moveToThread(m_opcUaThread);
    connect(m_opcUaThread, &QThread::finished, opcUaBootstrap, &QObject::deleteLater);

    m_opcUaThread->start();

    QMetaObject::invokeMethod(opcUaBootstrap,
                              [this]() {
                                  m_opcUaService = new OpcUaService(m_initialUrl);
                              },
                              Qt::BlockingQueuedConnection);

    if (!m_opcUaService) {
        qCritical() << "Failed to create OPC UA service in OpcUaThread.";
        return;
    }

    connect(m_opcUaThread, &QThread::finished, m_opcUaService, &QObject::deleteLater);
    m_opcUaManager->attachService(m_opcUaService);
    qInfo() << "OPC UA runtime objects created in OpcUaThread.";
}

/*!
 * \brief Stops the OPC UA worker thread.
 */
AppEngine::~AppEngine()
{
    // Stop feeding the GUI-thread log model first: the worker thread still logs
    // while it shuts down, and posting those messages to an event loop that is
    // already winding down has no value and can only fail.
    {
        QMutexLocker modelLocker(&g_logModelMutex);
        g_logModel = nullptr;
    }

    if (m_opcUaThread && m_opcUaThread->isRunning()) {
        m_opcUaThread->quit();
        m_opcUaThread->wait();
    }
    m_opcUaService = nullptr;
    m_opcUaManager = nullptr;
    m_projectManager = nullptr;

    qInstallMessageHandler(g_prevQtMessageHandler);
    g_prevQtMessageHandler = nullptr;

    m_logFilterModel = nullptr;
    m_logModel = nullptr;

    QMutexLocker locker(&g_logMutex);
    if (g_logFile.isOpen()) {
        g_logFile.flush();
        g_logFile.close();
    }
    g_logInitAttempted = false;
}

/*!
 * \brief Initializes the OPC UA backend service once.
 * Returns \c true when initialization was performed or had already been
 * requested; \c false when the service object is missing.
 */
bool AppEngine::startOpcUaBackend()
{
    if (m_opcUaBackendStartRequested) {
        qWarning() << "OPC UA backend startup was requested more than once.";
        return true;
    }

    if (!m_opcUaManager) {
        qCritical() << "Cannot start OPC UA backend because the manager is missing.";
        return false;
    }

    m_opcUaBackendStartRequested = true;

    QTimer::singleShot(0, this, [this]() { createOpcUaRuntime(); });
    qInfo() << "OPC UA backend runtime creation queued.";
    return true;
}
