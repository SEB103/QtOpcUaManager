#ifndef OPCUASERVICE_H
#define OPCUASERVICE_H

#include <QHash>
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
#include "opcuavaluedata.h"
#include "opcuavaluetree.h"

class QOpcUaNode;

/**
 * Worker-thread OPC UA service behind OpcUaManager.
 */
class OpcUaService : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpcUaService)

public:
    /** Worker-service operation states mirrored to the GUI facade. */
    enum class OperationState {
        /** No asynchronous service operation is active. */
        Idle = 0,
        /** FindServers is active. */
        DiscoveringServers,
        /** GetEndpoints is active. */
        RequestingEndpoints,
        /** Endpoint connection is active. */
        Connecting,
        /** Session disconnection is active. */
        Disconnecting
    };
    Q_ENUM(OperationState)

    /** Creates the service with \a initialUrl as its default discovery URL. */
    explicit OpcUaService(const QString &initialUrl = QString(), QObject *parent = nullptr);

    /** Releases Qt OPC UA runtime objects in the service thread. */
    ~OpcUaService() override;

public slots:
    /** Initializes backend discovery, PKI paths, timers, and default authentication. */
    void initialize();
    /** Selects the Qt OPC UA backend named by \a backend. */
    void setBackend(const QString &backend);
    /** Configures anonymous authentication for future endpoint and connect requests. */
    void setAnonymousAuthentication();
    /** Configures username authentication with \a userName and \a password. */
    void setUsernameAuthentication(const QString &userName, const QString &password);
    /** Stores \a password for encrypted certificate private keys. */
    void setCertificatePrivateKeyPassword(const QString &password);
    /** Configures certificate authentication using the application PKI. */
    void setCertificateAuthentication();
    /** Enables or disables advertised endpoint URL rewriting. */
    void setEndpointUrlRewriteEnabled(bool enabled);
    /** Starts asynchronous server discovery for \a hostOrUrl. */
    void discoverServers(const QString &hostOrUrl);
    /** Starts asynchronous endpoint discovery for \a serverUrl. */
    void requestEndpoints(const QString &serverUrl);
    /** Starts endpoint discovery for the discovered server at \a serverIndex. */
    void requestEndpointsForServer(int serverIndex);
    /** Connects to the endpoint at \a endpointIndex, or disconnects if already connected. */
    void connectToEndpoint(int endpointIndex);
    /** Requests asynchronous disconnection from the current endpoint. */
    void disconnectFromServer();
    /** Browses direct children of \a parentNodeId for GUI model request \a requestId. */
    void browseChildren(const QString &parentNodeId, quint64 requestId);
    /** Reads the main attributes of \a nodeId for GUI request \a requestId. */
    void readNodeAttributes(const QString &nodeId, quint64 requestId);
    /** Reads and decodes the value of \a nodeId into a structured tree for GUI request \a requestId. */
    void readStructuredValue(const QString &nodeId, quint64 requestId);
    /** Starts value-attribute monitoring for \a nodeId. */
    void subscribeNode(const QString &nodeId);
    /** Stops value-attribute monitoring for \a nodeId. */
    void unsubscribeNode(const QString &nodeId);
    /** Writes \a value to the value attribute of \a nodeId. */
    void writeNodeValue(const QString &nodeId, const QVariant &value);

signals:
    /** Emitted when available backend plugin names change. */
    void availableBackendsChanged(const QStringList &backends);
    /** Emitted when the selected backend plugin changes. */
    void backendChanged(const QString &backend);
    /** Emitted when discovered server display rows change. */
    void serversChanged(const QStringList &servers);
    /** Emitted when endpoint display rows change. */
    void endpointsChanged(const QStringList &endpoints);
    /** Emitted when the session connection state changes. */
    void connectedChanged(bool connected);
    /** Emitted when the last user-visible error text changes. */
    void lastErrorChanged(const QString &lastError);
    /** Emitted when the active authentication token mode changes. */
    void authModeChanged(int authMode);
    /** Emitted when the current service operation state changes. */
    void operationStateChanged(int operationState);
    /** Emitted when the underlying QOpcUaClient state changes. */
    void clientStateChanged(int clientState);
    /** Emitted when endpoint URL rewriting state changes. */
    void endpointUrlRewriteEnabledChanged(bool enabled);
    /** Emitted when browse children for \a parentNodeId and \a requestId are ready. */
    void browseChildrenReady(const QString &parentNodeId,
                             quint64 requestId,
                             const QList<OpcUaNodeData> &children,
                             bool success);
    /** Emitted when the attributes requested for \a requestId are ready. */
    void nodeAttributesReady(quint64 requestId, const OpcUaAttributeData &data, bool success);
    /** Emitted when the decoded value tree for \a nodeId and \a requestId is ready. */
    void structuredValueReady(quint64 requestId,
                              const QString &nodeId,
                              const OpcUaValueTreeNode &root,
                              bool success);
    /** Emitted when a monitored node reports a new value. */
    void monitoredValueChanged(const OpcUaValueUpdate &update);
    /** Emitted when a write to \a nodeId finishes; \a error is set on failure. */
    void writeCompleted(const QString &nodeId, bool success, const QString &error);

private slots:
    /** Applies a FindServers result for \a requestUrl. */
    void findServersComplete(const QList<QOpcUaApplicationDescription> &servers,
                             QOpcUa::UaStatusCode statusCode,
                             const QUrl &requestUrl);
    /** Applies a GetEndpoints result for \a requestUrl. */
    void getEndpointsComplete(const QList<QOpcUaEndpointDescription> &endpoints,
                              QOpcUa::UaStatusCode statusCode,
                              const QUrl &requestUrl);
    /** Handles successful client connection. */
    void onClientConnected();
    /** Handles client disconnection and helper cleanup. */
    void onClientDisconnected();
    /** Mirrors \a state changes from QOpcUaClient. */
    void onClientStateChanged(QOpcUaClient::ClientState state);
    /** Converts \a error into user-visible text when it is not NoError. */
    void onClientErrorChanged(QOpcUaClient::ClientError error);
    /** Handles the temporary \a errorState emitted by QOpcUaClient::connectError. */
    void onConnectError(QOpcUaErrorState *errorState);
    /** Handles a FindServers timeout. */
    void handleFindServersTimeout();
    /** Handles a GetEndpoints timeout. */
    void handleEndpointsTimeout();
    /** Initializes generic-structure support after the namespace array is available. */
    void namespacesArrayUpdated(const QStringList &namespaceArray);
    /** Reports generic structure handler initialization state. */
    void handleGenericStructHandlerInitFinished(bool success);

private:
    /** Normalizes \a hostOrUrl into an OPC UA discovery URL with a default port. */
    static QUrl normalizeDiscoveryUrl(const QString &hostOrUrl);
    /** Creates and configures the runtime PKI directory layout. */
    void setupPkiConfiguration();
    /** Validates certificate authentication files and writes an optional \a errorMessage. */
    bool validateCertificateConfiguration(QString *errorMessage) const;
    /** Builds the OPC UA application identity for the selected authentication mode. */
    QOpcUaApplicationIdentity buildApplicationIdentity() const;
    /** Stores and emits the latest user-visible error text \a err. */
    void setLastError(const QString &err);
    /** Updates and emits the current service operation \a state. */
    void setOperationState(OperationState state);
    /** Returns the discovery URL for the discovered server at \a index. */
    QString serverUrlAt(int index) const;
    /** Applies the current OPC UA application identity to the active client. */
    bool applyOpcUaIdentity();
    /** Applies authentication and PKI settings to the active client. */
    bool applyClientSecurityConfiguration();
    /** Queues client security configuration in the QOpcUaClient thread when needed. */
    bool queueClientSecurityConfiguration();
    /** Handles whether FindServers was dispatched for \a url. */
    void handleFindServersDispatchResult(bool dispatched, const QUrl &url);
    /** Handles whether GetEndpoints was dispatched for \a url. */
    void handleEndpointsDispatchResult(bool dispatched, const QUrl &url);
    /** Returns whether \a ep supports the currently selected authentication mode. */
    bool endpointSupportsAuth(const QOpcUaEndpointDescription &ep) const;
    /** Returns whether \a ep uses no message security. */
    static bool endpointIsNoSecurity(const QOpcUaEndpointDescription &ep);
    /** Chooses the preferred endpoint from \a endpoints for the current auth mode. */
    QOpcUaEndpointDescription chooseEndpoint(const QList<QOpcUaEndpointDescription> &endpoints) const;
    /** Formats supported authentication modes for \a ep. */
    static QString endpointAuthModesToString(const QOpcUaEndpointDescription &ep);
    /** Rewrites \a ep to use the host and port from \a discoveryUrl when enabled. */
    QOpcUaEndpointDescription patchEndpointForDiscoveryUrl(const QOpcUaEndpointDescription &ep,
                                                           const QUrl &discoveryUrl) const;
    /** Invalidates cached endpoint results and selection metadata. */
    void invalidateEndpointCache();
    /** Creates and connects the QOpcUaClient if no client exists. */
    void createClient();
    /** Returns whether the current call runs in this service object's thread. */
    bool isInObjectThread() const;
    /** Stops all active monitored-node subscriptions and releases their nodes. */
    void clearMonitoredNodes();
    /** Builds a value-attribute update snapshot for \a nodeId from \a node. */
    OpcUaValueUpdate buildValueUpdate(const QString &nodeId, QOpcUaNode *node) const;
    /**
     * Recursively decodes \a value into a value tree node named \a name.
     * \a dataTypeId and \a valueRank are the field's DataType node id and
     * ValueRank when known; they guide array and structure detection.
     */
    OpcUaValueTreeNode buildValueTree(const QString &name,
                                      const QVariant &value,
                                      const QString &dataTypeId,
                                      int valueRank) const;

    /** Whether initialize() has completed successfully. */
    bool m_initialized {false};
    /** Current session connection state. */
    bool m_clientConnected {false};
    /** Default discovery URL used when the UI submits an empty discovery input. */
    QString m_initialUrl;
    /** Last user-visible OPC UA error text. */
    QString m_lastError;
    /** Provider used to enumerate and create Qt OPC UA backend clients. */
    QOpcUaProvider *m_provider {nullptr};
    /** Active Qt OPC UA client owned by this service. */
    QOpcUaClient *m_client {nullptr};
    /** Available Qt OPC UA backend plugin names. */
    QStringList m_availableBackends;
    /** Selected Qt OPC UA backend plugin name. */
    QString m_backend;
    /** Backend-specific properties passed to QOpcUaProvider::createClient(). */
    QVariantMap m_backendProperties;
    /** Display rows for discovered servers. */
    QStringList m_servers;
    /** Display rows for discovered endpoints. */
    QStringList m_endpointsDisplay;
    /** Endpoint descriptions matching \c m_endpointsDisplay. */
    QList<QOpcUaEndpointDescription> m_endpointList;
    /** Raw server descriptions received from FindServers. */
    QList<QOpcUaApplicationDescription> m_serverDescriptions;
    /** Preferred discovery URLs matching \c m_serverDescriptions. */
    QStringList m_serverCandidateUrls;
    /** Last FindServers request URL used to reject stale callbacks. */
    QUrl m_lastFindServersRequestUrl;
    /** Last GetEndpoints request URL used to reject stale callbacks. */
    QUrl m_lastEndpointsRequestUrl;
    /** Optional helper for custom OPC UA structured types after connection. */
    QScopedPointer<QOpcUaGenericStructHandler> m_genericStructHandler;
    /** Application identity applied to the active client. */
    QOpcUaApplicationIdentity m_identity;
    /** Runtime PKI configuration for certificate validation and authentication. */
    QOpcUaPkiConfiguration m_pkiConfig;
    /** Authentication information applied to endpoint and connect requests. */
    QOpcUaAuthenticationInformation m_authInfo;
    /** Writable application PKI base directory. */
    QString m_pkiBaseDirectory;
    /** Password used when the configured certificate private key is encrypted. */
    QString m_certificatePrivateKeyPassword;
    /** Whether \c m_identity has been applied to the active client. */
    bool m_identityApplied {false};
    /** Whether endpoint cache metadata currently represents a valid result. */
    bool m_endpointCacheValid {false};
    /** Whether endpoint URLs should be patched to match the discovery URL. */
    bool m_endpointUrlRewriteEnabled {false};
    /** Discovery URL associated with the cached endpoints. */
    QUrl m_endpointCacheDiscoveryUrl;
    /** Authentication mode associated with the cached endpoints. */
    QOpcUaUserTokenPolicy::TokenType m_endpointCacheAuthType {
        QOpcUaUserTokenPolicy::TokenType::Anonymous
    };
    /** Cached raw endpoint descriptions. */
    QList<QOpcUaEndpointDescription> m_cachedEndpoints;
    /** Timeout timer for FindServers requests. */
    QTimer *m_findServersTimeoutTimer {nullptr};
    /** Timeout timer for GetEndpoints requests. */
    QTimer *m_endpointsTimeoutTimer {nullptr};
    /** Whether a FindServers request is waiting for a result. */
    bool m_findServersRequestPending {false};
    /** Whether a GetEndpoints request is waiting for a result. */
    bool m_endpointsRequestPending {false};
    /** Current service operation state. */
    OperationState m_operationState {OperationState::Idle};
    /** FindServers timeout in milliseconds. */
    int m_findServersRequestTimeoutMs {5000};
    /** GetEndpoints timeout in milliseconds. */
    int m_endpointsRequestTimeoutMs {5000};
    /** Active monitored nodes keyed by node id; owned and kept alive while monitoring. */
    QHash<QString, QOpcUaNode *> m_monitoredNodes;
    /** Publishing/sampling interval applied to new monitored items, in milliseconds. */
    double m_monitoringIntervalMs {250.0};
};

#endif // OPCUASERVICE_H
