#ifndef APPENGINE_H
#define APPENGINE_H

#include <QQmlApplicationEngine>
#include <QString>

class OpcUaManager;
class OpcUaService;
class QThread;

/*!
 * \class AppEngine
 * \brief QML engine wrapper that owns application-level C++ services.
 */
class AppEngine : public QQmlApplicationEngine
{
    Q_OBJECT
    Q_DISABLE_COPY(AppEngine)
public:
    /*! Creates the engine for \a initialUrl and exposes application services to QML. */
    explicit AppEngine(const QString& initialUrl, QObject* parent = nullptr);

    /*! Stops the OPC UA worker thread and releases application-level service references. */
    ~AppEngine() override;

    /*! Starts asynchronous creation and initialization of the OPC UA backend once. */
    bool startOpcUaBackend();

private:
    /*! Creates the worker-thread OPC UA service and connects it to the QML facade. */
    void createOpcUaRuntime();

    /*! Initial discovery URL passed from the command line or the default startup value. */
    QString m_initialUrl;

    /*! Worker thread that owns OpcUaService and Qt OPC UA runtime objects. */
    QThread* m_opcUaThread = nullptr;

    /*! GUI-thread facade exposed to QML as \c cppManagerOpcUa; owned by this engine. */
    OpcUaManager* m_opcUaManager = nullptr;

    /*! Worker-thread backend service; deleted through the worker thread shutdown path. */
    OpcUaService* m_opcUaService = nullptr;

    /*! Tracks whether backend startup was already requested. */
    bool m_opcUaBackendStartRequested = false;
};

#endif // APPENGINE_H
