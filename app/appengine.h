#ifndef APPENGINE_H
#define APPENGINE_H

#include <QQmlApplicationEngine>
#include <QString>

#include "models/logfiltermodel.h"

class AppInfo;
class LicenseModel;
class LogModel;
class OpcUaManager;
class OpcUaService;
class ProjectManager;
class ServerStudio;
class QThread;

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

/**
 * QML engine wrapper that owns application-level C++ services.
 */
class AppEngine : public QQmlApplicationEngine
{
    Q_OBJECT
    Q_DISABLE_COPY(AppEngine)

    /** Filtered application log shown by the log panel; owned by this engine. */
    Q_PROPERTY(LogFilterModel *logModel READ logModel CONSTANT)

public:
    /** Creates the engine for \a initialUrl and exposes application services to QML. */
    explicit AppEngine(const QString& initialUrl, QObject* parent = nullptr);

    /** Stops the OPC UA worker thread and releases application-level service references. */
    ~AppEngine() override;

    /** Starts asynchronous creation and initialization of the OPC UA backend once. */
    bool startOpcUaBackend();

    /** Injects the INI \a settings store into the OPC UA facade for persistence. */
    void setSettings(QSettings* settings);

    /** Returns the filtered application log exposed to QML. */
    LogFilterModel* logModel() const { return m_logFilterModel; }

    /** Removes every entry from the in-memory application log. */
    Q_INVOKABLE void clearLog();

    /**
     * Opens the directory holding the log file in the system file manager.
     * \return \c false when no log file has been written in this session.
     */
    Q_INVOKABLE bool showLogFileLocation();

private:
    /** Creates the worker-thread OPC UA service and connects it to the QML facade. */
    void createOpcUaRuntime();

    /** Initial discovery URL passed from the command line or the default startup value. */
    QString m_initialUrl;

    /** Worker thread that owns OpcUaService and Qt OPC UA runtime objects. */
    QThread* m_opcUaThread = nullptr;

    /** GUI-thread facade exposed to QML as \c cppManagerOpcUa; owned by this engine. */
    OpcUaManager* m_opcUaManager = nullptr;

    /** Project facade exposed to QML as \c cppProjectManager; owned by this engine. */
    ProjectManager* m_projectManager = nullptr;

    /** Server Studio facade exposed to QML as \c cppServerStudio; owned by this engine. */
    ServerStudio* m_serverStudio = nullptr;

    /** Application/build metadata exposed to QML as \c cppAppInfo; owned by this engine. */
    AppInfo* m_appInfo = nullptr;

    /** Bundled license documents exposed to QML as \c cppLicenseModel; owned by this engine. */
    LicenseModel* m_licenseModel = nullptr;

    /** Worker-thread backend service; deleted through the worker thread shutdown path. */
    OpcUaService* m_opcUaService = nullptr;

    /** In-memory application log fed by the installed Qt message handler. */
    LogModel* m_logModel = nullptr;

    /** Severity and text filter over m_logModel, exposed to QML. */
    LogFilterModel* m_logFilterModel = nullptr;

    /** Tracks whether backend startup was already requested. */
    bool m_opcUaBackendStartRequested = false;
};

#endif // APPENGINE_H
