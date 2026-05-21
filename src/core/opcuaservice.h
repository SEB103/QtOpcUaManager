#ifndef OPCUASERVICE_H
#define OPCUASERVICE_H

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QScopedPointer>
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
#include <QTimer>

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
    explicit OpcUaService(const QString &initialUrl = QString(), QObject *parent = nullptr);
    ~OpcUaService() override;

public slots:
    void initialize();
    void setBackend(const QString &backend);
    void setAnonymousAuthentication();
    void setUsernameAuthentication(const QString &userName, const QString &password);
    void setCertificateAuthentication();
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
    void browseChildrenReady(const QString &parentNodeId, const QList<OpcUaNodeData> &children);

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
    void namespacesArrayUpdated(const QStringList &namespaceArray);
    void handleGenericStructHandlerInitFinished(bool success);

private:
    static QUrl normalizeDiscoveryUrl(const QString &hostOrUrl);
    void setupPkiConfiguration();
    void setLastError(const QString &err);
    QString serverUrlAt(int index) const;
    void applyOpcUaIdentity();
    void applyClientSecurityConfiguration();
    void queueClientSecurityConfiguration();
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
    bool m_identityApplied {false};
    bool m_endpointCacheValid {false};
    QUrl m_endpointCacheDiscoveryUrl;
    QOpcUaUserTokenPolicy::TokenType m_endpointCacheAuthType {QOpcUaUserTokenPolicy::TokenType::Anonymous};
    QOpcUaEndpointDescription m_cachedEndpoint;
    QTimer *m_findServersTimeoutTimer {nullptr};
    QTimer *m_endpointsTimeoutTimer {nullptr};
    bool m_findServersRequestPending {false};
    int m_findServersRequestTimeoutMs {5000};
    int m_endpointsRequestTimeoutMs {5000};
};

#endif // OPCUASERVICE_H
