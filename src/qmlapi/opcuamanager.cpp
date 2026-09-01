#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMutexLocker>
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
    , m_dataModel(new DataAccessModel(this))
    , m_attributesModel(new AttributesModel(this))
    , m_nodeDatabase(std::make_unique<NodeDatabase>())
{
    // The database ships in a db/ folder next to the executable so that monitored
    // nodes persist across runs. It is created on first use if it does not exist.
    const QString databasePath =
        QCoreApplication::applicationDirPath() + QLatin1String("/db/opcua_nodes.db");
    if (m_nodeDatabase->open(databasePath))
        m_dataModel->setRecords(m_nodeDatabase->loadAll());
    else
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
    if (!m_treeModel || !treeIndex.isValid())
        return {};

    QStringList segments;
    for (QModelIndex index = treeIndex; index.isValid(); index = index.parent()) {
        const QString name = m_treeModel->data(index, OpcUaModel::DisplayNameRole).toString();
        if (!name.isEmpty())
            segments.prepend(name);
    }
    return segments.join(QLatin1Char('/'));
}

/*!
 * \brief Adds or removes the node at \a treeIndex from the Data Access View.
 * \param on Whether the node should be monitored and persisted.
 *
 * Adding inserts the node into the database and the table and starts a live
 * subscription when connected. Removing reverses all three steps.
 */
void OpcUaManager::setNodeMonitored(const QModelIndex &treeIndex, bool on)
{
    if (!m_treeModel || !treeIndex.isValid())
        return;

    const QString nodeId = m_treeModel->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    const QString server = m_currentServer.isEmpty() ? m_initialUrl : m_currentServer;

    if (on) {
        MonitoredNodeRecord record;
        record.server = server;
        record.nodeId = nodeId;
        record.nodePath = buildNodePath(treeIndex);
        record.displayName = m_treeModel->data(treeIndex, OpcUaModel::DisplayNameRole).toString();
        record.dataType = m_treeModel->data(treeIndex, OpcUaModel::DataTypeRole).toString();

        if (m_nodeDatabase)
            m_nodeDatabase->insert(record);
        m_dataModel->addRow(record);
        m_treeModel->setMonitoringEnabledAt(treeIndex, true);
        if (connected())
            emit subscribeNodeRequested(nodeId);
    } else {
        if (m_nodeDatabase)
            m_nodeDatabase->remove(server, nodeId);
        const int row = m_dataModel->rowCount();
        for (int i = 0; i < row; ++i) {
            if (m_dataModel->nodeIdAt(i) == nodeId) {
                m_dataModel->removeAt(i);
                break;
            }
        }
        m_treeModel->setMonitoringEnabledAt(treeIndex, false);
        emit unsubscribeNodeRequested(nodeId);
    }
}

/*!
 * \brief Removes the Data Access View row at \a row from the table and database.
 */
void OpcUaManager::removeNode(int row)
{
    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    const QString server = m_dataModel->serverAt(row);
    if (m_nodeDatabase)
        m_nodeDatabase->remove(server, nodeId);
    m_dataModel->removeAt(row);
    emit unsubscribeNodeRequested(nodeId);
}

/*!
 * \brief Requests the attributes of the node at \a treeIndex for the panel.
 *
 * Only the most recent request is applied; earlier in-flight results are ignored
 * so that fast selection changes do not show stale attributes.
 */
void OpcUaManager::requestAttributes(const QModelIndex &treeIndex)
{
    if (!m_treeModel || !treeIndex.isValid())
        return;

    const QString nodeId = m_treeModel->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    const quint64 requestId = ++m_nextAttributeRequestId;
    m_pendingAttributeRequestId = requestId;
    m_pendingStructuredRequestId = requestId;
    m_selectedNodeId = nodeId;
    emit readAttributesRequested(nodeId, requestId);
    emit readStructuredValueRequested(nodeId, requestId);
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
    connect(m_treeModel, &OpcUaModel::fetchChildrenRequested,
            this, &OpcUaManager::browseChildrenRequested, Qt::QueuedConnection);

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
    QMutexLocker locker(&m_stateMutex);
    if (m_servers == servers)
        return;
    m_servers = servers;
    locker.unlock();
    emit serversChanged();
}

/*!
 * \brief Mirrors endpoint display rows from the worker service.
 * \param endpoints List of endpoint display strings.
 */
void OpcUaManager::applyEndpoints(const QStringList &endpoints)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_endpoints == endpoints)
        return;
    m_endpoints = endpoints;
    locker.unlock();
    emit endpointsChanged();
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
        // Re-establish subscriptions for every persisted monitored node so the
        // Data Access View resumes updating after a (re)connect.
        const int rows = m_dataModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            const QString nodeId = m_dataModel->nodeIdAt(i);
            if (!nodeId.isEmpty())
                emit subscribeNodeRequested(nodeId);
        }
    } else {
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
    if (m_treeModel)
        m_treeModel->applyChildrenSnapshot(parentNodeId, requestId, children, success);
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
