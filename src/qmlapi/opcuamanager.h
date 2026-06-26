#ifndef OPCUAMANAGER_H
#define OPCUAMANAGER_H

#include <QObject>
#include <QStringList>
#include <QMutex>

#include "models/opcuamodel.h"
#include "core/opcuanodedata.h"

class OpcUaService;

/*!
 * \class OpcUaManager
 *
 * GUI-thread facade for the OPC UA backend.
 *
 * \details
 * This class is the QML-facing layer. It owns UI state, exposes Q_PROPERTY
 * values, forwards commands to the OPC UA service and applies service results
 * in the GUI thread.
 */
class OpcUaManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpcUaManager)

    Q_PROPERTY(QStringList opcUaBackend READ opcUaBackend NOTIFY opcUaBackendChanged)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QStringList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(QStringList endpoints READ endpoints NOTIFY endpointsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(OpcUaModel *treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int authMode READ authMode NOTIFY authModeChanged)

public:
    /*! Creates the GUI-thread OPC UA facade. */
    explicit OpcUaManager(const QString &initialUrl = QString(), QObject *parent = nullptr);

    /*! Destroys the facade. */
    ~OpcUaManager() override;

    /*! Returns the available Qt OPC UA backends. */
    QStringList opcUaBackend() const;

    /*! Returns the selected backend name. */
    QString backend() const;

    /*! Returns the current server list. */
    QStringList servers() const;

    /*! Returns the current endpoint list. */
    QStringList endpoints() const;

    /*! Returns whether the OPC UA service reports an active session. */
    bool connected() const;

    /*! Returns the GUI-thread browse model. */
    OpcUaModel *treeModel() const;

    /*! Returns the last error text. */
    QString lastError() const;

    /*! Returns the current authentication mode. */
    int authMode() const;

    /*! Selects the active Qt OPC UA backend. */
    void setBackend(const QString &backend);

    /*! Selects anonymous authentication. */
    Q_INVOKABLE void setAnonymousAuthentication();

    /*! Selects username/password authentication. */
    Q_INVOKABLE void setUsernameAuthentication(const QString &userName, const QString &password);

    /*! Selects certificate authentication. */
    Q_INVOKABLE void setCertificateAuthentication();

    /*! Starts a FindServers request. */
    Q_INVOKABLE void discoverServers(const QString &hostOrUrl);

    /*! Starts a GetEndpoints request. */
    Q_INVOKABLE void requestEndpoints(const QString &serverUrl);

    /*! Requests endpoints for the selected server. */
    Q_INVOKABLE void requestEndpointsForServer(int serverIndex);

    /*! Connects to the selected endpoint. */
    Q_INVOKABLE void connectToEndpoint(int endpointIndex);

    /*! Disconnects from the current server. */
    Q_INVOKABLE void disconnectFromServer();

    /*! Attaches the OPC UA service signal/slot bridge. */
    void attachService(OpcUaService *service);

signals:
    void opcUaBackendChanged();
    void backendChanged();
    void serversChanged();
    void endpointsChanged();
    void connectedChanged();
    void lastErrorChanged();
    void authModeChanged();

    void initializeRequested();
    void setBackendRequested(const QString &backend);
    void setAnonymousAuthenticationRequested();
    void setUsernameAuthenticationRequested(const QString &userName, const QString &password);
    void setCertificateAuthenticationRequested();
    void discoverServersRequested(const QString &hostOrUrl);
    void requestEndpointsRequested(const QString &serverUrl);
    void requestEndpointsForServerRequested(int serverIndex);
    void connectToEndpointRequested(int endpointIndex);
    void disconnectFromServerRequested();
    void browseChildrenRequested(const QString &parentNodeId);

public slots:
    void applyAvailableBackends(const QStringList &backends);
    void applyBackend(const QString &backend);
    void applyServers(const QStringList &servers);
    void applyEndpoints(const QStringList &endpoints);
    void applyConnected(bool connected);
    void applyLastError(const QString &lastError);
    void applyAuthMode(int authMode);
    void applyBrowseChildren(const QString &parentNodeId, const QList<OpcUaNodeData> &children);

private:
    mutable QMutex m_stateMutex;
    QString m_initialUrl;
    QStringList m_availableBackends;
    QString m_backend;
    QStringList m_servers;
    QStringList m_endpoints;
    bool m_connected {false};
    QString m_lastError;
    int m_authMode {0};
    OpcUaModel *m_treeModel {nullptr};
    OpcUaService *m_service {nullptr};
};

#endif // OPCUAMANAGER_H
