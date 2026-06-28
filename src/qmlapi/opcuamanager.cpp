#include <QDebug>
#include <QMutexLocker>

#include "core/opcuaservice.h"
#include "opcuamanager.h"

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
{
}

OpcUaManager::~OpcUaManager() = default;

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
    emit requestEndpointsRequested(serverUrl);
}

void OpcUaManager::requestEndpointsForServer(int serverIndex)
{
    qInfo() << "OpcUaManager requestEndpointsForServer requested. index:" << serverIndex;
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
