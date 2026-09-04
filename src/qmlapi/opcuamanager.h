#ifndef OPCUAMANAGER_H
#define OPCUAMANAGER_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <memory>

#include "core/opcuanodedata.h"
#include "core/opcuavaluedata.h"
#include "core/opcuavaluetree.h"
#include "models/attributesmodel.h"
#include "models/dataaccessmodel.h"
#include "models/opcuamodel.h"
#include "persistence/nodedatabase.h"
#include "project/projectdata.h"

class OpcUaService;
class QQuickTextDocument;
class StructuredValueHighlighter;

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

/**
 * GUI-thread facade for the OPC UA backend.
 *
 * This class is the QML-facing layer. It owns UI state, exposes Q_PROPERTY
 * values, forwards commands to the OPC UA service and applies service results
 * in the GUI thread.
 */
class OpcUaManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(OpcUaManager)

    /** Available Qt OPC UA backend plugin names. */
    Q_PROPERTY(QStringList opcUaBackend READ opcUaBackend NOTIFY opcUaBackendChanged)

    /** Currently selected Qt OPC UA backend plugin name. */
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)

    /** Display rows for discovered OPC UA servers. */
    Q_PROPERTY(QStringList servers READ servers NOTIFY serversChanged)

    /** Display rows for endpoints returned by the selected server. */
    Q_PROPERTY(QStringList endpoints READ endpoints NOTIFY endpointsChanged)

    /** Whether an OPC UA session is currently connected. */
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

    /** Whether discovery, endpoint lookup, connect, or disconnect is active. */
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    /** Current operation state as an OpcUaManager::OperationState integer. */
    Q_PROPERTY(int operationState READ operationState NOTIFY operationStateChanged)

    /** Current QOpcUaClient state as an OpcUaManager::ClientState integer. */
    Q_PROPERTY(int clientState READ clientState NOTIFY clientStateChanged)

    /** Whether advertised endpoint URLs are rewritten to the discovery host and port. */
    Q_PROPERTY(bool endpointUrlRewriteEnabled READ endpointUrlRewriteEnabled
                   WRITE setEndpointUrlRewriteEnabled
                   NOTIFY endpointUrlRewriteEnabledChanged)

    /** Tree model exposed to QML for address-space browsing; owned by this manager. */
    Q_PROPERTY(OpcUaModel *treeModel READ treeModel CONSTANT)

    /** Focus-segment tree model rooted at the pinned node; owned by this manager. */
    Q_PROPERTY(OpcUaModel *focusModel READ focusModel CONSTANT)

    /** Node id of the pinned focus node; empty when no node is pinned. */
    Q_PROPERTY(QString focusNodeId READ focusNodeId NOTIFY focusNodeChanged)

    /** Display name of the pinned focus node, used as the focus segment title. */
    Q_PROPERTY(QString focusNodeName READ focusNodeName NOTIFY focusNodeChanged)

    /** Whether a previously used connection is stored and can be reconnected to. */
    Q_PROPERTY(bool hasLastConnection READ hasLastConnection NOTIFY hasLastConnectionChanged)

    /** Data Access View table model exposed to QML; owned by this manager. */
    Q_PROPERTY(DataAccessModel *dataModel READ dataModel CONSTANT)

    /** Attributes panel model exposed to QML; owned by this manager. */
    Q_PROPERTY(AttributesModel *attributesModel READ attributesModel CONSTANT)

    /** Last user-visible OPC UA error text; empty when no error is active. */
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    /** Current QOpcUaUserTokenPolicy::TokenType as an integer for QML controls. */
    Q_PROPERTY(int authMode READ authMode NOTIFY authModeChanged)

    /** Output format used to render the structured value of the selected node. */
    Q_PROPERTY(ValueFormat valueFormat READ valueFormat WRITE setValueFormat
                   NOTIFY valueFormatChanged)

    /** Formatted structured value text (JSON or XML) for the selected node. */
    Q_PROPERTY(QString structuredValueText READ structuredValueText
                   NOTIFY structuredValueChanged)

    /** Whether the selected node exposes a renderable structured or scalar value. */
    Q_PROPERTY(bool structuredValueAvailable READ structuredValueAvailable
                   NOTIFY structuredValueChanged)

    /** Node id of the currently selected node, shared by all panels for highlighting. */
    Q_PROPERTY(QString selectedNodeId READ selectedNodeId NOTIFY selectedNodeIdChanged)

public:
    /** Operation state values exposed to QML. */
    enum OperationState {
        /** No asynchronous user operation is active. */
        OperationIdle = 0,
        /** FindServers is active. */
        OperationDiscoveringServers,
        /** GetEndpoints is active. */
        OperationRequestingEndpoints,
        /** Endpoint connection is active. */
        OperationConnecting,
        /** Session disconnection is active. */
        OperationDisconnecting
    };
    Q_ENUM(OperationState)

    /** Client session state values exposed to QML. */
    enum ClientState {
        /** The OPC UA client is disconnected. */
        ClientDisconnected = 0,
        /** The OPC UA client is connecting. */
        ClientConnecting,
        /** The OPC UA client has an active session. */
        ClientConnected,
        /** The OPC UA client is closing the session. */
        ClientClosing
    };
    Q_ENUM(ClientState)

    /** Structured-value output format selectable from the View menu. */
    enum ValueFormat {
        /** Render structured values as JSON. */
        FormatJson = 0,
        /** Render structured values as XML. */
        FormatXml
    };
    Q_ENUM(ValueFormat)

    /** Creates a GUI-thread facade using \a initialUrl as the default discovery URL. */
    explicit OpcUaManager(const QString &initialUrl = QString(), QObject *parent = nullptr);

    /** Destroys the facade and its owned tree model. */
    ~OpcUaManager() override;

    /** Returns available backend plugin names. */
    QStringList opcUaBackend() const;

    /** Returns the currently selected backend plugin name. */
    QString backend() const;

    /** Returns discovered server display rows. */
    QStringList servers() const;

    /** Returns discovered endpoint display rows. */
    QStringList endpoints() const;

    /** Returns whether the service reports an active connection. */
    bool connected() const;

    /** Returns whether the current operation state is not OperationIdle. */
    bool busy() const;

    /** Returns the current OperationState value as an integer. */
    int operationState() const;

    /** Returns the current ClientState value as an integer. */
    int clientState() const;

    /** Returns whether endpoint URL rewriting is enabled. */
    bool endpointUrlRewriteEnabled() const;

    /** Returns the owned address-space tree model exposed to QML. */
    OpcUaModel *treeModel() const;

    /** Returns the owned focus-segment tree model exposed to QML. */
    OpcUaModel *focusModel() const;

    /** Returns the node id of the pinned focus node, or an empty string. */
    QString focusNodeId() const;

    /** Returns the display name of the pinned focus node. */
    QString focusNodeName() const;

    /** Returns whether a stored connection is available for reconnection. */
    bool hasLastConnection() const;

    /**
     * Injects the INI settings store used to persist the last connection and the
     * pinned focus node. Ownership stays with the caller; the pointer may be null,
     * in which case persistence is disabled.
     */
    void setSettings(QSettings *settings);

    /** Returns the owned Data Access View table model exposed to QML. */
    DataAccessModel *dataModel() const;

    /** Returns the owned Attributes panel model exposed to QML. */
    AttributesModel *attributesModel() const;

    /** Returns the last user-visible error text. */
    QString lastError() const;

    /** Returns the current authentication token mode as an integer. */
    int authMode() const;

    /** Returns the current structured-value output format. */
    ValueFormat valueFormat() const;

    /** Returns the formatted structured value text for the selected node. */
    QString structuredValueText() const;

    /** Returns whether a renderable structured value is available for the panel. */
    bool structuredValueAvailable() const;

    /** Returns the node id of the currently selected node. */
    QString selectedNodeId() const;

    /** Sets the structured-value output \a format and re-renders the cached value. */
    void setValueFormat(ValueFormat format);

    /** Requests backend selection on the worker service. */
    void setBackend(const QString &backend);

    /** Requests enabling or disabling endpoint URL rewriting on the worker service. */
    void setEndpointUrlRewriteEnabled(bool enabled);

    /** Requests anonymous authentication for future endpoint and connect operations. */
    Q_INVOKABLE void setAnonymousAuthentication();

    /** Requests username authentication with \a userName and \a password. */
    Q_INVOKABLE void setUsernameAuthentication(const QString &userName, const QString &password);

    /** Stores \a password for encrypted private keys used by certificate authentication. */
    Q_INVOKABLE void setCertificatePrivateKeyPassword(const QString &password);

    /** Requests certificate authentication using the configured application PKI. */
    Q_INVOKABLE void setCertificateAuthentication();

    /** Requests server discovery for \a hostOrUrl or the default URL when empty. */
    Q_INVOKABLE void discoverServers(const QString &hostOrUrl);

    /** Requests endpoint discovery for \a serverUrl. */
    Q_INVOKABLE void requestEndpoints(const QString &serverUrl);

    /** Requests endpoint discovery for the server at \a serverIndex. */
    Q_INVOKABLE void requestEndpointsForServer(int serverIndex);

    /** Requests connection to the endpoint at \a endpointIndex. */
    Q_INVOKABLE void connectToEndpoint(int endpointIndex);

    /** Requests disconnection from the current server. */
    Q_INVOKABLE void disconnectFromServer();

    /**
     * Pins the node at the tree \a treeIndex as the focus node, so the focus
     * segment shows only that node's subtree. The index may come from either the
     * main tree model or the focus model.
     */
    Q_INVOKABLE void setFocusNodeFromIndex(const QModelIndex &treeIndex);

    /** Clears the pinned focus node and empties the focus segment. */
    Q_INVOKABLE void clearFocusNode();

    /**
     * Reconnects to the stored last connection: selects the backend and
     * authentication mode, rediscovers servers and endpoints, and connects to the
     * matching endpoint. For username authentication passwordRequired() is emitted
     * first so QML can prompt for the password.
     */
    Q_INVOKABLE void connectToLast();

    /**
     * Supplies the \a password requested by passwordRequired() and resumes an
     * in-progress connectToLast() run that is waiting for username credentials.
     */
    Q_INVOKABLE void provideReconnectPassword(const QString &password);

    /**
     * Adds or removes the node at the tree \a treeIndex from the Data Access View
     * and the persistent database, and starts or stops its live subscription.
     */
    Q_INVOKABLE void setNodeMonitored(const QModelIndex &treeIndex, bool on);

    /** Removes the Data Access View row at \a row from the table and the database. */
    Q_INVOKABLE void removeNode(int row);

    /** Requests the attributes of the node at the tree \a treeIndex for the panel. */
    Q_INVOKABLE void requestAttributes(const QModelIndex &treeIndex);

    /** Selects the Data Access View row at \a row, driving the shared node selection. */
    Q_INVOKABLE void selectDataRow(int row);

    /** Re-reads and re-decodes the structured value of the last selected node. */
    Q_INVOKABLE void refreshStructuredValue();

    /**
     * Installs the structured-value syntax highlighter on \a document.
     *
     * The QML Value panel passes the TextArea's QQuickTextDocument so the
     * highlighter attaches to its underlying QTextDocument. The call is a no-op
     * when \a document is null or a highlighter is already installed.
     */
    Q_INVOKABLE void installStructuredValueHighlighter(QQuickTextDocument *document);

    /**
     * Selects the dark (\a dark true) or light color palette for the structured
     * value syntax highlighter so its tokens follow the application Material theme.
     *
     * The QML Value panel calls this with the current theme and on every theme
     * toggle. The choice is remembered and applied to the highlighter when it is
     * installed, so ordering between install and theme changes does not matter.
     */
    Q_INVOKABLE void setStructuredValueDarkTheme(bool dark);

    /** Writes \a value to the node backing the Data Access View row at \a row. */
    Q_INVOKABLE void writeValue(int row, const QVariant &value);

    /**
     * Applies \a data as the active project's runtime state: loads its monitored
     * nodes into the Data Access View, restores the pinned focus node and value
     * format, and stores its connection configuration for a later connect. No
     * connection is started; call connectToProjectConnection() afterwards.
     */
    void applyProject(const ProjectData &data);

    /** Snapshots the current runtime state into a ProjectData for saving. */
    ProjectData exportProject() const;

    /**
     * Releases all project runtime state: disconnects any session, clears the
     * tree, focus, data, and attribute models, and resets the stored connection,
     * focus node, and value format. Used when a project is closed or switched.
     */
    void clearRuntimeState();

    /**
     * Connects using the active project's stored connection configuration, if any.
     * Username authentication emits passwordRequired() first, exactly like
     * connectToLast(). Does nothing when no connection is configured.
     */
    Q_INVOKABLE void connectToProjectConnection();

    /** Returns whether legacy pre-project state exists to migrate into a project. */
    bool hasLegacyState() const;

    /**
     * Builds a ProjectData from the legacy INI connection, INI focus node, native
     * value format, and SQLite monitored nodes, for one-time migration into a
     * Default project. Returns an empty project when no legacy state exists.
     */
    ProjectData exportLegacyState() const;

    /** Attaches the worker-thread \a service and wires queued cross-thread signals. */
    void attachService(OpcUaService *service);

signals:
    /** Emitted when available backend plugin names change. */
    void opcUaBackendChanged();
    /** Emitted when the selected backend plugin changes. */
    void backendChanged();
    /** Emitted when discovered server display rows change. */
    void serversChanged();
    /** Emitted when endpoint display rows change. */
    void endpointsChanged();
    /** Emitted when the connected state changes. */
    void connectedChanged();
    /** Emitted when the derived busy state changes. */
    void busyChanged();
    /** Emitted when the current operation state changes. */
    void operationStateChanged();
    /** Emitted when the current client state changes. */
    void clientStateChanged();
    /** Emitted when endpoint URL rewriting changes. */
    void endpointUrlRewriteEnabledChanged();
    /** Emitted when the last user-visible error text changes. */
    void lastErrorChanged();
    /** Emitted when the authentication token mode changes. */
    void authModeChanged();
    /** Emitted when the structured-value output format changes. */
    void valueFormatChanged();
    /** Emitted when the structured value text or its availability changes. */
    void structuredValueChanged();
    /** Emitted when the currently selected node id changes. */
    void selectedNodeIdChanged();

    /** Emitted when the pinned focus node or its display name changes. */
    void focusNodeChanged();

    /** Emitted when the availability of a stored connection changes. */
    void hasLastConnectionChanged();

    /**
     * Emitted when project-relevant runtime state changes through a user action
     * (monitored nodes, focus node, value format, or a new connection). The
     * project manager uses it to mark the active project as having unsaved
     * changes. Not emitted while applyProject() loads project state.
     */
    void projectStateChanged();

    /**
     * Emitted while connectToLast() needs the password for username authentication
     * with \a userName. QML shows a prompt and calls provideReconnectPassword().
     */
    void passwordRequired(const QString &userName);

    /** Requests worker-service initialization. */
    void initializeRequested();
    /** Requests backend selection on the worker service. */
    void setBackendRequested(const QString &backend);
    /** Requests anonymous authentication on the worker service. */
    void setAnonymousAuthenticationRequested();
    /** Requests username authentication on the worker service. */
    void setUsernameAuthenticationRequested(const QString &userName, const QString &password);
    /** Requests storing the private-key \a password on the worker service. */
    void setCertificatePrivateKeyPasswordRequested(const QString &password);
    /** Requests certificate authentication on the worker service. */
    void setCertificateAuthenticationRequested();
    /** Requests endpoint URL rewriting on the worker service. */
    void setEndpointUrlRewriteEnabledRequested(bool enabled);
    /** Requests server discovery for \a hostOrUrl on the worker service. */
    void discoverServersRequested(const QString &hostOrUrl);
    /** Requests endpoint discovery for \a serverUrl on the worker service. */
    void requestEndpointsRequested(const QString &serverUrl);
    /** Requests endpoint discovery for the server at \a serverIndex on the worker service. */
    void requestEndpointsForServerRequested(int serverIndex);
    /** Requests connection to the endpoint at \a endpointIndex on the worker service. */
    void connectToEndpointRequested(int endpointIndex);
    /** Requests disconnection on the worker service. */
    void disconnectFromServerRequested();
    /** Requests browsing children of \a parentNodeId for the model request \a requestId. */
    void browseChildrenRequested(const QString &parentNodeId, quint64 requestId);
    /** Requests reading attributes of \a nodeId for the panel request \a requestId. */
    void readAttributesRequested(const QString &nodeId, quint64 requestId);
    /** Requests reading the structured value of \a nodeId for panel request \a requestId. */
    void readStructuredValueRequested(const QString &nodeId, quint64 requestId);
    /** Requests starting a value subscription for \a nodeId on the worker service. */
    void subscribeNodeRequested(const QString &nodeId);
    /** Requests stopping the value subscription for \a nodeId on the worker service. */
    void unsubscribeNodeRequested(const QString &nodeId);
    /** Requests writing \a value to \a nodeId on the worker service. */
    void writeValueRequested(const QString &nodeId, const QVariant &value);

public slots:
    /** Applies available backend plugin names received from the worker service. */
    void applyAvailableBackends(const QStringList &backends);
    /** Applies the selected backend plugin name received from the worker service. */
    void applyBackend(const QString &backend);
    /** Applies discovered server display rows received from the worker service. */
    void applyServers(const QStringList &servers);
    /** Applies endpoint display rows received from the worker service. */
    void applyEndpoints(const QStringList &endpoints);
    /** Applies the connection state received from the worker service. */
    void applyConnected(bool connected);
    /** Applies the current operation state received from the worker service. */
    void applyOperationState(int operationState);
    /** Applies the current client state received from the worker service. */
    void applyClientState(int clientState);
    /** Applies endpoint URL rewriting state received from the worker service. */
    void applyEndpointUrlRewriteEnabled(bool enabled);
    /** Applies the last user-visible error text received from the worker service. */
    void applyLastError(const QString &lastError);
    /** Applies the authentication mode received from the worker service. */
    void applyAuthMode(int authMode);
    /** Applies browse \a children for \a parentNodeId and \a requestId to the tree model. */
    void applyBrowseChildren(const QString &parentNodeId,
                             quint64 requestId,
                             const QList<OpcUaNodeData> &children,
                             bool success);
    /** Applies node attributes for \a requestId to the Attributes panel model. */
    void applyNodeAttributes(quint64 requestId, const OpcUaAttributeData &data, bool success);
    /** Applies the decoded value tree for \a requestId and \a nodeId to the View panel. */
    void applyStructuredValue(quint64 requestId,
                              const QString &nodeId,
                              const OpcUaValueTreeNode &root,
                              bool success);
    /** Applies a live value \a update to the Data Access View table model. */
    void applyMonitoredValue(const OpcUaValueUpdate &update);
    /** Applies a write result for \a nodeId, surfacing \a error when the write failed. */
    void applyWriteCompleted(const QString &nodeId, bool success, const QString &error);

private:
    /** Builds a slash-separated browse path for \a treeIndex from its ancestors. */
    QString buildNodePath(const QModelIndex &treeIndex) const;
    /** Makes \a nodeId the selected node and requests its attributes and value. */
    void selectNode(const QString &nodeId);
    /** Pushes the monitored node ids of the current server to the tree model. */
    void refreshMonitoredNodeIds();
    /** Returns the owned model that produced \a index (focus model or tree model). */
    OpcUaModel *modelForIndex(const QModelIndex &index) const;
    /** Pins \a nodeId as the focus node using \a absolutePath and \a displayName. */
    void setFocusNode(const QString &nodeId,
                      const QString &absolutePath,
                      const QString &displayName);
    /** Applies the pinned focus node to the focus model for the current connection. */
    void applyFocusNodeToModel();
    /** Assigns a routed browse id for \a model and forwards the request to the service. */
    void routeFetch(OpcUaModel *model, const QString &parentNodeId, quint64 modelRequestId);
    /** Recomputes hasLastConnection() from the active project connection. */
    void updateHasLastConnection();
    /** Seeds the reconnect state machine from \a config and starts connecting. */
    void connectUsingConfig(const ProjectConnectionConfig &config);
    /** Rebuilds m_connection from the live session and emits on a real change. */
    void updateConnectionFromLiveState();
    /** Starts server discovery for an in-progress connectToLast() run. */
    void startReconnectDiscovery();
    /** Aborts an in-progress connectToLast() run and reports \a reason. */
    void abortReconnect(const QString &reason);

    /** Reconnect state-machine stages driven by connectToLast(). */
    enum class ReconnectStage {
        /** No reconnection is in progress. */
        Idle,
        /** Waiting for the username password from QML. */
        AwaitingPassword,
        /** FindServers is in progress for the stored server. */
        DiscoveringServers,
        /** GetEndpoints is in progress for the stored server. */
        RequestingEndpoints,
        /** Connection to the matched endpoint is in progress. */
        Connecting
    };

    /** Route from a service browse id back to the requesting model and its request id. */
    struct BrowseRoute {
        /** Model that issued the browse request. */
        OpcUaModel *model {nullptr};
        /** Request id the model expects in applyChildrenSnapshot(). */
        quint64 modelRequestId {0};
    };

    /** Protects state mirrored from queued worker-service signals. */
    mutable QMutex m_stateMutex;

    /** Default discovery URL used when QML submits an empty discovery input. */
    QString m_initialUrl;

    /** Cached available backend plugin names. */
    QStringList m_availableBackends;

    /** Cached selected backend plugin name. */
    QString m_backend;

    /** Cached discovered server display rows. */
    QStringList m_servers;

    /** Cached endpoint display rows. */
    QStringList m_endpoints;

    /** Cached connection state. */
    bool m_connected {false};

    /** Cached operation state as an OperationState integer. */
    int m_operationState {OperationIdle};

    /** Cached client state as a ClientState integer. */
    int m_clientState {ClientDisconnected};

    /** Cached endpoint URL rewriting state. */
    bool m_endpointUrlRewriteEnabled {false};

    /** Cached user-visible error text. */
    QString m_lastError;

    /** Cached authentication token mode as an integer. */
    int m_authMode {0};

    /** Owned GUI-thread model exposed to QML. */
    OpcUaModel *m_treeModel {nullptr};

    /** Owned focus-segment model rooted at the pinned node. */
    OpcUaModel *m_focusModel {nullptr};

    /** Node id of the pinned focus node; empty when none is pinned. */
    QString m_focusNodeId;
    /** Absolute display path of the pinned focus node. */
    QString m_focusNodePath;
    /** Display name of the pinned focus node. */
    QString m_focusNodeName;

    /** Non-owning INI settings store for last-connection and focus persistence. */
    QSettings *m_settings {nullptr};

    /** Cached availability of a stored connection, mirrored to QML. */
    bool m_hasLastConnection {false};

    /** Active project's connection configuration; the source for reconnects and saves. */
    ProjectConnectionConfig m_connection;

    /** Routes service browse ids to the model that requested them. */
    QHash<quint64, BrowseRoute> m_browseRouting;
    /** Monotonic browse id shared by both models and sent to the service. */
    quint64 m_nextBrowseRequestId {0};

    /** Discovery URL of the most recent connection attempt, persisted on success. */
    QString m_lastDiscoveryUrl;
    /** Endpoint display string of the most recent connect, persisted on success. */
    QString m_lastEndpointDisplay;
    /** User name of the most recent username authentication; never the password. */
    QString m_lastUserName;

    /** Current reconnection stage for connectToLast(). */
    ReconnectStage m_reconnectStage {ReconnectStage::Idle};
    /** Stored server display string to match during a reconnect. */
    QString m_reconnectServer;
    /** Stored endpoint display string to match during a reconnect. */
    QString m_reconnectEndpoint;
    /** Stored user name to reuse during a reconnect. */
    QString m_reconnectUser;
    /** Stored discovery URL to reuse during a reconnect. */
    QString m_reconnectDiscoveryUrl;
    /** Stored authentication mode to reuse during a reconnect. */
    int m_reconnectAuthMode {0};

    /** Owned Data Access View table model exposed to QML. */
    DataAccessModel *m_dataModel {nullptr};

    /** Owned Attributes panel model exposed to QML. */
    AttributesModel *m_attributesModel {nullptr};

    /** Owned persistent store for monitored nodes. */
    std::unique_ptr<NodeDatabase> m_nodeDatabase;

    /** Server identity string used for new Data Access View rows and DB scoping. */
    QString m_currentServer;

    /** Monotonic id used to correlate and de-duplicate attribute read results. */
    quint64 m_nextAttributeRequestId {0};

    /** Attribute request id whose result is still relevant for the panel. */
    quint64 m_pendingAttributeRequestId {0};

    /** Structured-value request id whose result is still relevant for the panel. */
    quint64 m_pendingStructuredRequestId {0};

    /** Node id of the last selected node, used by refreshStructuredValue(). */
    QString m_selectedNodeId;

    /** Current structured-value output format. */
    ValueFormat m_valueFormat {FormatJson};

    /** Cached decoded value tree of the selected node, kept for re-rendering. */
    OpcUaValueTreeNode m_structuredValueRoot;

    /** Formatted structured value text shown in the View panel. */
    QString m_structuredValueText;

    /** Whether a renderable structured value is currently available. */
    bool m_structuredValueAvailable {false};

    /** Syntax highlighter for the View panel; owned by the TextArea's QTextDocument. */
    QPointer<StructuredValueHighlighter> m_structuredValueHighlighter;

    /** Whether the structured-value highlighter uses the dark color palette. */
    bool m_structuredValueDarkTheme {true};

    /** Non-owning worker-service pointer used only for attachment identity checks. */
    OpcUaService *m_service {nullptr};
};

#endif // OPCUAMANAGER_H
