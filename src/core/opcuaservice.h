#ifndef OPCUASERVICE_H
#define OPCUASERVICE_H

#include <QObject>
#include <QList>
#include <QOpcUaApplicationDescription>
#include <QOpcUaApplicationIdentity>
#include <QOpcUaAuthenticationInformation>
#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaErrorState>
#include <QOpcUaGenericStructHandler>
#include <QOpcUaPkiConfiguration>
#include <QOpcUaProvider>
#include <QOpcUaUserTokenPolicy>
#include <QScopedPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "opcuanodedata.h"

/*!
 * \class OpcUaService
 * \brief Worker-thread OPC UA service behind OpcUaManager.
 */
class OpcUaService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpcUaService)

public:
    enum class OperationState {
        Idle = 0,
        DiscoveringServers,
        RequestingEndpoints,
        Connecting,
        Disconnecting
    };
    Q_ENUM(OperationState)

    explicit OpcUaService(const QString &initialUrl = QString(), QObject *parent = nullptr);
    ~OpcUaService() override;

public slots:
    void initialize();
    void setBackend(const QString &backend);
    void setAnonymousAuthentication();
    void setUsernameAuthentication(const QString &userName, const QString &password);
    void setCertificatePrivateKeyPassword(const QString &password);
    void setCertificateAuthentication();
    void setEndpointUrlRewriteEnabled(bool enabled);
    void discoverServers(const QString &hostOrUrl);
    void requestEndpoints(const QString &serverUrl);
    void requestEndpointsForServer(int serverIndex);
    void connectToEndpoint(int endpointIndex);
    void disconnectFromServer();
    void browseChildren(const QString &parentNodeId);

signals:
    void availableBackendsChanged(const QStringList &backends);
    void backendChanged(const QString &backend);
    void serversChanged(const QStringList &servers);
    void endpointsChanged(const QStringList &endpoints);
    void connectedChanged(bool connected);
    void lastErrorChanged(const QString &lastError);
    void authModeChanged(int authMode);
    void operationStateChanged(int operationState);
    void clientStateChanged(int clientState);
    void endpointUrlRewriteEnabledChanged(bool enabled);
    void browseChildrenReady(const QString &parentNodeId,
                             const QList<OpcUaNodeData> &children,
                             bool success);

private slots:
    void findServersComplete(const QList<QOpcUaApplicationDescription> &servers,
                             QOpcUa::UaStatusCode statusCode,
                             const QUrl &requestUrl);
    void getEndpointsComplete(const QList<QOpcUaEndpointDescription> &endpoints,
                              QOpcUa::UaStatusCode statusCode,
                              const QUrl &requestUrl);
    void onClientConnected();
    void onClientDisconnected();
    void onClientStateChanged(QOpcUaClient::ClientState state);
    void onClientErrorChanged(QOpcUaClient::ClientError error);
    void onConnectError(QOpcUaErrorState *errorState);
    void handleFindServersTimeout();
    void handleEndpointsTimeout();
    void namespacesArrayUpdated(const QStringList &namespaceArray);
    void handleGenericStructHandlerInitFinished(bool success);

private:
    static QUrl normalizeDiscoveryUrl(const QString &hostOrUrl);
    void setupPkiConfiguration();
    bool validateCertificateConfiguration(QString *errorMessage) const;
    QOpcUaApplicationIdentity buildApplicationIdentity() const;
    void setLastError(const QString &err);
    void setOperationState(OperationState state);
    QString serverUrlAt(int index) const;
    bool applyOpcUaIdentity();
    bool applyClientSecurityConfiguration();
    bool queueClientSecurityConfiguration();
    void handleFindServersDispatchResult(bool dispatched, const QUrl &url);
    void handleEndpointsDispatchResult(bool dispatched, const QUrl &url);
    bool endpointSupportsAuth(const QOpcUaEndpointDescription &ep) const;
    static bool endpointIsNoSecurity(const QOpcUaEndpointDescription &ep);
    QOpcUaEndpointDescription chooseEndpoint(const QList<QOpcUaEndpointDescription> &endpoints) const;
    static QString endpointAuthModesToString(const QOpcUaEndpointDescription &ep);
    QOpcUaEndpointDescription patchEndpointForDiscoveryUrl(const QOpcUaEndpointDescription &ep,
                                                           const QUrl &discoveryUrl) const;
    void invalidateEndpointCache();
    void createClient();
    bool isInObjectThread() const;

    bool m_initialized {false};
    bool m_clientConnected {false};
    QString m_initialUrl;
    QString m_lastError;
    QOpcUaProvider *m_provider {nullptr};
    QOpcUaClient *m_client {nullptr};
    QStringList m_availableBackends;
    QString m_backend;
    QVariantMap m_backendProperties;
    QStringList m_servers;
    QStringList m_endpointsDisplay;
    QList<QOpcUaEndpointDescription> m_endpointList;
    QList<QOpcUaApplicationDescription> m_serverDescriptions;
    QStringList m_serverCandidateUrls;
    QUrl m_lastFindServersRequestUrl;
    QUrl m_lastEndpointsRequestUrl;
    QScopedPointer<QOpcUaGenericStructHandler> m_genericStructHandler;
    QOpcUaApplicationIdentity m_identity;
    QOpcUaPkiConfiguration m_pkiConfig;
    QOpcUaAuthenticationInformation m_authInfo;
    QString m_pkiBaseDirectory;
    QString m_certificatePrivateKeyPassword;
    bool m_identityApplied {false};
    bool m_endpointCacheValid {false};
    bool m_endpointUrlRewriteEnabled {false};
    QUrl m_endpointCacheDiscoveryUrl;
    QOpcUaUserTokenPolicy::TokenType m_endpointCacheAuthType {
        QOpcUaUserTokenPolicy::TokenType::Anonymous
    };
    QList<QOpcUaEndpointDescription> m_cachedEndpoints;
    QTimer *m_findServersTimeoutTimer {nullptr};
    QTimer *m_endpointsTimeoutTimer {nullptr};
    bool m_findServersRequestPending {false};
    bool m_endpointsRequestPending {false};
    OperationState m_operationState {OperationState::Idle};
    int m_findServersRequestTimeoutMs {5000};
    int m_endpointsRequestTimeoutMs {5000};
};

#endif // OPCUASERVICE_H
