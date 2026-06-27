#ifndef OPCUAMANAGER_H
#define OPCUAMANAGER_H

#include <QMutex>
#include <QObject>
#include <QStringList>

#include "core/opcuanodedata.h"
#include "models/opcuamodel.h"

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
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int operationState READ operationState NOTIFY operationStateChanged)
    Q_PROPERTY(int clientState READ clientState NOTIFY clientStateChanged)
    Q_PROPERTY(bool endpointUrlRewriteEnabled READ endpointUrlRewriteEnabled
                   WRITE setEndpointUrlRewriteEnabled
                   NOTIFY endpointUrlRewriteEnabledChanged)
    Q_PROPERTY(OpcUaModel *treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int authMode READ authMode NOTIFY authModeChanged)

public:
    enum OperationState {
        OperationIdle = 0,
        OperationDiscoveringServers,
        OperationRequestingEndpoints,
        OperationConnecting,
        OperationDisconnecting
    };
    Q_ENUM(OperationState)

    enum ClientState {
        ClientDisconnected = 0,
        ClientConnecting,
        ClientConnected,
        ClientClosing
    };
    Q_ENUM(ClientState)

    explicit OpcUaManager(const QString &initialUrl = QString(), QObject *parent = nullptr);
    ~OpcUaManager() override;

    QStringList opcUaBackend() const;
    QString backend() const;
    QStringList servers() const;
    QStringList endpoints() const;
    bool connected() const;
    bool busy() const;
    int operationState() const;
    int clientState() const;
    bool endpointUrlRewriteEnabled() const;
    OpcUaModel *treeModel() const;
    QString lastError() const;
    int authMode() const;

    void setBackend(const QString &backend);
    void setEndpointUrlRewriteEnabled(bool enabled);

    Q_INVOKABLE void setAnonymousAuthentication();
    Q_INVOKABLE void setUsernameAuthentication(const QString &userName, const QString &password);
    Q_INVOKABLE void setCertificatePrivateKeyPassword(const QString &password);
    Q_INVOKABLE void setCertificateAuthentication();
    Q_INVOKABLE void discoverServers(const QString &hostOrUrl);
    Q_INVOKABLE void requestEndpoints(const QString &serverUrl);
    Q_INVOKABLE void requestEndpointsForServer(int serverIndex);
    Q_INVOKABLE void connectToEndpoint(int endpointIndex);
    Q_INVOKABLE void disconnectFromServer();

    void attachService(OpcUaService *service);

signals:
    void opcUaBackendChanged();
    void backendChanged();
    void serversChanged();
    void endpointsChanged();
    void connectedChanged();
    void busyChanged();
    void operationStateChanged();
    void clientStateChanged();
    void endpointUrlRewriteEnabledChanged();
    void lastErrorChanged();
    void authModeChanged();

    void initializeRequested();
    void setBackendRequested(const QString &backend);
    void setAnonymousAuthenticationRequested();
    void setUsernameAuthenticationRequested(const QString &userName, const QString &password);
    void setCertificatePrivateKeyPasswordRequested(const QString &password);
    void setCertificateAuthenticationRequested();
    void setEndpointUrlRewriteEnabledRequested(bool enabled);
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
    void applyOperationState(int operationState);
    void applyClientState(int clientState);
    void applyEndpointUrlRewriteEnabled(bool enabled);
    void applyLastError(const QString &lastError);
    void applyAuthMode(int authMode);
    void applyBrowseChildren(const QString &parentNodeId,
                             const QList<OpcUaNodeData> &children,
                             bool success);

private:
    mutable QMutex m_stateMutex;
    QString m_initialUrl;
    QStringList m_availableBackends;
    QString m_backend;
    QStringList m_servers;
    QStringList m_endpoints;
    bool m_connected {false};
    int m_operationState {OperationIdle};
    int m_clientState {ClientDisconnected};
    bool m_endpointUrlRewriteEnabled {false};
    QString m_lastError;
    int m_authMode {0};
    OpcUaModel *m_treeModel {nullptr};
    OpcUaService *m_service {nullptr};
};

#endif // OPCUAMANAGER_H
