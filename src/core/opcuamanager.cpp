#include <QMutexLocker>
#include <QDebug>
#include "opcuaservice.h"
#include "opcuamanager.h"

/*!
 * \brief Creates the QML-facing OPC UA facade.
 * \details The facade owns GUI-thread state and the browse model. The actual
 * OPC UA service is attached later by AppEngine.
 */
OpcUaManager::OpcUaManager(const QString &initialUrl, QObject *parent)
    : QObject(parent)
    , m_initialUrl(initialUrl)
    , m_treeModel(new OpcUaModel(this))
{
}

/*!
 * \brief Destroys the facade and its QObject-owned model.
 */
OpcUaManager::~OpcUaManager() = default;

/*! \brief Returns available OPC UA backend names. */
QStringList OpcUaManager::opcUaBackend() const { QMutexLocker locker(&m_stateMutex); return m_availableBackends; }

/*! \brief Returns the selected backend name. */
QString OpcUaManager::backend() const { QMutexLocker locker(&m_stateMutex); return m_backend; }

/*! \brief Returns server rows displayed by QML. */
QStringList OpcUaManager::servers() const { QMutexLocker locker(&m_stateMutex); return m_servers; }

/*! \brief Returns endpoint rows displayed by QML. */
QStringList OpcUaManager::endpoints() const { QMutexLocker locker(&m_stateMutex); return m_endpoints; }

/*! \brief Returns whether an OPC UA session is connected. */
bool OpcUaManager::connected() const { QMutexLocker locker(&m_stateMutex); return m_connected; }

/*! \brief Returns the GUI-thread browse model. */
OpcUaModel *OpcUaManager::treeModel() const { return m_treeModel; }

/*! \brief Returns the latest user-visible OPC UA error. */
QString OpcUaManager::lastError() const { QMutexLocker locker(&m_stateMutex); return m_lastError; }

/*! \brief Returns the selected authentication mode. */
int OpcUaManager::authMode() const { QMutexLocker locker(&m_stateMutex); return m_authMode; }

/*! \brief Requests a backend change in the OPC UA service. */
void OpcUaManager::setBackend(const QString &backend) { emit setBackendRequested(backend); }

/*! \brief Requests anonymous authentication. */
void OpcUaManager::setAnonymousAuthentication() { emit setAnonymousAuthenticationRequested(); }

/*! \brief Requests username/password authentication. */
void OpcUaManager::setUsernameAuthentication(const QString &userName, const QString &password) { emit setUsernameAuthenticationRequested(userName, password); }

/*! \brief Requests certificate authentication. */
void OpcUaManager::setCertificateAuthentication() { emit setCertificateAuthenticationRequested(); }

/*!
 * \brief Starts server discovery through the OPC UA service.
 * \details Empty user input falls back to the initial URL passed at application
 * startup.
 */
void OpcUaManager::discoverServers(const QString &hostOrUrl)
{
    const QString effectiveUrl = hostOrUrl.trimmed().isEmpty() ? m_initialUrl : hostOrUrl;
    qInfo() << "OpcUaManager discoverServers requested:" << effectiveUrl;
    emit discoverServersRequested(effectiveUrl);
}

/*! \brief Starts endpoint discovery for \a serverUrl. */
void OpcUaManager::requestEndpoints(const QString &serverUrl)
{
    qInfo() << "OpcUaManager requestEndpoints requested:" << serverUrl;
    emit requestEndpointsRequested(serverUrl);
}

/*! \brief Starts endpoint discovery for the selected server row. */
void OpcUaManager::requestEndpointsForServer(int serverIndex)
{
    qInfo() << "OpcUaManager requestEndpointsForServer requested. index:" << serverIndex;
    emit requestEndpointsForServerRequested(serverIndex);
}

/*! \brief Requests connection to the selected endpoint row. */
void OpcUaManager::connectToEndpoint(int endpointIndex)
{
    qInfo() << "OpcUaManager connectToEndpoint requested. index:" << endpointIndex;
    emit connectToEndpointRequested(endpointIndex);
}

/*! \brief Requests disconnect from the current server. */
void OpcUaManager::disconnectFromServer()
{
    qInfo() << "OpcUaManager disconnectFromServer requested.";
    emit disconnectFromServerRequested();
}

/*!
 * \brief Attaches the OPC UA service signal/slot bridge.
 * \details This method wires the QML facade to the OPC UA service. The service
 * lives in the OPC UA worker thread, so all command and result traffic crosses
 * the thread boundary through queued signal-slot delivery.
 */
void OpcUaManager::attachService(OpcUaService *service)
{
    if (!service || m_service == service)
        return;
    m_service = service;

    connect(this, &OpcUaManager::initializeRequested, service, &OpcUaService::initialize, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setBackendRequested, service, &OpcUaService::setBackend, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setAnonymousAuthenticationRequested, service, &OpcUaService::setAnonymousAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setUsernameAuthenticationRequested, service, &OpcUaService::setUsernameAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setCertificateAuthenticationRequested, service, &OpcUaService::setCertificateAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::discoverServersRequested, service, &OpcUaService::discoverServers, Qt::QueuedConnection);
    connect(this, &OpcUaManager::requestEndpointsRequested, service, &OpcUaService::requestEndpoints, Qt::QueuedConnection);
    connect(this, &OpcUaManager::requestEndpointsForServerRequested, service, &OpcUaService::requestEndpointsForServer, Qt::QueuedConnection);
    connect(this, &OpcUaManager::connectToEndpointRequested, service, &OpcUaService::connectToEndpoint, Qt::QueuedConnection);
    connect(this, &OpcUaManager::disconnectFromServerRequested, service, &OpcUaService::disconnectFromServer, Qt::QueuedConnection);
    connect(this, &OpcUaManager::browseChildrenRequested, service, &OpcUaService::browseChildren, Qt::QueuedConnection);

    connect(service, &OpcUaService::availableBackendsChanged, this, &OpcUaManager::applyAvailableBackends, Qt::QueuedConnection);
    connect(service, &OpcUaService::backendChanged, this, &OpcUaManager::applyBackend, Qt::QueuedConnection);
    connect(service, &OpcUaService::serversChanged, this, &OpcUaManager::applyServers, Qt::QueuedConnection);
    connect(service, &OpcUaService::endpointsChanged, this, &OpcUaManager::applyEndpoints, Qt::QueuedConnection);
    connect(service, &OpcUaService::connectedChanged, this, &OpcUaManager::applyConnected, Qt::QueuedConnection);
    connect(service, &OpcUaService::lastErrorChanged, this, &OpcUaManager::applyLastError, Qt::QueuedConnection);
    connect(service, &OpcUaService::authModeChanged, this, &OpcUaManager::applyAuthMode, Qt::QueuedConnection);
    connect(service, &OpcUaService::browseChildrenReady, this, &OpcUaManager::applyBrowseChildren, Qt::QueuedConnection);
    connect(m_treeModel, &OpcUaModel::fetchChildrenRequested, this, &OpcUaManager::browseChildrenRequested, Qt::QueuedConnection);

    emit initializeRequested();
}

void OpcUaManager::applyAvailableBackends(const QStringList &backends) { QMutexLocker locker(&m_stateMutex); if (m_availableBackends == backends) return; m_availableBackends = backends; locker.unlock(); qInfo() << "OpcUaManager available backends updated:" << backends; emit opcUaBackendChanged(); }
void OpcUaManager::applyBackend(const QString &backend) { QMutexLocker locker(&m_stateMutex); if (m_backend == backend) return; m_backend = backend; locker.unlock(); qInfo() << "OpcUaManager backend updated:" << backend; emit backendChanged(); }
void OpcUaManager::applyServers(const QStringList &servers) { QMutexLocker locker(&m_stateMutex); if (m_servers == servers) return; m_servers = servers; locker.unlock(); qInfo() << "OpcUaManager servers updated. count:" << servers.size() << "servers:" << servers; emit serversChanged(); }
void OpcUaManager::applyEndpoints(const QStringList &endpoints) { QMutexLocker locker(&m_stateMutex); if (m_endpoints == endpoints) return; m_endpoints = endpoints; locker.unlock(); qInfo() << "OpcUaManager endpoints updated. count:" << endpoints.size() << "endpoints:" << endpoints; emit endpointsChanged(); }

/*!
 * \brief Applies the OPC UA service connection state.
 * \details The GUI-thread model owns only snapshots, so it is safe to keep the
 * model alive in the GUI thread and simply toggle whether a live session is
 * available for lazy browse requests.
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

void OpcUaManager::applyLastError(const QString &lastError) { QMutexLocker locker(&m_stateMutex); if (m_lastError == lastError) return; m_lastError = lastError; locker.unlock(); qInfo() << "OpcUaManager lastError updated:" << lastError; emit lastErrorChanged(); }
void OpcUaManager::applyAuthMode(int authMode) { QMutexLocker locker(&m_stateMutex); if (m_authMode == authMode) return; m_authMode = authMode; locker.unlock(); qInfo() << "OpcUaManager auth mode updated:" << authMode; emit authModeChanged(); }

/*!
 * \brief Applies lazy browse children in the GUI model.
 * \details This is the adapter step between the OPC UA service and the
 * GUI-thread tree model. The service sends immutable node snapshots, and the
 * facade inserts them into OpcUaModel without exposing protocol objects.
 */
void OpcUaManager::applyBrowseChildren(const QString &parentNodeId, const QList<OpcUaNodeData> &children)
{
    if (m_treeModel)
        m_treeModel->applyChildrenSnapshot(parentNodeId, children);
}
