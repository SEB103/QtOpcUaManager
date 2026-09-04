#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QtQuick/QQuickTextDocument>

#include "core/opcuaservice.h"
#include "models/structuredvalueformatter.h"
#include "opcuamanager.h"
#include "structuredvaluehighlighter.h"

namespace {

/*!
 * \internal
 * \brief QSettings key storing the persisted structured-value output format.
 */
constexpr auto kValueFormatSettingsKey = "view/valueFormat";

/*!
 * \internal
 * \brief Maps an OpcUaManager::ValueFormat to the formatter output format.
 */
StructuredValueFormatter::Format toFormatterFormat(OpcUaManager::ValueFormat format)
{
    return format == OpcUaManager::FormatXml ? StructuredValueFormatter::Format::Xml
                                             : StructuredValueFormatter::Format::Json;
}

/*!
 * \internal
 * \brief Maps an OpcUaManager::ValueFormat to the highlighter language.
 */
StructuredValueHighlighter::Language toHighlighterLanguage(OpcUaManager::ValueFormat format)
{
    return format == OpcUaManager::FormatXml ? StructuredValueHighlighter::Language::Xml
                                             : StructuredValueHighlighter::Language::Json;
}

} // namespace

/*!
 * \property OpcUaManager::opcUaBackend
 * \brief Available Qt OPC UA backend plugin names.
 */

/*!
 * \property OpcUaManager::backend
 * \brief Currently selected Qt OPC UA backend plugin name.
 */

/*!
 * \property OpcUaManager::servers
 * \brief Display rows for discovered OPC UA servers.
 */

/*!
 * \property OpcUaManager::endpoints
 * \brief Display rows for endpoints returned by the selected server.
 */

/*!
 * \property OpcUaManager::connected
 * \brief Whether an OPC UA session is currently connected.
 */

/*!
 * \property OpcUaManager::busy
 * \brief Whether discovery, endpoint lookup, connect, or disconnect is active.
 */

/*!
 * \property OpcUaManager::operationState
 * \brief Current operation state as an OpcUaManager::OperationState integer.
 */

/*!
 * \property OpcUaManager::clientState
 * \brief Current QOpcUaClient state as an OpcUaManager::ClientState integer.
 */

/*!
 * \property OpcUaManager::endpointUrlRewriteEnabled
 * \brief Whether advertised endpoint URLs are rewritten to the discovery host and port.
 */

/*!
 * \property OpcUaManager::treeModel
 * \brief Tree model exposed to QML for address-space browsing; owned by this manager.
 */

/*!
 * \property OpcUaManager::lastError
 * \brief Last user-visible OPC UA error text; empty when no error is active.
 */

/*!
 * \property OpcUaManager::authMode
 * \brief Current QOpcUaUserTokenPolicy::TokenType as an integer for QML controls.
 */

OpcUaManager::OpcUaManager(const QString &initialUrl, QObject *parent)
    : QObject(parent)
    , m_initialUrl(initialUrl)
    , m_treeModel(new OpcUaModel(this))
    , m_focusModel(new OpcUaModel(this))
    , m_dataModel(new DataAccessModel(this))
    , m_attributesModel(new AttributesModel(this))
    , m_nodeDatabase(std::make_unique<NodeDatabase>())
{
    // The legacy SQLite store is opened read-only for one-time migration into a
    // project file. Monitored nodes are no longer seeded from it at startup; the
    // active project is the source of truth and fills the Data Access View through
    // applyProject(). See exportLegacyState().
    const QString databasePath =
        QCoreApplication::applicationDirPath() + QLatin1String("/db/opcua_nodes.db");
    if (!m_nodeDatabase->open(databasePath))
        qWarning() << "OpcUaManager: failed to open node database at" << databasePath;

    // Restore the structured-value output format chosen in a previous session.
    const int storedFormat =
        QSettings().value(QLatin1String(kValueFormatSettingsKey), FormatJson).toInt();
    m_valueFormat = storedFormat == FormatXml ? FormatXml : FormatJson;
}

OpcUaManager::~OpcUaManager() = default;

/*!
 * \property OpcUaManager::dataModel
 * \brief Data Access View table model exposed to QML; owned by this manager.
 */

/*!
 * \property OpcUaManager::attributesModel
 * \brief Attributes panel model exposed to QML; owned by this manager.
 */

QStringList OpcUaManager::opcUaBackend() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_availableBackends;
}

QString OpcUaManager::backend() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_backend;
}

QStringList OpcUaManager::servers() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_servers;
}

QStringList OpcUaManager::endpoints() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_endpoints;
}

bool OpcUaManager::connected() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_connected;
}

bool OpcUaManager::busy() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_operationState != OperationIdle;
}

int OpcUaManager::operationState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_operationState;
}

int OpcUaManager::clientState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_clientState;
}

bool OpcUaManager::endpointUrlRewriteEnabled() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_endpointUrlRewriteEnabled;
}

OpcUaModel *OpcUaManager::treeModel() const
{
    return m_treeModel;
}

/*!
 * \brief Returns the owned focus-segment tree model exposed to QML.
 */
OpcUaModel *OpcUaManager::focusModel() const
{
    return m_focusModel;
}

/*!
 * \brief Returns the node id of the pinned focus node, or an empty string.
 */
QString OpcUaManager::focusNodeId() const
{
    return m_focusNodeId;
}

/*!
 * \brief Returns the display name of the pinned focus node.
 */
QString OpcUaManager::focusNodeName() const
{
    return m_focusNodeName;
}

/*!
 * \brief Returns whether a stored connection is available for reconnection.
 */
bool OpcUaManager::hasLastConnection() const
{
    return m_hasLastConnection;
}

/*!
 * \brief Injects the INI settings store used for legacy migration and defaults.
 * \param settings Non-owning settings store, or null to disable persistence.
 *
 * No project state is restored at injection time; the launcher opens a project
 * later, and applyProject() restores its state then.
 */
void OpcUaManager::setSettings(QSettings *settings)
{
    // The project layer owns connection and focus persistence; the injected store
    // is retained only for reading legacy state during one-time migration and for
    // the native value-format default. No project state is restored here at
    // startup, because no project is active until the launcher opens one.
    m_settings = settings;
}

DataAccessModel *OpcUaManager::dataModel() const
{
    return m_dataModel;
}

AttributesModel *OpcUaManager::attributesModel() const
{
    return m_attributesModel;
}

QString OpcUaManager::lastError() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_lastError;
}

int OpcUaManager::authMode() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_authMode;
}

void OpcUaManager::setBackend(const QString &backend)
{
    emit setBackendRequested(backend);
}

void OpcUaManager::setEndpointUrlRewriteEnabled(bool enabled)
{
    emit setEndpointUrlRewriteEnabledRequested(enabled);
}

void OpcUaManager::setAnonymousAuthentication()
{
    emit setAnonymousAuthenticationRequested();
}

void OpcUaManager::setUsernameAuthentication(const QString &userName, const QString &password)
{
    // Remember only the user name for persistence; the password is never stored.
    m_lastUserName = userName;
    emit setUsernameAuthenticationRequested(userName, password);
}

void OpcUaManager::setCertificatePrivateKeyPassword(const QString &password)
{
    emit setCertificatePrivateKeyPasswordRequested(password);
}

void OpcUaManager::setCertificateAuthentication()
{
    emit setCertificateAuthenticationRequested();
}

void OpcUaManager::discoverServers(const QString &hostOrUrl)
{
    const QString effectiveUrl = hostOrUrl.trimmed().isEmpty() ? m_initialUrl : hostOrUrl;
    m_lastDiscoveryUrl = effectiveUrl;
    qInfo() << "OpcUaManager discoverServers requested:" << effectiveUrl;
    emit discoverServersRequested(effectiveUrl);
}

void OpcUaManager::requestEndpoints(const QString &serverUrl)
{
    qInfo() << "OpcUaManager requestEndpoints requested:" << serverUrl;
    if (!serverUrl.trimmed().isEmpty())
        m_currentServer = serverUrl.trimmed();
    emit requestEndpointsRequested(serverUrl);
}

void OpcUaManager::requestEndpointsForServer(int serverIndex)
{
    qInfo() << "OpcUaManager requestEndpointsForServer requested. index:" << serverIndex;
    QMutexLocker locker(&m_stateMutex);
    const QString server = m_servers.value(serverIndex);
    locker.unlock();
    if (!server.isEmpty())
        m_currentServer = server;
    emit requestEndpointsForServerRequested(serverIndex);
}

void OpcUaManager::connectToEndpoint(int endpointIndex)
{
    qInfo() << "OpcUaManager connectToEndpoint requested. index:" << endpointIndex;
    {
        QMutexLocker locker(&m_stateMutex);
        m_lastEndpointDisplay = m_endpoints.value(endpointIndex);
    }
    emit connectToEndpointRequested(endpointIndex);
}

void OpcUaManager::disconnectFromServer()
{
    qInfo() << "OpcUaManager disconnectFromServer requested.";
    emit disconnectFromServerRequested();
}

/*!
 * \brief Builds a slash-separated browse path for \a treeIndex from its ancestors.
 *
 * The path is composed from the display names of the node and its ancestors, so
 * the Data Access View can show where a monitored node lives in the address space.
 */
QString OpcUaManager::buildNodePath(const QModelIndex &treeIndex) const
{
    if (!treeIndex.isValid())
        return {};

    const QAbstractItemModel *model = treeIndex.model();
    if (!model)
        return {};

    QStringList segments;
    for (QModelIndex index = treeIndex; index.isValid(); index = index.parent()) {
        const QString name = model->data(index, OpcUaModel::DisplayNameRole).toString();
        if (!name.isEmpty())
            segments.prepend(name);
    }

    QString path = segments.join(QLatin1Char('/'));

    // The focus model is rooted at the pinned node, so a path walked inside it is
    // relative to that node. Prefix the focus node's own absolute path to keep the
    // stored path server-absolute for cross-panel reveal and persistence.
    if (model == m_focusModel && !m_focusNodePath.isEmpty()) {
        path = path.isEmpty()
                   ? m_focusNodePath
                   : m_focusNodePath + QLatin1Char('/') + path;
    }

    return path;
}

/*!
 * \internal
 * \brief Pushes the monitored node ids of the current server to the tree model.
 *
 * Collects the node ids of every Data Access View row that belongs to the
 * currently connected server and hands them to the tree model, which uses them
 * to restore the monitoring checkbox for matching nodes as the tree is browsed.
 */
void OpcUaManager::refreshMonitoredNodeIds()
{
    if (!m_treeModel || !m_dataModel)
        return;

    const QString server = m_currentServer.isEmpty() ? m_initialUrl : m_currentServer;
    QSet<QString> ids;
    const int rows = m_dataModel->rowCount();
    for (int i = 0; i < rows; ++i) {
        if (m_dataModel->serverAt(i) == server) {
            const QString nodeId = m_dataModel->nodeIdAt(i);
            if (!nodeId.isEmpty())
                ids.insert(nodeId);
        }
    }
    m_treeModel->setMonitoredNodeIds(ids);
    if (m_focusModel)
        m_focusModel->setMonitoredNodeIds(ids);
}

/*!
 * \brief Adds or removes the node at \a treeIndex from the Data Access View.
 * \param on Whether the node should be monitored.
 *
 * Adding inserts the node into the table and starts a live subscription when
 * connected; removing reverses both steps. The monitored-node set is part of the
 * active project, so projectStateChanged() is emitted for the project manager to
 * record an unsaved change; the set is persisted when the project is saved.
 */
void OpcUaManager::setNodeMonitored(const QModelIndex &treeIndex, bool on)
{
    if (!treeIndex.isValid())
        return;

    OpcUaModel *model = modelForIndex(treeIndex);
    const QString nodeId = model->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    const QString server = m_currentServer.isEmpty() ? m_initialUrl : m_currentServer;

    if (on) {
        MonitoredNodeRecord record;
        record.server = server;
        record.nodeId = nodeId;
        record.nodePath = buildNodePath(treeIndex);
        record.displayName = model->data(treeIndex, OpcUaModel::DisplayNameRole).toString();
        record.dataType = model->data(treeIndex, OpcUaModel::DataTypeRole).toString();

        m_dataModel->addRow(record);
        model->setMonitoringEnabledAt(treeIndex, true);
        if (connected())
            emit subscribeNodeRequested(nodeId);
    } else {
        const int row = m_dataModel->rowCount();
        for (int i = 0; i < row; ++i) {
            if (m_dataModel->nodeIdAt(i) == nodeId) {
                m_dataModel->removeAt(i);
                break;
            }
        }
        model->setMonitoringEnabledAt(treeIndex, false);
        emit unsubscribeNodeRequested(nodeId);
    }

    // Keep both models' monitored-id sets in sync so later re-browses stay correct.
    refreshMonitoredNodeIds();

    // The monitored-node set belongs to the active project; mark it changed.
    emit projectStateChanged();
}

/*!
 * \brief Removes the Data Access View row at \a row from the table.
 *
 * The monitored-node set is part of the active project, so projectStateChanged()
 * is emitted; the change is persisted when the project is saved.
 */
void OpcUaManager::removeNode(int row)
{
    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    m_dataModel->removeAt(row);
    emit unsubscribeNodeRequested(nodeId);
    emit projectStateChanged();
}

/*!
 * \brief Requests the attributes of the node at \a treeIndex for the panel.
 *
 * Only the most recent request is applied; earlier in-flight results are ignored
 * so that fast selection changes do not show stale attributes.
 */
void OpcUaManager::requestAttributes(const QModelIndex &treeIndex)
{
    if (!treeIndex.isValid())
        return;

    const QString nodeId = modelForIndex(treeIndex)->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    selectNode(nodeId);
}

/*!
 * \brief Selects the Data Access View row at \a row, driving the shared node selection.
 *
 * The Data Access View only knows the node id string, so it resolves the id from
 * the table model and selects it directly instead of going through a tree index.
 */
void OpcUaManager::selectDataRow(int row)
{
    if (!m_dataModel)
        return;

    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    selectNode(nodeId);
}

/*!
 * \internal
 * \brief Makes \a nodeId the selected node and requests its attributes and value.
 *
 * Centralizes selection so that clicks in the address-space tree and the Data
 * Access View share one selected-node state. Only the most recent request is
 * applied; earlier in-flight results are ignored so that fast selection changes
 * do not show stale attributes. The selected node id is exposed to QML so both
 * panels can highlight the matching row.
 */
void OpcUaManager::selectNode(const QString &nodeId)
{
    const quint64 requestId = ++m_nextAttributeRequestId;
    m_pendingAttributeRequestId = requestId;
    m_pendingStructuredRequestId = requestId;

    if (m_selectedNodeId != nodeId) {
        m_selectedNodeId = nodeId;
        emit selectedNodeIdChanged();
    }

    emit readAttributesRequested(nodeId, requestId);
    emit readStructuredValueRequested(nodeId, requestId);
}

/*!
 * \internal
 * \brief Returns the owned model that produced \a index.
 *
 * A tree \a index carries a pointer to its source model, so a click in the focus
 * segment is routed to the focus model and any other index to the main tree model.
 */
OpcUaModel *OpcUaManager::modelForIndex(const QModelIndex &index) const
{
    return index.model() == m_focusModel ? m_focusModel : m_treeModel;
}

/*!
 * \brief Pins the node at the tree \a treeIndex as the focus node.
 *
 * Resolves the node id, absolute path, and display name from the source model and
 * re-roots the focus segment on that node. The index may come from either model.
 */
void OpcUaManager::setFocusNodeFromIndex(const QModelIndex &treeIndex)
{
    if (!treeIndex.isValid())
        return;

    OpcUaModel *model = modelForIndex(treeIndex);
    const QString nodeId = model->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    const QString name = model->data(treeIndex, OpcUaModel::DisplayNameRole).toString();
    const QString path = buildNodePath(treeIndex);
    setFocusNode(nodeId, path, name);
}

/*!
 * \internal
 * \brief Pins \a nodeId as the focus node and updates the focus segment.
 * \param absolutePath The server-absolute display path of the node.
 * \param displayName The display name shown as the focus segment title.
 */
void OpcUaManager::setFocusNode(const QString &nodeId,
                                const QString &absolutePath,
                                const QString &displayName)
{
    if (nodeId.isEmpty())
        return;

    m_focusNodeId = nodeId;
    m_focusNodePath = absolutePath;
    m_focusNodeName = displayName.isEmpty() ? nodeId : displayName;

    applyFocusNodeToModel();
    emit focusNodeChanged();
    emit projectStateChanged();
}

/*!
 * \brief Clears the pinned focus node and empties the focus segment.
 */
void OpcUaManager::clearFocusNode()
{
    m_focusNodeId.clear();
    m_focusNodePath.clear();
    m_focusNodeName.clear();

    if (m_focusModel)
        m_focusModel->clear();

    emit focusNodeChanged();
    emit projectStateChanged();
}

/*!
 * \internal
 * \brief Applies the pinned focus node to the focus model for the current session.
 *
 * The focus model is re-rooted at the pinned node and activated only while a
 * session is connected; otherwise it is cleared so the segment stays empty.
 */
void OpcUaManager::applyFocusNodeToModel()
{
    if (!m_focusModel)
        return;

    if (m_focusNodeId.isEmpty() || !connected()) {
        m_focusModel->clear();
        return;
    }

    m_focusModel->setRootNode(m_focusNodeId, m_focusNodeName);
    m_focusModel->setConnectionActive(true);
}

/*!
 * \internal
 * \brief Assigns a routed browse id for \a model and forwards the request.
 *
 * Both tree models generate their own request ids, which can collide. Each browse
 * is remapped to a manager-global id sent to the service, so the reply is routed
 * back to the model that asked for it with the request id that model expects.
 */
void OpcUaManager::routeFetch(OpcUaModel *model,
                              const QString &parentNodeId,
                              quint64 modelRequestId)
{
    const quint64 browseId = ++m_nextBrowseRequestId;
    m_browseRouting.insert(browseId, BrowseRoute{model, modelRequestId});
    emit browseChildrenRequested(parentNodeId, browseId);
}

/*!
 * \internal
 * \brief Recomputes hasLastConnection() from the active project connection.
 */
void OpcUaManager::updateHasLastConnection()
{
    const bool has = m_connection.isConfigured();
    if (has == m_hasLastConnection)
        return;

    m_hasLastConnection = has;
    emit hasLastConnectionChanged();
}

/*!
 * \internal
 * \brief Rebuilds m_connection from the live session and emits on a real change.
 *
 * Called after a successful connect. When the resulting configuration differs
 * from the one currently held (for example, a brand-new project connecting for
 * the first time), projectStateChanged() is emitted so the project manager can
 * record the unsaved change. Reconnecting with the same parameters an opened
 * project already carries produces no change and no dirty flag.
 */
void OpcUaManager::updateConnectionFromLiveState()
{
    ProjectConnectionConfig live;
    {
        QMutexLocker locker(&m_stateMutex);
        live.backend = m_backend;
        live.authMode = m_authMode;
        live.endpointUrlRewriteEnabled = m_endpointUrlRewriteEnabled;
    }
    live.discoveryUrl = m_lastDiscoveryUrl;
    live.server = m_currentServer;
    live.endpoint = m_lastEndpointDisplay;
    live.userName = live.authMode == 1 ? m_lastUserName : QString();

    const bool changed = live != m_connection;
    m_connection = live;
    updateHasLastConnection();
    if (changed)
        emit projectStateChanged();
}

/*!
 * \brief Reconnects using the active project's stored connection configuration.
 *
 * Selects the backend and authentication mode and starts server discovery.
 * Username authentication needs a password, which is never stored, so
 * passwordRequired() is emitted and the run resumes in provideReconnectPassword().
 */
void OpcUaManager::connectToLast()
{
    if (!m_connection.isConfigured()) {
        applyLastError(tr("No stored connection is available."));
        return;
    }
    connectUsingConfig(m_connection);
}

/*!
 * \brief Connects using the active project's stored connection configuration, if any.
 *
 * Called right after a project is opened. Does nothing when the project has no
 * connection configured, so a freshly created project stays disconnected until
 * the user configures a connection.
 */
void OpcUaManager::connectToProjectConnection()
{
    if (!m_connection.isConfigured())
        return;
    connectUsingConfig(m_connection);
}

/*!
 * \internal
 * \brief Seeds the reconnect state machine from \a config and starts connecting.
 *
 * Shared by connectToLast() and connectToProjectConnection(). The pinned focus
 * node is not restored here; applyProject() already loaded it before this runs.
 */
void OpcUaManager::connectUsingConfig(const ProjectConnectionConfig &config)
{
    if (connected() || busy())
        return;
    if (!config.isConfigured()) {
        applyLastError(tr("No stored connection is available."));
        return;
    }

    m_reconnectDiscoveryUrl = config.discoveryUrl;
    m_reconnectServer = config.server;
    m_reconnectEndpoint = config.endpoint;
    m_reconnectAuthMode = config.authMode;
    m_reconnectUser = config.userName;

    setEndpointUrlRewriteEnabled(config.endpointUrlRewriteEnabled);

    if (!config.backend.isEmpty())
        setBackend(config.backend);

    // Username authentication (authMode 1) needs a password, which is never
    // stored: ask QML for it and continue in provideReconnectPassword().
    if (m_reconnectAuthMode == 1) {
        m_reconnectStage = ReconnectStage::AwaitingPassword;
        emit passwordRequired(m_reconnectUser);
        return;
    }

    if (m_reconnectAuthMode == 2)
        setCertificateAuthentication();
    else
        setAnonymousAuthentication();

    startReconnectDiscovery();
}

/*!
 * \brief Supplies the \a password and resumes a waiting connectToLast() run.
 */
void OpcUaManager::provideReconnectPassword(const QString &password)
{
    if (m_reconnectStage != ReconnectStage::AwaitingPassword)
        return;

    setUsernameAuthentication(m_reconnectUser, password);
    startReconnectDiscovery();
}

/*!
 * \internal
 * \brief Starts server discovery for an in-progress connectToLast() run.
 */
void OpcUaManager::startReconnectDiscovery()
{
    m_reconnectStage = ReconnectStage::DiscoveringServers;
    discoverServers(m_reconnectDiscoveryUrl);
}

/*!
 * \internal
 * \brief Aborts an in-progress connectToLast() run and reports \a reason.
 */
void OpcUaManager::abortReconnect(const QString &reason)
{
    if (m_reconnectStage == ReconnectStage::Idle)
        return;

    m_reconnectStage = ReconnectStage::Idle;
    if (!reason.isEmpty())
        applyLastError(reason);
}

/*!
 * \brief Returns the current structured-value output format.
 */
OpcUaManager::ValueFormat OpcUaManager::valueFormat() const
{
    return m_valueFormat;
}

/*!
 * \brief Returns the formatted structured value text for the selected node.
 */
QString OpcUaManager::structuredValueText() const
{
    return m_structuredValueText;
}

/*!
 * \brief Returns whether a renderable structured value is available for the panel.
 */
bool OpcUaManager::structuredValueAvailable() const
{
    return m_structuredValueAvailable;
}

/*!
 * \brief Returns the node id of the currently selected node.
 */
QString OpcUaManager::selectedNodeId() const
{
    return m_selectedNodeId;
}

/*!
 * \brief Sets the structured-value output \a format and re-renders the cached value.
 *
 * The choice is persisted so it is restored on the next run. When a decoded value
 * is cached it is re-rendered immediately in the new format.
 */
void OpcUaManager::setValueFormat(ValueFormat format)
{
    if (format == m_valueFormat)
        return;

    m_valueFormat = format;
    QSettings().setValue(QLatin1String(kValueFormatSettingsKey), static_cast<int>(format));
    emit valueFormatChanged();
    emit projectStateChanged();

    if (m_structuredValueHighlighter)
        m_structuredValueHighlighter->setLanguage(toHighlighterLanguage(m_valueFormat));

    if (m_structuredValueAvailable) {
        m_structuredValueText =
            StructuredValueFormatter::format(m_structuredValueRoot, toFormatterFormat(m_valueFormat));
        emit structuredValueChanged();
    }
}

/*!
 * \brief Installs the structured-value syntax highlighter on \a document.
 *
 * Attaches a StructuredValueHighlighter to the TextArea's underlying
 * QTextDocument and seeds it with the current output format. The highlighter is
 * parented to that document, so it is destroyed with the TextArea; a QPointer
 * guards the manager's reference against that. The call is ignored when
 * \a document is null or a highlighter is already installed.
 */
void OpcUaManager::installStructuredValueHighlighter(QQuickTextDocument *document)
{
    if (!document || m_structuredValueHighlighter)
        return;

    QTextDocument *textDocument = document->textDocument();
    if (!textDocument)
        return;

    m_structuredValueHighlighter = new StructuredValueHighlighter(textDocument);
    m_structuredValueHighlighter->setLanguage(toHighlighterLanguage(m_valueFormat));
    m_structuredValueHighlighter->setDarkTheme(m_structuredValueDarkTheme);
}

/*!
 * \brief Selects the dark or light palette for the structured-value highlighter.
 *
 * The value is remembered so it can be applied when the highlighter is installed;
 * an already installed highlighter is updated immediately and re-highlights.
 */
void OpcUaManager::setStructuredValueDarkTheme(bool dark)
{
    m_structuredValueDarkTheme = dark;
    if (m_structuredValueHighlighter)
        m_structuredValueHighlighter->setDarkTheme(dark);
}

/*!
 * \brief Re-reads and re-decodes the structured value of the last selected node.
 */
void OpcUaManager::refreshStructuredValue()
{
    if (m_selectedNodeId.isEmpty())
        return;

    m_pendingStructuredRequestId = ++m_nextAttributeRequestId;
    emit readStructuredValueRequested(m_selectedNodeId, m_pendingStructuredRequestId);
}

/*!
 * \brief Writes \a value to the node backing the Data Access View row at \a row.
 */
void OpcUaManager::writeValue(int row, const QVariant &value)
{
    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    emit writeValueRequested(nodeId, value);
}

/*!
 * \brief Applies \a data as the active project's runtime state.
 *
 * Stores the connection configuration for a later connect, restores the pinned
 * focus node and value format into memory, and loads the monitored nodes into the
 * Data Access View. No connection is started and projectStateChanged() is not
 * emitted, because loading a project is not an unsaved change.
 */
void OpcUaManager::applyProject(const ProjectData &data)
{
    m_connection = data.connection;
    updateHasLastConnection();

    // Restore the pinned focus node into memory; it is applied to the focus model
    // once a session connects, in applyFocusNodeToModel().
    m_focusNodeId = data.focusNode.nodeId;
    m_focusNodePath = data.focusNode.path;
    m_focusNodeName = data.focusNode.displayName.isEmpty()
                          ? data.focusNode.nodeId
                          : data.focusNode.displayName;
    emit focusNodeChanged();

    // Restore the per-project value format without re-persisting it.
    const ValueFormat format = data.settings.valueFormat == FormatXml ? FormatXml : FormatJson;
    if (format != m_valueFormat) {
        m_valueFormat = format;
        if (m_structuredValueHighlighter)
            m_structuredValueHighlighter->setLanguage(toHighlighterLanguage(m_valueFormat));
        emit valueFormatChanged();
    }

    // Load the monitored nodes into the Data Access View and seed the tree so the
    // monitoring checkbox is restored for those nodes when the address space is
    // browsed after connecting.
    m_dataModel->setRecords(data.monitoredNodes);
    refreshMonitoredNodeIds();
}

/*!
 * \brief Snapshots the current runtime state into a ProjectData for saving.
 *
 * The display name is left empty for the project manager to fill in, since it
 * owns the project's identity and file path.
 */
ProjectData OpcUaManager::exportProject() const
{
    ProjectData data;
    data.formatVersion = kProjectFormatVersion;
    data.connection = m_connection;
    data.focusNode.nodeId = m_focusNodeId;
    data.focusNode.path = m_focusNodePath;
    data.focusNode.displayName = m_focusNodeName;
    if (m_dataModel)
        data.monitoredNodes = m_dataModel->records();
    data.settings.valueFormat = static_cast<int>(m_valueFormat);
    return data;
}

/*!
 * \brief Releases all project runtime state.
 *
 * Disconnects any active session, clears the tree, focus, data, and attribute
 * models, and resets the stored connection, focus node, and selection so no stale
 * OPC UA state remains after a project is closed or switched.
 */
void OpcUaManager::clearRuntimeState()
{
    if (connected())
        disconnectFromServer();

    if (m_treeModel)
        m_treeModel->clear();
    if (m_focusModel)
        m_focusModel->clear();
    if (m_dataModel)
        m_dataModel->setRecords({});
    if (m_attributesModel)
        m_attributesModel->clear();

    m_focusNodeId.clear();
    m_focusNodePath.clear();
    m_focusNodeName.clear();
    emit focusNodeChanged();

    if (!m_selectedNodeId.isEmpty()) {
        m_selectedNodeId.clear();
        emit selectedNodeIdChanged();
    }

    m_connection = ProjectConnectionConfig{};
    updateHasLastConnection();
    m_reconnectStage = ReconnectStage::Idle;
}

/*!
 * \brief Returns whether legacy pre-project state exists to migrate into a project.
 */
bool OpcUaManager::hasLegacyState() const
{
    if (m_settings) {
        if (!m_settings->value(QStringLiteral("LastConnection/discoveryUrl")).toString().isEmpty())
            return true;
        if (!m_settings->value(QStringLiteral("LastFocusNode/nodeId")).toString().isEmpty())
            return true;
    }
    if (m_nodeDatabase && m_nodeDatabase->isOpen() && !m_nodeDatabase->loadAll().isEmpty())
        return true;
    return false;
}

/*!
 * \brief Builds a ProjectData from legacy INI, native, and SQLite state.
 *
 * Reads the INI last-connection and focus-node groups, the native value format,
 * and the SQLite monitored-node table so a one-time migration can preserve the
 * previous single-session state as a Default project. The password is not part of
 * the legacy state and is never migrated.
 */
ProjectData OpcUaManager::exportLegacyState() const
{
    ProjectData data;
    data.formatVersion = kProjectFormatVersion;

    if (m_settings) {
        m_settings->beginGroup(QStringLiteral("LastConnection"));
        data.connection.discoveryUrl = m_settings->value(QStringLiteral("discoveryUrl")).toString();
        data.connection.backend = m_settings->value(QStringLiteral("backend")).toString();
        data.connection.server = m_settings->value(QStringLiteral("server")).toString();
        data.connection.endpoint = m_settings->value(QStringLiteral("endpoint")).toString();
        data.connection.authMode = m_settings->value(QStringLiteral("authMode"), 0).toInt();
        data.connection.userName = m_settings->value(QStringLiteral("userName")).toString();
        m_settings->endGroup();

        m_settings->beginGroup(QStringLiteral("LastFocusNode"));
        data.focusNode.nodeId = m_settings->value(QStringLiteral("nodeId")).toString();
        data.focusNode.path = m_settings->value(QStringLiteral("path")).toString();
        data.focusNode.displayName = m_settings->value(QStringLiteral("displayName")).toString();
        m_settings->endGroup();
    }

    data.settings.valueFormat =
        QSettings().value(QLatin1String(kValueFormatSettingsKey), FormatJson).toInt();

    if (m_nodeDatabase && m_nodeDatabase->isOpen())
        data.monitoredNodes = m_nodeDatabase->loadAll();

    return data;
}

/*!
 * \brief Connects the GUI facade to the worker-thread OPC UA service.
 * All command signals are queued to \a service, and all service
 * results are queued back to this GUI-thread object. The function is idempotent
 * for the same service pointer and emits initializeRequested() after wiring.
 */
void OpcUaManager::attachService(OpcUaService *service)
{
    if (!service || m_service == service)
        return;
    m_service = service;

    connect(this, &OpcUaManager::initializeRequested,
            service, &OpcUaService::initialize, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setBackendRequested,
            service, &OpcUaService::setBackend, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setAnonymousAuthenticationRequested,
            service, &OpcUaService::setAnonymousAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setUsernameAuthenticationRequested,
            service, &OpcUaService::setUsernameAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setCertificatePrivateKeyPasswordRequested,
            service, &OpcUaService::setCertificatePrivateKeyPassword, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setCertificateAuthenticationRequested,
            service, &OpcUaService::setCertificateAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setEndpointUrlRewriteEnabledRequested,
            service, &OpcUaService::setEndpointUrlRewriteEnabled, Qt::QueuedConnection);
    connect(this, &OpcUaManager::discoverServersRequested,
            service, &OpcUaService::discoverServers, Qt::QueuedConnection);
    connect(this, &OpcUaManager::requestEndpointsRequested,
            service, &OpcUaService::requestEndpoints, Qt::QueuedConnection);
    connect(this, &OpcUaManager::requestEndpointsForServerRequested,
            service, &OpcUaService::requestEndpointsForServer, Qt::QueuedConnection);
    connect(this, &OpcUaManager::connectToEndpointRequested,
            service, &OpcUaService::connectToEndpoint, Qt::QueuedConnection);
    connect(this, &OpcUaManager::disconnectFromServerRequested,
            service, &OpcUaService::disconnectFromServer, Qt::QueuedConnection);
    connect(this, &OpcUaManager::browseChildrenRequested,
            service, &OpcUaService::browseChildren, Qt::QueuedConnection);

    connect(service, &OpcUaService::availableBackendsChanged,
            this, &OpcUaManager::applyAvailableBackends, Qt::QueuedConnection);
    connect(service, &OpcUaService::backendChanged,
            this, &OpcUaManager::applyBackend, Qt::QueuedConnection);
    connect(service, &OpcUaService::serversChanged,
            this, &OpcUaManager::applyServers, Qt::QueuedConnection);
    connect(service, &OpcUaService::endpointsChanged,
            this, &OpcUaManager::applyEndpoints, Qt::QueuedConnection);
    connect(service, &OpcUaService::connectedChanged,
            this, &OpcUaManager::applyConnected, Qt::QueuedConnection);
    connect(service, &OpcUaService::operationStateChanged,
            this, &OpcUaManager::applyOperationState, Qt::QueuedConnection);
    connect(service, &OpcUaService::clientStateChanged,
            this, &OpcUaManager::applyClientState, Qt::QueuedConnection);
    connect(service, &OpcUaService::endpointUrlRewriteEnabledChanged,
            this, &OpcUaManager::applyEndpointUrlRewriteEnabled, Qt::QueuedConnection);
    connect(service, &OpcUaService::lastErrorChanged,
            this, &OpcUaManager::applyLastError, Qt::QueuedConnection);
    connect(service, &OpcUaService::authModeChanged,
            this, &OpcUaManager::applyAuthMode, Qt::QueuedConnection);
    connect(service, &OpcUaService::browseChildrenReady,
            this, &OpcUaManager::applyBrowseChildren, Qt::QueuedConnection);

    // Both the main tree and the focus segment browse through the service. Each
    // model's request is remapped to a manager-global id so the reply is routed
    // back to the model that asked for it (see routeFetch/applyBrowseChildren).
    connect(m_treeModel, &OpcUaModel::fetchChildrenRequested, this,
            [this](const QString &parentNodeId, quint64 requestId) {
                routeFetch(m_treeModel, parentNodeId, requestId);
            });
    connect(m_focusModel, &OpcUaModel::fetchChildrenRequested, this,
            [this](const QString &parentNodeId, quint64 requestId) {
                routeFetch(m_focusModel, parentNodeId, requestId);
            });

    connect(this, &OpcUaManager::readAttributesRequested,
            service, &OpcUaService::readNodeAttributes, Qt::QueuedConnection);
    connect(this, &OpcUaManager::readStructuredValueRequested,
            service, &OpcUaService::readStructuredValue, Qt::QueuedConnection);
    connect(this, &OpcUaManager::subscribeNodeRequested,
            service, &OpcUaService::subscribeNode, Qt::QueuedConnection);
    connect(this, &OpcUaManager::unsubscribeNodeRequested,
            service, &OpcUaService::unsubscribeNode, Qt::QueuedConnection);
    connect(this, &OpcUaManager::writeValueRequested,
            service, &OpcUaService::writeNodeValue, Qt::QueuedConnection);

    connect(service, &OpcUaService::nodeAttributesReady,
            this, &OpcUaManager::applyNodeAttributes, Qt::QueuedConnection);
    connect(service, &OpcUaService::structuredValueReady,
            this, &OpcUaManager::applyStructuredValue, Qt::QueuedConnection);
    connect(service, &OpcUaService::monitoredValueChanged,
            this, &OpcUaManager::applyMonitoredValue, Qt::QueuedConnection);
    connect(service, &OpcUaService::writeCompleted,
            this, &OpcUaManager::applyWriteCompleted, Qt::QueuedConnection);

    emit initializeRequested();
}

/*!
 * \brief Mirrors backend plugin names from the worker service.
 * \param backends The available backend plugin names.
 */
void OpcUaManager::applyAvailableBackends(const QStringList &backends)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_availableBackends == backends)
        return;
    m_availableBackends = backends;
    locker.unlock();
    emit opcUaBackendChanged();
}

/*!
 * \brief Mirrors the selected backend from the worker service.
 * \param backend The backend plugin name.
 */
void OpcUaManager::applyBackend(const QString &backend)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_backend == backend)
        return;
    m_backend = backend;
    locker.unlock();
    emit backendChanged();
}

/*!
 * \brief Mirrors discovered server display rows from the worker service.
 * \param servers List of server display strings.
 */
void OpcUaManager::applyServers(const QStringList &servers)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_servers != servers) {
            m_servers = servers;
            changed = true;
        }
    }
    if (changed)
        emit serversChanged();

    if (m_reconnectStage != ReconnectStage::DiscoveringServers)
        return;

    // discoverServers() clears the list and emits an empty serversChanged() before
    // the network result arrives. That transient empty emission must not abort an
    // in-progress reconnect; wait for the populated result instead.
    if (servers.isEmpty())
        return;

    // Pick the stored server: exact display match first, then a substring match,
    // then the first server as a fallback.
    int index = servers.indexOf(m_reconnectServer);
    if (index < 0 && !m_reconnectServer.isEmpty()) {
        for (int i = 0; i < servers.size(); ++i) {
            if (servers.at(i).contains(m_reconnectServer)) {
                index = i;
                break;
            }
        }
    }
    if (index < 0 && !servers.isEmpty())
        index = 0;
    if (index < 0) {
        abortReconnect(tr("The stored server was not found during discovery."));
        return;
    }

    m_reconnectStage = ReconnectStage::RequestingEndpoints;
    requestEndpointsForServer(index);
}

/*!
 * \brief Mirrors endpoint display rows from the worker service.
 * \param endpoints List of endpoint display strings.
 */
void OpcUaManager::applyEndpoints(const QStringList &endpoints)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_endpoints != endpoints) {
            m_endpoints = endpoints;
            changed = true;
        }
    }
    if (changed)
        emit endpointsChanged();

    if (m_reconnectStage != ReconnectStage::RequestingEndpoints)
        return;

    // Endpoint discovery clears the list first, like server discovery; ignore the
    // transient empty emission and wait for the populated endpoint list.
    if (endpoints.isEmpty())
        return;

    // Pick the stored endpoint by exact display match, else the first endpoint.
    int index = endpoints.indexOf(m_reconnectEndpoint);
    if (index < 0 && !endpoints.isEmpty())
        index = 0;
    if (index < 0) {
        abortReconnect(tr("The stored endpoint was not found."));
        return;
    }

    m_reconnectStage = ReconnectStage::Connecting;
    connectToEndpoint(index);
}

/*!
 * \brief Mirrors connection state and updates the tree model activation state.
 * \param connected Whether the service is connected.
 */
void OpcUaManager::applyConnected(bool connected)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_connected == connected)
        return;
    m_connected = connected;
    locker.unlock();

    if (m_treeModel)
        m_treeModel->setConnectionActive(connected);

    if (connected) {
        // Seed the tree with the persisted monitored node ids before its browse
        // results arrive, so the monitoring checkbox is restored for those nodes.
        refreshMonitoredNodeIds();

        // Restore the pinned focus segment now that the session is active.
        applyFocusNodeToModel();

        // Capture the parameters that produced this successful connection into the
        // active project's connection config and mark any reconnect as finished.
        updateConnectionFromLiveState();
        m_reconnectStage = ReconnectStage::Idle;

        // Re-establish subscriptions for every persisted monitored node so the
        // Data Access View resumes updating after a (re)connect.
        const int rows = m_dataModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            const QString nodeId = m_dataModel->nodeIdAt(i);
            if (!nodeId.isEmpty())
                emit subscribeNodeRequested(nodeId);
        }
    } else {
        if (m_focusModel)
            m_focusModel->clear();
        // Drop routes for browses that will never return after the session ended.
        m_browseRouting.clear();
        m_dataModel->clearValues();
        m_attributesModel->clear();
    }

    emit connectedChanged();
}

/*!
 * \brief Mirrors operation state and emits busyChanged() when the derived busy value changes.
 * \param operationState The current operation state as an integer.
 */
void OpcUaManager::applyOperationState(int operationState)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_operationState == operationState)
        return;
    const bool wasBusy = m_operationState != OperationIdle;
    m_operationState = operationState;
    const bool isBusy = m_operationState != OperationIdle;
    locker.unlock();

    emit operationStateChanged();
    if (wasBusy != isBusy)
        emit busyChanged();
}

/*!
 * \brief Mirrors the underlying client state from the worker service.
 * \param clientState The current client state as an integer.
 */
void OpcUaManager::applyClientState(int clientState)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_clientState == clientState)
        return;
    m_clientState = clientState;
    locker.unlock();
    emit clientStateChanged();
}

/*!
 * \brief Mirrors endpoint URL rewrite state from the worker service.
 * \param enabled Whether endpoint URL rewriting is enabled.
 */
void OpcUaManager::applyEndpointUrlRewriteEnabled(bool enabled)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_endpointUrlRewriteEnabled == enabled)
        return;
    m_endpointUrlRewriteEnabled = enabled;
    locker.unlock();
    emit endpointUrlRewriteEnabledChanged();
}

/*!
 * \brief Mirrors the last user-visible error text from the worker service.
 * \param lastError The error text.
 */
void OpcUaManager::applyLastError(const QString &lastError)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_lastError == lastError)
        return;
    m_lastError = lastError;
    locker.unlock();

    // A real error during an automatic reconnect ends the run so the state machine
    // does not keep matching stale discovery results.
    if (!lastError.isEmpty()
        && m_reconnectStage != ReconnectStage::Idle
        && m_reconnectStage != ReconnectStage::AwaitingPassword) {
        m_reconnectStage = ReconnectStage::Idle;
    }

    emit lastErrorChanged();
}

/*!
 * \brief Mirrors the authentication mode from the worker service.
 * \param authMode The authentication mode as an integer.
 */
void OpcUaManager::applyAuthMode(int authMode)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_authMode == authMode)
        return;
    m_authMode = authMode;
    locker.unlock();
    emit authModeChanged();
}

/*!
 * \brief Forwards browse results from the worker service into the GUI tree model.
 * \param parentNodeId The parent node identifier.
 * \param requestId The request identifier.
 * \param children List of child node data.
 * \param success Whether the browse operation succeeded.
 */
void OpcUaManager::applyBrowseChildren(const QString &parentNodeId,
                                       quint64 requestId,
                                       const QList<OpcUaNodeData> &children,
                                       bool success)
{
    const auto it = m_browseRouting.find(requestId);
    if (it == m_browseRouting.end())
        return;

    const BrowseRoute route = it.value();
    m_browseRouting.erase(it);

    if (route.model)
        route.model->applyChildrenSnapshot(parentNodeId, route.modelRequestId, children, success);
}

/*!
 * \brief Applies node attributes for \a requestId to the Attributes panel model.
 * \param data The attribute snapshot read from the server.
 * \param success Whether the read succeeded.
 *
 * Stale results from superseded requests are ignored so the panel always shows
 * the attributes of the most recently selected node.
 */
void OpcUaManager::applyNodeAttributes(quint64 requestId,
                                       const OpcUaAttributeData &data,
                                       bool success)
{
    if (requestId != m_pendingAttributeRequestId)
        return;

    if (success)
        m_attributesModel->setAttributes(data);
    else
        m_attributesModel->clear();
}

/*!
 * \brief Applies the decoded value tree for \a requestId and \a nodeId to the View panel.
 * \param root The decoded value tree read from the server.
 * \param success Whether the read and decode succeeded.
 *
 * Stale results from superseded requests are ignored. The tree is cached so the
 * text can be re-rendered when the output format changes. A value is considered
 * available when it is a structure or array, or a scalar with a valid value, so
 * that non-variable nodes (folders and objects) leave the panel empty.
 */
void OpcUaManager::applyStructuredValue(quint64 requestId,
                                        const QString &nodeId,
                                        const OpcUaValueTreeNode &root,
                                        bool success)
{
    Q_UNUSED(nodeId)
    if (requestId != m_pendingStructuredRequestId)
        return;

    const bool hasContent = success
        && (root.kind != OpcUaValueTreeNode::Kind::Scalar || root.scalarValue.isValid());

    m_structuredValueRoot = root;
    m_structuredValueAvailable = hasContent;
    m_structuredValueText = hasContent
        ? StructuredValueFormatter::format(root, toFormatterFormat(m_valueFormat))
        : QString();
    emit structuredValueChanged();
}

/*!
 * \brief Applies a live value \a update to the Data Access View table model.
 */
void OpcUaManager::applyMonitoredValue(const OpcUaValueUpdate &update)
{
    m_dataModel->updateValue(update);
}

/*!
 * \brief Applies a write result for \a nodeId, surfacing \a error when it failed.
 * \param success Whether the write succeeded.
 *
 * Successful writes are reflected through the active subscription, so only
 * failures need to update the user-visible error text.
 */
void OpcUaManager::applyWriteCompleted(const QString &nodeId, bool success, const QString &error)
{
    if (success)
        return;

    qWarning() << "OpcUaManager: write failed for" << nodeId << ":" << error;
    applyLastError(error);
}
