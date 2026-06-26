#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QtGlobal>
#include <QQmlContext>

#include "qmlapi/opcuamanager.h"
#include "models/opcuamodel.h"
#include "core/opcuaservice.h"
#include "core/opcuanodedata.h"

#include "appengine.h"

namespace {
constexpr auto kLogDirectoryName = "log";
constexpr auto kLogFileName = "app.log";
QtMessageHandler g_prevQtMessageHandler = nullptr;
QObject* g_guiLogSink = nullptr;
QFile  g_logFile;
QMutex g_logMutex;
bool   g_logInitAttempted = false;

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

bool ensureLogFileOpenLocked()
{
    if (g_logFile.isOpen())
        return true;
    if (g_logInitAttempted)
        return false;
    g_logInitAttempted = true;
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString logDirPath = QDir(exeDir).filePath(QString::fromLatin1(kLogDirectoryName));
    if (!QDir().mkpath(logDirPath))
        return false;
    const QString logFilePath = QDir(logDirPath).filePath(QString::fromLatin1(kLogFileName));
    g_logFile.setFileName(logFilePath);
    return g_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void appendLogLineToFile(const QString& line)
{
    QMutexLocker locker(&g_logMutex);
    if (ensureLogFileOpenLocked()) {
        QTextStream ts(&g_logFile);
        ts << line << '\n';
        ts.flush();
    }
}

void customLogMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (msg.startsWith(QLatin1String("Model size of")))
        return;
    const QString message = msg;
    const QString contextStr = QStringLiteral("  (%1; %2: %3)")
                                   .arg(formatContextFunction(context), context.file ? QString::fromUtf8(context.file) : QStringLiteral("<unknown>"), QString::number(context.line));
    appendLogLineToFile(message + contextStr);
    if (g_prevQtMessageHandler)
        g_prevQtMessageHandler(type, context, msg);
}

void passthroughMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (g_prevQtMessageHandler)
        g_prevQtMessageHandler(type, context, msg);
}
} // namespace

/*!
 * \brief Creates the QML engine and registers the C++ objects used by QML.
 * \details The OPC UA facade remains in the GUI thread while the service owns
 * Qt OPC UA runtime objects in a dedicated worker thread.
 */
AppEngine::AppEngine(const QString& initialUrl, QObject* parent)
    : QQmlApplicationEngine(parent)
    , m_initialUrl(initialUrl)
    , m_opcUaManager(new OpcUaManager(initialUrl, this))
{
    qRegisterMetaType<OpcUaNodeData>("OpcUaNodeData");
    qRegisterMetaType<QList<OpcUaNodeData>>("QList<OpcUaNodeData>");

    qmlRegisterUncreatableType<AppEngine>("Cpp.AppEngine", 1, 0, "AppEngine", QStringLiteral("AppEngine is a subclass of QQmlApplicationEngine and should not be created in QML."));
    rootContext()->setContextProperty("cppAppEngine", this);
    qmlRegisterUncreatableType<OpcUaManager>("Cpp.OpcUaManager", 1, 0, "OpcUaManager", QStringLiteral("OpcUaManager should not be created in QML."));
    qmlRegisterUncreatableType<OpcUaModel>("Cpp.OpcUaManager", 1, 0, "OpcUaModel", QStringLiteral("OpcUaModel is exposed by OpcUaManager::treeModel."));
    rootContext()->setContextProperty("cppManagerOpcUa", m_opcUaManager);

    g_guiLogSink = nullptr;
#ifdef QT_NO_DEBUG
    g_prevQtMessageHandler = qInstallMessageHandler(customLogMessageHandler);
#else
    g_prevQtMessageHandler = qInstallMessageHandler(passthroughMessageHandler);
#endif
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
    if (m_opcUaThread && m_opcUaThread->isRunning()) {
        m_opcUaThread->quit();
        m_opcUaThread->wait();
    }
    m_opcUaService = nullptr;
    m_opcUaManager = nullptr;
}

/*!
 * \brief Initializes the OPC UA backend service once.
 * \return \c true when initialization was performed or had already been
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
