#include "opcuaservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QMetaEnum>
#include <QMetaObject>
#include <QOpcUaNode>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <algorithm>

using namespace Qt::Literals::StringLiterals;

/*! \internal \brief Copies missing files and child directories from \a from to \a to. */
static bool copyDirRecursively(const QString &from, const QString &to)
{
    const QDir srcDir(from);
    const QDir dstDir(to);

    if (!srcDir.exists())
        return false;
    if (!QDir().mkpath(to))
        return false;

    const QFileInfoList infos = srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : infos) {
        const QString srcItemPath = info.absoluteFilePath();
        const QString dstItemPath = dstDir.absoluteFilePath(info.fileName());
        if (info.isDir()) {
            if (!copyDirRecursively(srcItemPath, dstItemPath))
                return false;
        } else if (info.isFile()) {
            if (QFile::exists(dstItemPath))
                continue;
            if (!QFile::copy(srcItemPath, dstItemPath))
                return false;
        }
    }
    return true;
}

/*! \internal \brief Converts \a s into a safe token for stable OPC UA URN components. */
static QString sanitizeUrnToken(QString s)
{
    s = s.trimmed();
    if (s.isEmpty())
        return QStringLiteral("unknown");

    for (QChar &ch : s) {
        const bool ok = ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_') || ch == QLatin1Char('.');
        if (!ok)
            ch = QLatin1Char('-');
    }

    while (s.contains(QStringLiteral("--")))
        s.replace(QStringLiteral("--"), QStringLiteral("-"));

    return s;
}

/*!
 * \internal
 * \brief Loads or creates the stable application URI used by non-certificate authentication.
 * The generated URI is persisted in QSettings so servers can recognize
 * the same client application identity across restarts.
 */
static QString loadOrCreateStableApplicationUri()
{
    QSettings settings;
    const QString key = QStringLiteral("opcua/clientApplicationUri");

    const QString savedUri = settings.value(key).toString().trimmed();
    if (!savedUri.isEmpty())
        return savedUri;

    QString orgDomain = QCoreApplication::organizationDomain().trimmed();
    if (orgDomain.isEmpty())
        orgDomain = QStringLiteral("local");
    orgDomain = sanitizeUrnToken(orgDomain);

    QString appName = QCoreApplication::applicationName().trimmed();
    if (appName.isEmpty())
        appName = QStringLiteral("OpcUaBrowser");
    const QString appNameUrn = sanitizeUrnToken(appName);

    const QString host = sanitizeUrnToken(QHostInfo::localHostName());
    const QString installId = QUuid::createUuid().toString(QUuid::WithoutBraces);

#ifdef Q_OS_WIN
    const QString osTag = QStringLiteral("win");
#else
    const QString osTag = QStringLiteral("linux");
#endif

    const QString uri = QStringLiteral("urn:%1:%2:%3-%4-%5").arg(orgDomain, appNameUrn, osTag, host, installId);
    settings.setValue(key, uri);
    settings.sync();
    return uri;
}

/*!
 * \brief Creates the OPC UA service.
 * The object is expected to be moved to the OPC UA worker thread
 * before initialization. All Qt OPC UA operations remain asynchronous and
 * report results through signals.
 */
OpcUaService::OpcUaService(const QString &initialUrl, QObject *parent)
    : QObject(parent)
    , m_initialUrl(initialUrl)
{
}

/*!
 * \brief Releases OPC UA runtime objects owned by the service.
 * Timers, QOpcUaClient, QOpcUaProvider, and protocol helper objects
 * are created and destroyed in the same thread as the service.
 */
OpcUaService::~OpcUaService()
{
    m_genericStructHandler.reset();

    if (m_findServersTimeoutTimer) {
        m_findServersTimeoutTimer->stop();
        delete m_findServersTimeoutTimer;
        m_findServersTimeoutTimer = nullptr;
    }

    if (m_endpointsTimeoutTimer) {
        m_endpointsTimeoutTimer->stop();
        delete m_endpointsTimeoutTimer;
        m_endpointsTimeoutTimer = nullptr;
    }

    if (m_client) {
        m_genericStructHandler.reset();
        m_client->disconnectFromEndpoint();
        delete m_client;
        m_client = nullptr;
    }

    delete m_provider;
    m_provider = nullptr;
}

bool OpcUaService::isInObjectThread() const
{
    return thread() == QThread::currentThread();
}

/*!
 * \brief Initializes backend discovery, PKI paths, timers, and authentication.
 * This method is idempotent. It selects the first available Qt OPC UA
 * backend but delays QOpcUaClient creation until an operation needs it.
 */
void OpcUaService::initialize()
{
    if (!isInObjectThread()) {
        QMetaObject::invokeMethod(this, &OpcUaService::initialize, Qt::BlockingQueuedConnection);
        return;
    }

    if (m_initialized) {
        qInfo() << "OpcUaService initialization skipped because it already completed.";
        return;
    }

    qInfo() << "OpcUaService initialization started in OpcUaThread.";

    m_provider = new QOpcUaProvider;
    m_availableBackends = m_provider->availableBackends();
    qInfo() << "Available OPC UA backends:" << m_availableBackends;
    emit availableBackendsChanged(m_availableBackends);

    m_backendProperties.clear();

    setupPkiConfiguration();

    m_findServersTimeoutTimer = new QTimer;
    m_findServersTimeoutTimer->setSingleShot(true);
    m_findServersTimeoutTimer->setTimerType(Qt::PreciseTimer);
    connect(m_findServersTimeoutTimer, &QTimer::timeout, m_findServersTimeoutTimer, [this]() {
        handleFindServersTimeout();
    });

    m_endpointsTimeoutTimer = new QTimer;
    m_endpointsTimeoutTimer->setSingleShot(true);
    m_endpointsTimeoutTimer->setTimerType(Qt::PreciseTimer);

    connect(m_endpointsTimeoutTimer,
            &QTimer::timeout,
            this,
            &OpcUaService::handleEndpointsTimeout);

    m_initialized = true;
    emit operationStateChanged(int(m_operationState));
    emit clientStateChanged(int(QOpcUaClient::ClientState::Disconnected));
    emit endpointUrlRewriteEnabledChanged(m_endpointUrlRewriteEnabled);

    setAnonymousAuthentication();

    if (!m_availableBackends.isEmpty()) {
        m_backend = m_availableBackends.first();
        emit backendChanged(m_backend);
        qInfo() << "Default OPC UA backend selected without creating a client:" << m_backend;
    } else {
        qWarning() << "No OPC UA backends are available. Check QtOpcUa plugin deployment.";
        setLastError(QStringLiteral("No OPC UA backends available. Check QtOpcUa plugins installation."));
    }

    qInfo() << "OpcUaService initialization finished.";
}

/*!
 * \brief Selects a Qt OPC UA backend.
 * Changing the backend tears down the current client and clears
 * pending discovery/endpoint requests before a new client is created.
 */
void OpcUaService::setBackend(const QString &backend)
{
    initialize();

    const QString selectedBackend = backend.trimmed();
    if (selectedBackend.isEmpty() || selectedBackend == m_backend)
        return;

    m_findServersRequestPending = false;
    m_endpointsRequestPending = false;
    if (m_findServersTimeoutTimer)
        m_findServersTimeoutTimer->stop();
    if (m_endpointsTimeoutTimer)
        m_endpointsTimeoutTimer->stop();
    setOperationState(OperationState::Idle);

    if (m_client) {
        m_genericStructHandler.reset();
        m_client->disconnectFromEndpoint();
        delete m_client;
        m_client = nullptr;
        m_identityApplied = false;
        if (m_clientConnected) {
            m_clientConnected = false;
            emit connectedChanged(m_clientConnected);
        }
        emit clientStateChanged(int(QOpcUaClient::ClientState::Disconnected));
    }

    invalidateEndpointCache();
    m_endpointList.clear();
    m_endpointsDisplay.clear();
    emit endpointsChanged(m_endpointsDisplay);

    m_backend = selectedBackend;
    emit backendChanged(m_backend);
    createClient();
}

/*!
 * \brief Configures anonymous authentication for future endpoint requests.
 */
void OpcUaService::setAnonymousAuthentication()
{
    initialize();
    QOpcUaAuthenticationInformation info;
    m_authInfo = info;
    m_identityApplied = false;
    invalidateEndpointCache();
    setLastError(QString());
    emit authModeChanged(int(m_authInfo.authenticationType()));
    if (m_client)
        queueClientSecurityConfiguration();
}

/*!
 * \brief Configures username/password authentication.
 */
void OpcUaService::setUsernameAuthentication(const QString &userName, const QString &password)
{
    initialize();

    if (userName.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Username authentication requires a user name."));
        return;
    }

    QOpcUaAuthenticationInformation info;
    info.setUsernameAuthentication(userName, password);
    m_authInfo = info;
    m_identityApplied = false;
    invalidateEndpointCache();
    setLastError(QString());
    emit authModeChanged(int(m_authInfo.authenticationType()));
    if (m_client)
        queueClientSecurityConfiguration();
}

/*!
 * \brief Configures certificate authentication.
 */
void OpcUaService::setCertificateAuthentication()
{
    initialize();

    // Select the requested mode before validation. A subsequently queued
    // endpoint or connect request will therefore fail closed instead of
    // silently continuing with the previously selected authentication mode.
    QOpcUaAuthenticationInformation info;
    info.setCertificateAuthentication();
    m_authInfo = info;
    m_identityApplied = false;
    invalidateEndpointCache();
    emit authModeChanged(int(m_authInfo.authenticationType()));

    QString validationError;
    if (!validateCertificateConfiguration(&validationError)) {
        setLastError(validationError);
        return;
    }

    setLastError(QString());
    if (m_client)
        queueClientSecurityConfiguration();
}

/*!
 * \brief Stores the password used when the configured private key is encrypted.
 */
void OpcUaService::setCertificatePrivateKeyPassword(const QString &password)
{
    m_certificatePrivateKeyPassword = password;
}

/*!
 * \brief Enables the optional endpoint host and port rewrite workaround.
 */
void OpcUaService::setEndpointUrlRewriteEnabled(bool enabled)
{
    if (m_endpointUrlRewriteEnabled == enabled)
        return;

    m_endpointUrlRewriteEnabled = enabled;
    invalidateEndpointCache();
    emit endpointUrlRewriteEnabledChanged(m_endpointUrlRewriteEnabled);
}

/*!
 * \brief Starts asynchronous OPC UA server discovery for \a hostOrUrl.
 * The service keeps a timeout around FindServers because some
 * backends or server configurations dispatch the request without later
 * emitting findServersFinished. Endpoint lookup remains disabled until an
 * actual FindServers result is received and published.
 */
void OpcUaService::discoverServers(const QString &hostOrUrl)
{
    initialize();
    m_findServersRequestPending = false;
    if (m_findServersTimeoutTimer)
        m_findServersTimeoutTimer->stop();
    setOperationState(OperationState::DiscoveringServers);
    m_servers.clear();
    emit serversChanged(m_servers);
    m_endpointList.clear();
    m_endpointsDisplay.clear();
    emit endpointsChanged(m_endpointsDisplay);
    m_serverCandidateUrls.clear();
    setLastError(QString());

    const QString input = hostOrUrl.trimmed().isEmpty() ? m_initialUrl : hostOrUrl;
    const QUrl url = normalizeDiscoveryUrl(input);
    qInfo() << "OpcUaService discoverServers started."
            << "input:" << input
            << "normalized URL:" << url;
    if (!url.isValid()) {
        qWarning() << "OpcUaService discoverServers rejected invalid URL:" << input;
        setLastError(QStringLiteral("Invalid discovery URL: %1").arg(input));
        setOperationState(OperationState::Idle);
        return;
    }

    createClient();
    if (!m_client) {
        setOperationState(OperationState::Idle);
        return;
    }

    m_lastFindServersRequestUrl = url;
    m_serverDescriptions.clear();

    m_findServersRequestPending = true;
    if (m_findServersTimeoutTimer)
        m_findServersTimeoutTimer->start(m_findServersRequestTimeoutMs);

    const QPointer<QOpcUaClient> client(m_client);
    const QPointer<OpcUaService> service(this);
    const bool queued = QMetaObject::invokeMethod(m_client,
                                                  [client, service, url]() {
                                                      if (!client || !service)
                                                          return;

                                                      qInfo() << "OpcUaService findServers dispatch executing."
                                                              << "current thread:" << QThread::currentThread()
                                                              << "client thread:" << client->thread();
                                                      const bool dispatched = client->findServers(url);
                                                      QMetaObject::invokeMethod(service,
                                                                                [service, dispatched, url]() {
                                                                                    if (service)
                                                                                        service->handleFindServersDispatchResult(dispatched, url);
                                                                                },
                                                                                Qt::QueuedConnection);
                                                  },
                                                  Qt::QueuedConnection);
    qInfo() << "OpcUaService findServers dispatch queued:" << queued
            << "url:" << url
            << "client thread:" << m_client->thread()
            << "service thread:" << thread();
    if (!queued) {
        m_findServersRequestPending = false;
        if (m_findServersTimeoutTimer)
            m_findServersTimeoutTimer->stop();
        setLastError(QStringLiteral("findServers() failed to queue for %1").arg(url.toString()));
        setOperationState(OperationState::Idle);
    }
}

/*!
 * \brief Starts asynchronous GetEndpoints for \a serverUrl.
 * Endpoint results are cached by discovery URL and authentication
 * mode so repeated requests can update the UI without another network round
 * trip when the same endpoint is still valid.
 */
void OpcUaService::requestEndpoints(const QString &serverUrl)
{
    initialize();
    m_endpointsRequestPending = false;
    if (m_endpointsTimeoutTimer)
        m_endpointsTimeoutTimer->stop();
    setOperationState(OperationState::RequestingEndpoints);
    m_endpointList.clear();
    if (!m_endpointsDisplay.isEmpty()) {
        m_endpointsDisplay.clear();
        emit endpointsChanged(m_endpointsDisplay);
    }
    setLastError(QString());

    const QUrl url = normalizeDiscoveryUrl(serverUrl);
    qInfo() << "OpcUaService requestEndpoints started."
            << "input:" << serverUrl
            << "normalized URL:" << url;
    if (!url.isValid()) {
        qWarning() << "OpcUaService requestEndpoints rejected invalid URL:" << serverUrl;
        setLastError(QStringLiteral("Invalid server URL: %1").arg(serverUrl));
        setOperationState(OperationState::Idle);
        return;
    }

    createClient();
    if (!m_client) {
        setOperationState(OperationState::Idle);
        return;
    }

    if (!queueClientSecurityConfiguration()) {
        setOperationState(OperationState::Idle);
        return;
    }
    m_lastEndpointsRequestUrl = url;
    m_endpointsRequestPending = true;

    if (m_endpointCacheValid
        && m_endpointCacheDiscoveryUrl == url
        && m_endpointCacheAuthType == m_authInfo.authenticationType()) {
        getEndpointsComplete(m_cachedEndpoints, QOpcUa::UaStatusCode::Good, url);
        return;
    }

    if (m_endpointsTimeoutTimer)
        m_endpointsTimeoutTimer->start(m_endpointsRequestTimeoutMs);

    const QPointer<QOpcUaClient> client(m_client);
    const QPointer<OpcUaService> service(this);
    const bool queued = QMetaObject::invokeMethod(m_client,
                                                  [client, service, url]() {
                                                      if (!client || !service)
                                                          return;

                                                      qInfo() << "OpcUaService requestEndpoints dispatch executing."
                                                              << "current thread:" << QThread::currentThread()
                                                              << "client thread:" << client->thread();
                                                      const bool dispatched = client->requestEndpoints(url);
                                                      QMetaObject::invokeMethod(service,
                                                                                [service, dispatched, url]() {
                                                                                    if (service)
                                                                                        service->handleEndpointsDispatchResult(dispatched, url);
                                                                                },
                                                                                Qt::QueuedConnection);
                                                  },
                                                  Qt::QueuedConnection);
    qInfo() << "OpcUaService requestEndpoints dispatch queued:" << queued
            << "url:" << url
            << "client thread:" << m_client->thread()
            << "service thread:" << thread();
    if (!queued) {
        m_endpointsRequestPending = false;
        if (m_endpointsTimeoutTimer)
            m_endpointsTimeoutTimer->stop();
        setLastError(QStringLiteral("requestEndpoints() failed to queue for %1").arg(url.toString()));
        setOperationState(OperationState::Idle);
    }
}

/*!
 * \brief Starts endpoint discovery for a server selected by UI index.
 */
void OpcUaService::requestEndpointsForServer(int serverIndex)
{
    const QString url = serverUrlAt(serverIndex);
    qInfo() << "OpcUaService requestEndpointsForServer."
            << "index:" << serverIndex
            << "url:" << url
            << "known server count:" << m_serverCandidateUrls.size();
    if (url.isEmpty()) {
        setLastError(QStringLiteral("Selected server has no usable discovery URL."));
        return;
    }
    requestEndpoints(url);
}

/*!
 * \brief Connects to the selected endpoint or disconnects an active session.
 */
void OpcUaService::connectToEndpoint(int endpointIndex)
{
    initialize();
    setLastError(QString());
    setOperationState(OperationState::Connecting);

    if (!m_client)
        createClient();
    if (!m_client) {
        setOperationState(OperationState::Idle);
        return;
    }
    if (m_clientConnected) {
        disconnectFromServer();
        return;
    }
    if (endpointIndex < 0 || endpointIndex >= m_endpointList.size()) {
        qWarning() << "OpcUaService connectToEndpoint rejected invalid index."
                   << "index:" << endpointIndex
                   << "endpoint count:" << m_endpointList.size();
        setLastError(QStringLiteral("Endpoint index out of range: %1").arg(endpointIndex));
        setOperationState(OperationState::Idle);
        return;
    }
    const QOpcUaEndpointDescription endpoint = m_endpointList.at(endpointIndex);
    qInfo() << "OpcUaService connectToEndpoint."
            << "index:" << endpointIndex
            << "url:" << endpoint.endpointUrl()
            << "security policy:" << endpoint.securityPolicy()
            << "security mode:" << int(endpoint.securityMode())
            << "auth modes:" << endpointAuthModesToString(endpoint);
    if (!endpointSupportsAuth(endpoint)) {
        setLastError(QStringLiteral("Selected endpoint does not support current authentication mode."));
        setOperationState(OperationState::Idle);
        return;
    }
    m_genericStructHandler.reset();
    if (!queueClientSecurityConfiguration()) {
        setOperationState(OperationState::Idle);
        return;
    }

    const QPointer<QOpcUaClient> client(m_client);
    const bool queued = QMetaObject::invokeMethod(m_client,
                                                  [client, endpoint]() {
                                                      if (client)
                                                          client->connectToEndpoint(endpoint);
                                                  },
                                                  Qt::QueuedConnection);
    if (!queued) {
        setLastError(QStringLiteral("connectToEndpoint() failed to queue for %1").arg(endpoint.endpointUrl()));
        setOperationState(OperationState::Idle);
    }
}

/*!
 * \brief Requests asynchronous disconnect from the current endpoint.
 */
void OpcUaService::disconnectFromServer()
{
    qInfo() << "OpcUaService disconnectFromServer requested.";
    if (!m_client) {
        setOperationState(OperationState::Idle);
        return;
    }

    setOperationState(OperationState::Disconnecting);
    const QPointer<QOpcUaClient> client(m_client);
    const bool queued = QMetaObject::invokeMethod(m_client,
                                                  [client]() {
                                                      if (client)
                                                          client->disconnectFromEndpoint();
                                                  },
                                                  Qt::QueuedConnection);
    if (!queued) {
        setLastError(QStringLiteral("disconnectFromEndpoint() failed to queue."));
        setOperationState(OperationState::Idle);
    }
}

/*!
 * \brief Browses direct children for \a parentNodeId.
 * The result is converted into OpcUaNodeData snapshots before being
 * emitted to the GUI model. QOpcUaNode instances remain private to this
 * service and are deleted after the browse completes.
 */
void OpcUaService::browseChildren(const QString &parentNodeId, quint64 requestId)
{
    if (!isInObjectThread()) {
        QMetaObject::invokeMethod(this,
                                  [this, parentNodeId, requestId]() { browseChildren(parentNodeId, requestId); },
                                  Qt::QueuedConnection);
        return;
    }

    initialize();
    const QString effectiveParentNodeId = parentNodeId.trimmed().isEmpty()
        ? QStringLiteral("ns=0;i=84")
        : parentNodeId;

    if (!m_clientConnected || !m_client) {
        setLastError(QStringLiteral("Browse requires an active OPC UA connection."));
        emit browseChildrenReady(effectiveParentNodeId, requestId, {}, false);
        return;
    }

    QOpcUaNode *node = m_client->node(effectiveParentNodeId);
    if (!node) {
        setLastError(QStringLiteral("Failed to create node for %1").arg(effectiveParentNodeId));
        emit browseChildrenReady(effectiveParentNodeId, requestId, {}, false);
        return;
    }

    if (node->thread() != thread())
        node->moveToThread(thread());

    node->setParent(this);

    connect(node,
            &QOpcUaNode::browseFinished,
            this,
            [this, node, effectiveParentNodeId, requestId](const QList<QOpcUaReferenceDescription> &children,
                                                           QOpcUa::UaStatusCode statusCode) {
                QList<OpcUaNodeData> snapshotChildren;
                const bool success = statusCode == QOpcUa::UaStatusCode::Good;
                if (success) {
                    snapshotChildren.reserve(children.size());
                    for (const auto &ref : children) {
                        OpcUaNodeData child;
                        child.nodeId = ref.targetNodeId().nodeId();
                        child.browseName = ref.browseName().name();
                        child.displayName = ref.displayName().text();
                        child.nodeClass = int(ref.nodeClass());
                        child.hasChildren = ref.nodeClass() != QOpcUa::NodeClass::Method;
                        snapshotChildren.push_back(child);
                    }
                } else {
                    setLastError(QStringLiteral("Browse failed for %1: %2")
                                     .arg(effectiveParentNodeId,
                                          QOpcUa::statusToString(statusCode)));
                }
                emit browseChildrenReady(effectiveParentNodeId, requestId, snapshotChildren, success);
                node->deleteLater();
            });

    if (!node->browseChildren()) {
        setLastError(QStringLiteral("browseChildren() failed to dispatch for %1")
                         .arg(effectiveParentNodeId));
        node->deleteLater();
        emit browseChildrenReady(effectiveParentNodeId, requestId, {}, false);
    }
}

/*!
 * \brief Normalizes a host or URL input into an OPC UA discovery URL.
 * Missing schemes are treated as \c opc.tcp and missing ports default
 * to \c 4840, matching the common local OPC UA endpoint convention.
 */
QUrl OpcUaService::normalizeDiscoveryUrl(const QString &hostOrUrl)
{
    QString s = hostOrUrl.trimmed();
    if (s.isEmpty())
        return {};
    if (!s.contains("://"_L1))
        s.prepend("opc.tcp://"_L1);
    QUrl url(s);
    if (!url.isValid())
        return {};
    if (url.port() == -1)
        url.setPort(4840);
    return url;
}

/*!
 * \brief Prepares the writable OPC UA PKI directory layout.
 * The application-bundled \c pki directory is copied once as initial
 * material, while runtime trust and certificate state live under the writable
 * application data directory.
 */
void OpcUaService::setupPkiConfiguration()
{
    const QString pkiSource = QCoreApplication::applicationDirPath() + QLatin1String("/pki");
    m_pkiBaseDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QLatin1String("/pki");
    qInfo() << "OPC UA runtime PKI directory:" << m_pkiBaseDirectory;

    const QStringList requiredDirectories {
        m_pkiBaseDirectory + QLatin1String("/own/certs"),
        m_pkiBaseDirectory + QLatin1String("/own/private"),
        m_pkiBaseDirectory + QLatin1String("/trusted/certs"),
        m_pkiBaseDirectory + QLatin1String("/trusted/crl"),
        m_pkiBaseDirectory + QLatin1String("/issuers/certs"),
        m_pkiBaseDirectory + QLatin1String("/issuers/crl")
    };

    for (const QString &directory : requiredDirectories) {
        if (!QDir().mkpath(directory))
            qWarning() << "Failed to create PKI directory:" << directory;
    }

    if (QDir(pkiSource).exists() && !copyDirRecursively(pkiSource, m_pkiBaseDirectory))
        qWarning() << "Failed to copy the initial PKI directory to" << m_pkiBaseDirectory;

    m_pkiConfig.setClientCertificateFile(
        m_pkiBaseDirectory + QLatin1String("/own/certs/client.der"));
    m_pkiConfig.setPrivateKeyFile(
        m_pkiBaseDirectory + QLatin1String("/own/private/client.pem"));
    m_pkiConfig.setTrustListDirectory(
        m_pkiBaseDirectory + QLatin1String("/trusted/certs"));
    m_pkiConfig.setRevocationListDirectory(
        m_pkiBaseDirectory + QLatin1String("/trusted/crl"));
    m_pkiConfig.setIssuerListDirectory(
        m_pkiBaseDirectory + QLatin1String("/issuers/certs"));
    m_pkiConfig.setIssuerRevocationListDirectory(
        m_pkiBaseDirectory + QLatin1String("/issuers/crl"));
}

/*!
 * \brief Validates files required for certificate-based authentication.
 */
bool OpcUaService::validateCertificateConfiguration(QString *errorMessage) const
{
    const QFileInfo certificateInfo(m_pkiConfig.clientCertificateFile());
    const QFileInfo privateKeyInfo(m_pkiConfig.privateKeyFile());

    if (!certificateInfo.isFile() || !certificateInfo.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Client certificate is missing or unreadable: %1")
                                .arg(certificateInfo.absoluteFilePath());
        }
        return false;
    }

    if (!privateKeyInfo.isFile() || !privateKeyInfo.isReadable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Client private key is missing or unreadable: %1")
                                .arg(privateKeyInfo.absoluteFilePath());
        }
        return false;
    }

    if (!m_pkiConfig.isPkiValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The OPC UA PKI directories are incomplete or invalid under %1")
                                .arg(m_pkiBaseDirectory);
        }
        return false;
    }

    const QOpcUaApplicationIdentity certificateIdentity = m_pkiConfig.applicationIdentity();
    if (!certificateIdentity.isValid() || certificateIdentity.applicationUri().trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The client certificate does not contain a valid OPC UA application identity.");
        }
        return false;
    }

    return true;
}

/*!
 * \brief Builds an identity that matches the selected authentication mode.
 */
QOpcUaApplicationIdentity OpcUaService::buildApplicationIdentity() const
{
    if (m_authInfo.authenticationType() == QOpcUaUserTokenPolicy::TokenType::Certificate)
        return m_pkiConfig.applicationIdentity();

    QString organizationDomain = QCoreApplication::organizationDomain().trimmed();
    if (organizationDomain.isEmpty())
        organizationDomain = QStringLiteral("local");
    organizationDomain = sanitizeUrnToken(organizationDomain);

    QString applicationName = QCoreApplication::applicationName().trimmed();
    if (applicationName.isEmpty())
        applicationName = QStringLiteral("OpcUaManager");

    QOpcUaApplicationIdentity identity;
    identity.setApplicationName(applicationName);
    identity.setApplicationUri(loadOrCreateStableApplicationUri());
    identity.setProductUri(QStringLiteral("urn:%1:products:%2")
                               .arg(organizationDomain, sanitizeUrnToken(applicationName)));
    identity.setApplicationType(QOpcUaApplicationDescription::Client);
    return identity;
}

/*!
 * \brief Stores and emits the latest user-visible OPC UA error text.
 */
void OpcUaService::setLastError(const QString &err)
{
    const QString e = err.trimmed();
    if (m_lastError == e)
        return;
    m_lastError = e;
    if (m_lastError.isEmpty())
        qInfo() << "OpcUaService lastError cleared.";
    else
        qWarning() << "OpcUaService lastError:" << m_lastError;
    emit lastErrorChanged(m_lastError);
}

/*!
 * \brief Updates the operation state exposed to the GUI.
 */
void OpcUaService::setOperationState(OperationState state)
{
    if (m_operationState == state)
        return;

    m_operationState = state;
    emit operationStateChanged(int(m_operationState));
}

/*!
 * \brief Resolves a UI server row to a discovery URL.
 */
QString OpcUaService::serverUrlAt(int index) const
{
    if (index < 0 || index >= m_serverCandidateUrls.size())
        return {};
    const QUrl candidate = normalizeDiscoveryUrl(m_serverCandidateUrls.at(index));
    if (candidate.isValid())
        return candidate.toString();

    if (index >= m_serverDescriptions.size())
        return {};
    const auto &server = m_serverDescriptions.at(index);
    for (const auto &u : server.discoveryUrls()) {
        const QUrl url = normalizeDiscoveryUrl(u);
        if (url.isValid())
            return url.toString();
    }
    return m_lastFindServersRequestUrl.isValid() ? m_lastFindServersRequestUrl.toString() : QString{};
}

/*!
 * \brief Applies the client identity required by Qt OPC UA.
 */
bool OpcUaService::applyOpcUaIdentity()
{
    if (m_identityApplied)
        return true;
    if (!m_client)
        return false;

    if (m_authInfo.authenticationType() == QOpcUaUserTokenPolicy::TokenType::Certificate) {
        QString validationError;
        if (!validateCertificateConfiguration(&validationError)) {
            setLastError(validationError);
            return false;
        }
    }

    const QOpcUaApplicationIdentity identity = buildApplicationIdentity();
    if (!identity.isValid()) {
        setLastError(QStringLiteral("The OPC UA application identity is invalid."));
        return false;
    }

    m_identity = identity;
    m_client->setApplicationIdentity(m_identity);
    m_identityApplied = true;
    return true;
}

/*!
 * \brief Applies authentication and PKI settings to the active client.
 */
bool OpcUaService::applyClientSecurityConfiguration()
{
    if (!m_client)
        return false;
    if (!applyOpcUaIdentity())
        return false;

    m_client->setAuthenticationInformation(m_authInfo);
    if (m_authInfo.authenticationType() == QOpcUaUserTokenPolicy::TokenType::Certificate)
        m_client->setPkiConfiguration(m_pkiConfig);
    return true;
}

/*!
 * \brief Applies client security settings in the QOpcUaClient object's thread.
 */
bool OpcUaService::queueClientSecurityConfiguration()
{
    if (!m_client)
        return false;

    if (m_authInfo.authenticationType() == QOpcUaUserTokenPolicy::TokenType::Certificate) {
        QString validationError;
        if (!validateCertificateConfiguration(&validationError)) {
            setLastError(validationError);
            return false;
        }
    }

    const QOpcUaApplicationIdentity identity = buildApplicationIdentity();
    if (!identity.isValid()) {
        setLastError(QStringLiteral("The OPC UA application identity is invalid."));
        return false;
    }

    m_identity = identity;
    m_identityApplied = true;

    if (m_client->thread() == QThread::currentThread())
        return applyClientSecurityConfiguration();

    const QOpcUaAuthenticationInformation authInfo = m_authInfo;
    const QOpcUaPkiConfiguration pkiConfig = m_pkiConfig;
    const auto authType = m_authInfo.authenticationType();
    const QPointer<QOpcUaClient> client(m_client);

    const bool queued = QMetaObject::invokeMethod(m_client,
                                                  [client, identity, authInfo, pkiConfig, authType]() {
                                                      if (!client)
                                                          return;

                                                      qInfo() << "OpcUaService applying client security configuration."
                                                              << "current thread:" << QThread::currentThread()
                                                              << "client thread:" << client->thread();
                                                      client->setApplicationIdentity(identity);
                                                      client->setAuthenticationInformation(authInfo);
                                                      if (authType == QOpcUaUserTokenPolicy::TokenType::Certificate)
                                                          client->setPkiConfiguration(pkiConfig);
                                                  },
                                                  Qt::QueuedConnection);
    if (!queued)
        setLastError(QStringLiteral("Failed to queue OPC UA client security configuration."));
    return queued;
}

/*!
 * \brief Handles the result of queueing and executing FindServers in the client thread.
 */
void OpcUaService::handleFindServersDispatchResult(bool dispatched, const QUrl &url)
{
    qInfo() << "OpcUaService findServers dispatched:" << dispatched << "url:" << url;
    if (url != m_lastFindServersRequestUrl)
        return;

    if (!dispatched) {
        m_findServersRequestPending = false;
        if (m_findServersTimeoutTimer)
            m_findServersTimeoutTimer->stop();
        setLastError(QStringLiteral("findServers() failed to dispatch for %1").arg(url.toString()));
        setOperationState(OperationState::Idle);
    }
}

/*!
 * \brief Handles the result of queueing and executing GetEndpoints in the client thread.
 */
void OpcUaService::handleEndpointsDispatchResult(bool dispatched, const QUrl &url)
{
    qInfo() << "OpcUaService requestEndpoints dispatched:" << dispatched << "url:" << url;
    if (url != m_lastEndpointsRequestUrl)
        return;

    if (!dispatched) {
        m_endpointsRequestPending = false;
        if (m_endpointsTimeoutTimer)
            m_endpointsTimeoutTimer->stop();
        setLastError(QStringLiteral("requestEndpoints() failed to dispatch for %1").arg(url.toString()));
        setOperationState(OperationState::Idle);
    }
}

/*!
 * \brief Returns whether \a ep accepts the selected authentication mode.
 */
bool OpcUaService::endpointSupportsAuth(const QOpcUaEndpointDescription &ep) const
{
    const auto desired = m_authInfo.authenticationType();
    const auto tokens = ep.userIdentityTokens();
    if (tokens.isEmpty())
        return (desired == QOpcUaUserTokenPolicy::TokenType::Anonymous);
    for (const auto &t : tokens) {
        if (t.tokenType() == desired)
            return true;
    }
    return false;
}

/*!
 * \brief Returns whether \a ep uses no message security.
 */
bool OpcUaService::endpointIsNoSecurity(const QOpcUaEndpointDescription &ep)
{
    const QString pol = ep.securityPolicy();
    const bool policyNone = pol.isEmpty() || pol.endsWith(QStringLiteral("#None"));
    const bool modeNone = (ep.securityMode() == QOpcUaEndpointDescription::MessageSecurityMode::None);
    return policyNone && modeNone;
}

/*!
 * \brief Chooses the preferred endpoint from \a endpoints.
 * Anonymous no-security endpoints are preferred for the default UI
 * flow; otherwise the first endpoint compatible with the selected
 * authentication mode is selected.
 */
QOpcUaEndpointDescription OpcUaService::chooseEndpoint(const QList<QOpcUaEndpointDescription> &endpoints) const
{
    for (const auto &ep : endpoints) {
        if (endpointIsNoSecurity(ep) && endpointSupportsAuth(ep))
            return ep;
    }
    for (const auto &ep : endpoints) {
        if (endpointSupportsAuth(ep))
            return ep;
    }
    return endpoints.isEmpty() ? QOpcUaEndpointDescription() : endpoints.first();
}

/*!
 * \brief Formats supported authentication token modes for logging and display.
 */
QString OpcUaService::endpointAuthModesToString(const QOpcUaEndpointDescription &ep)
{
    QStringList parts;
    for (const auto &t : ep.userIdentityTokens()) {
        switch (t.tokenType()) {
        case QOpcUaUserTokenPolicy::TokenType::Anonymous: parts << QStringLiteral("Anonymous"); break;
        case QOpcUaUserTokenPolicy::TokenType::Username: parts << QStringLiteral("Username"); break;
        case QOpcUaUserTokenPolicy::TokenType::Certificate: parts << QStringLiteral("Certificate"); break;
        case QOpcUaUserTokenPolicy::TokenType::IssuedToken: parts << QStringLiteral("IssuedToken"); break;
        default: parts << QStringLiteral("Unknown"); break;
        }
    }
    parts.removeDuplicates();
    return parts.join(QStringLiteral(","));
}

/*!
 * \brief Rewrites an endpoint URL host/port to match the discovery URL.
 * Some servers advertise host names that are not reachable from the
 * current client. Preserving the selected discovery host keeps local direct
 * connections practical.
 */
QOpcUaEndpointDescription OpcUaService::patchEndpointForDiscoveryUrl(
    const QOpcUaEndpointDescription &ep,
    const QUrl &discoveryUrl) const
{
    if (!m_endpointUrlRewriteEnabled || !discoveryUrl.isValid())
        return ep;

    QOpcUaEndpointDescription patchedEndpoint = ep;
    QUrl endpointUrl(patchedEndpoint.endpointUrl());
    if (!endpointUrl.isValid())
        return ep;

    endpointUrl.setHost(discoveryUrl.host());
    if (discoveryUrl.port() > 0)
        endpointUrl.setPort(discoveryUrl.port());
    patchedEndpoint.setEndpointUrl(endpointUrl.toString());
    return patchedEndpoint;
}

/*!
 * \brief Clears cached endpoint selection state.
 */
void OpcUaService::invalidateEndpointCache()
{
    m_endpointCacheValid = false;
    m_endpointCacheDiscoveryUrl = QUrl();
    m_endpointCacheAuthType = m_authInfo.authenticationType();
    m_cachedEndpoints.clear();
}

/*!
 * \brief Creates and wires the QOpcUaClient if it does not already exist.
 * This is the client used for discovery, endpoint lookup, connect,
 * and browse operations.
 */
void OpcUaService::createClient()
{
    if (!isInObjectThread()) {
        QMetaObject::invokeMethod(this, &OpcUaService::createClient, Qt::BlockingQueuedConnection);
        return;
    }

    initialize();
    if (m_client || !m_provider)
        return;
    const QString backendToUse = !m_backend.isEmpty() ? m_backend : (m_availableBackends.isEmpty() ? QString() : m_availableBackends.first());
    if (backendToUse.isEmpty()) {
        qWarning() << "Cannot create OPC UA client because no backend is selected.";
        setLastError(QStringLiteral("No backend selected."));
        return;
    }

    qInfo() << "Creating QOpcUaClient for backend" << backendToUse
            << "in OpcUaThread.";
    m_client = m_provider->createClient(backendToUse, m_backendProperties);
    m_identityApplied = false;
    if (!m_client) {
        qCritical() << "Failed to create QOpcUaClient for backend" << backendToUse;
        setLastError(QStringLiteral("Failed to create QOpcUaClient for backend '%1'.").arg(backendToUse));
        return;
    }

    if (m_client->thread() != QThread::currentThread()) {
        qWarning() << "QOpcUaClient was created with unexpected thread affinity. Keeping explicit lifetime management.";
    }

    qInfo() << "QOpcUaClient created for backend" << backendToUse;
    connect(m_client, &QOpcUaClient::connected, this, &OpcUaService::onClientConnected, Qt::QueuedConnection);
    connect(m_client, &QOpcUaClient::disconnected, this, &OpcUaService::onClientDisconnected, Qt::QueuedConnection);
    connect(m_client, &QOpcUaClient::stateChanged, this, &OpcUaService::onClientStateChanged, Qt::QueuedConnection);
    connect(m_client, &QOpcUaClient::errorChanged, this, &OpcUaService::onClientErrorChanged, Qt::QueuedConnection);
    // QOpcUaClient waits for connectError handlers to return. Handle the
    // temporary QOpcUaErrorState synchronously and never keep its pointer.
    connect(m_client,
            &QOpcUaClient::connectError,
            this,
            &OpcUaService::onConnectError,
            Qt::DirectConnection);
    connect(m_client, &QOpcUaClient::findServersFinished, this, &OpcUaService::findServersComplete, Qt::QueuedConnection);
    connect(m_client, &QOpcUaClient::endpointsRequestFinished, this, &OpcUaService::getEndpointsComplete, Qt::QueuedConnection);
    connect(m_client,
            &QOpcUaClient::passwordForPrivateKeyRequired,
            this,
            [this](const QString &keyFilePath, QString *password, bool previousTryWasInvalid) {
                if (!password)
                    return;
                QString errorText;
                if (previousTryWasInvalid) {
                    errorText = QStringLiteral("The password for private key %1 is invalid.")
                                    .arg(keyFilePath);
                    password->clear();
                } else {
                    *password = m_certificatePrivateKeyPassword;
                    if (password->isEmpty()) {
                        errorText = QStringLiteral(
                            "Private key %1 is encrypted. Enter its password and retry.")
                                        .arg(keyFilePath);
                    }
                }

                if (!errorText.isEmpty()) {
                    const auto applyError = [this, errorText]() { setLastError(errorText); };
                    if (isInObjectThread())
                        applyError();
                    else
                        QMetaObject::invokeMethod(this, applyError, Qt::QueuedConnection);
                }
            },
            Qt::DirectConnection);
    queueClientSecurityConfiguration();
}

/*!
 * \brief Applies the asynchronous FindServers result.
 * Server descriptions are converted into display rows and matching
 * candidate URLs. Stale callbacks are ignored, and failed or empty results
 * leave the server list empty so endpoint lookup stays disabled.
 */
void OpcUaService::findServersComplete(const QList<QOpcUaApplicationDescription> &servers, QOpcUa::UaStatusCode statusCode, const QUrl &requestUrl)
{
    qInfo() << "OpcUaService findServersComplete."
            << "status:" << QOpcUa::statusToString(statusCode)
            << "server count:" << servers.size()
            << "request URL:" << requestUrl;
    const QUrl effectiveRequestUrl = requestUrl.isValid() ? requestUrl : m_lastFindServersRequestUrl;
    if (!m_findServersRequestPending) {
        qWarning() << "Ignoring late FindServers result after request completion or timeout."
                   << "request URL:" << requestUrl;
        return;
    }
    if (requestUrl.isValid() && requestUrl != m_lastFindServersRequestUrl) {
        qWarning() << "Ignoring stale FindServers result."
                   << "request URL:" << requestUrl
                   << "last request URL:" << m_lastFindServersRequestUrl;
        return;
    }
    m_findServersRequestPending = false;
    if (m_findServersTimeoutTimer)
        m_findServersTimeoutTimer->stop();
    setOperationState(OperationState::Idle);

    if (statusCode != QOpcUa::UaStatusCode::Good) {
        setLastError(QStringLiteral("FindServers failed: %1").arg(QOpcUa::statusToString(statusCode)));
        return;
    }

    if (servers.isEmpty()) {
        setLastError(QStringLiteral("FindServers returned 0 servers for %1.")
                         .arg(effectiveRequestUrl.toString()));
        return;
    }

    m_lastFindServersRequestUrl = effectiveRequestUrl;
    const QMetaEnum typeEnum = QMetaEnum::fromType<QOpcUaApplicationDescription::ApplicationType>();
    QStringList display;
    QList<QOpcUaApplicationDescription> discoveredServers;
    QStringList discoveredCandidateUrls;
    for (const auto &server : servers) {
        QStringList validUrls;
        for (const auto &u : server.discoveryUrls()) {
            const QUrl normalized = normalizeDiscoveryUrl(u);
            if (normalized.isValid())
                validUrls << normalized.toString();
        }
        const QString appName = server.applicationName().text().trimmed();
        const QString appUri = server.applicationUri().trimmed();
        const QString appType = QString::fromLatin1(typeEnum.valueToKey(int(server.applicationType())));
        const QString primaryUrl = !validUrls.isEmpty() ? validUrls.first() : effectiveRequestUrl.toString();
        QString line = QStringLiteral("%1 | %2 | %3").arg(appName.isEmpty() ? QStringLiteral("<no applicationName>") : appName, primaryUrl.isEmpty() ? QStringLiteral("<no discovery URL>") : primaryUrl, appType.isEmpty() ? QStringLiteral("<unknown type>") : appType);
        if (appUri.isEmpty())
            line += QStringLiteral(" | appUri:<empty>");
        else
            line += QStringLiteral(" | appUri:%1").arg(appUri);
        if (validUrls.isEmpty())
            line += QStringLiteral(" | discoveryUrls:<empty, fallback=requestUrl>");
        display << line;
        discoveredServers.push_back(server);
        discoveredCandidateUrls << primaryUrl;
        qInfo() << "OpcUaService discovered server."
                << "application name:" << appName
                << "application URI:" << appUri
                << "application type:" << appType
                << "discovery URLs:" << server.discoveryUrls();
    }

    m_serverDescriptions = discoveredServers;
    m_serverCandidateUrls = discoveredCandidateUrls;
    m_servers = display;
    emit serversChanged(m_servers);
}

/*!
 * \brief Applies the asynchronous GetEndpoints result.
 * The endpoint list is patched, sorted by the current authentication
 * preference, cached, and exposed as display text for QML.
 */
void OpcUaService::getEndpointsComplete(const QList<QOpcUaEndpointDescription> &endpoints,
                                        QOpcUa::UaStatusCode statusCode,
                                        const QUrl &requestUrl)
{
    qInfo() << "OpcUaService getEndpointsComplete."
            << "status:" << QOpcUa::statusToString(statusCode)
            << "endpoint count:" << endpoints.size()
            << "request URL:" << requestUrl;

    if (!m_endpointsRequestPending) {
        qWarning() << "Ignoring late GetEndpoints result after request completion or timeout."
                   << "request URL:" << requestUrl;
        return;
    }

    if (requestUrl != m_lastEndpointsRequestUrl) {
        qWarning() << "Ignoring stale GetEndpoints result."
                   << "request URL:" << requestUrl
                   << "last request URL:" << m_lastEndpointsRequestUrl;
        return;
    }

    m_endpointsRequestPending = false;
    if (m_endpointsTimeoutTimer)
        m_endpointsTimeoutTimer->stop();
    setOperationState(OperationState::Idle);

    m_endpointList.clear();
    if (!m_endpointsDisplay.isEmpty()) {
        m_endpointsDisplay.clear();
        emit endpointsChanged(m_endpointsDisplay);
    }
    if (statusCode != QOpcUa::UaStatusCode::Good) {
        setLastError(QStringLiteral("GetEndpoints failed: %1")
                         .arg(QOpcUa::statusToString(statusCode)));
        return;
    }
    if (endpoints.isEmpty()) {
        setLastError(QStringLiteral("GetEndpoints returned 0 endpoints for %1")
                         .arg(requestUrl.toString()));
        return;
    }

    QList<QOpcUaEndpointDescription> displayEndpoints;
    displayEndpoints.reserve(endpoints.size());
    for (const auto &endpoint : endpoints)
        displayEndpoints << patchEndpointForDiscoveryUrl(endpoint, requestUrl);

    std::stable_sort(displayEndpoints.begin(),
                     displayEndpoints.end(),
                     [this](const QOpcUaEndpointDescription &left,
                            const QOpcUaEndpointDescription &right) {
                         const bool leftPreferred = endpointIsNoSecurity(left)
                             && endpointSupportsAuth(left);
                         const bool rightPreferred = endpointIsNoSecurity(right)
                             && endpointSupportsAuth(right);
                         if (leftPreferred != rightPreferred)
                             return leftPreferred > rightPreferred;
                         const bool leftSupported = endpointSupportsAuth(left);
                         const bool rightSupported = endpointSupportsAuth(right);
                         if (leftSupported != rightSupported)
                             return leftSupported > rightSupported;
                         return left.endpointUrl() < right.endpointUrl();
                     });

    m_endpointList = displayEndpoints;
    m_cachedEndpoints = endpoints;
    m_endpointCacheValid = true;
    m_endpointCacheDiscoveryUrl = requestUrl;
    m_endpointCacheAuthType = m_authInfo.authenticationType();

    const QMetaEnum modeEnum =
        QMetaEnum::fromType<QOpcUaEndpointDescription::MessageSecurityMode>();
    for (const auto &endpoint : m_endpointList) {
        const QString mode = QString::fromLatin1(
            modeEnum.valueToKey(int(endpoint.securityMode())));
        const QString auth = endpointAuthModesToString(endpoint);
        m_endpointsDisplay.push_back(
            QStringLiteral("%1 | %2 | %3 | auth:%4")
                .arg(endpoint.endpointUrl(),
                     endpoint.securityPolicy(),
                     mode,
                     auth.isEmpty() ? QStringLiteral("<none>") : auth));
    }

    emit endpointsChanged(m_endpointsDisplay);
}

/*!
 * \brief Handles successful session connection.
 */
void OpcUaService::onClientConnected()
{
    qInfo() << "OpcUaService client connected.";
    m_clientConnected = true;
    setOperationState(OperationState::Idle);
    setLastError(QString());
    emit connectedChanged(m_clientConnected);
    connect(m_client,
            &QOpcUaClient::namespaceArrayUpdated,
            this,
            &OpcUaService::namespacesArrayUpdated,
            Qt::UniqueConnection);
    m_client->updateNamespaceArray();
}

/*!
 * \brief Handles session disconnection and clears protocol helpers.
 */
void OpcUaService::onClientDisconnected()
{
    qInfo() << "OpcUaService client disconnected.";
    m_clientConnected = false;
    setOperationState(OperationState::Idle);
    emit connectedChanged(m_clientConnected);
    if (m_client) {
        disconnect(m_client,
                   &QOpcUaClient::namespaceArrayUpdated,
                   this,
                   &OpcUaService::namespacesArrayUpdated);
    }
    m_genericStructHandler.reset();
}

/*!
 * \brief Logs client state changes.
 */
void OpcUaService::onClientStateChanged(QOpcUaClient::ClientState state)
{
    qInfo() << "OpcUaService client state changed:" << int(state);
    emit clientStateChanged(int(state));

    switch (state) {
    case QOpcUaClient::ClientState::Connecting:
        setOperationState(OperationState::Connecting);
        break;
    case QOpcUaClient::ClientState::Closing:
        setOperationState(OperationState::Disconnecting);
        break;
    case QOpcUaClient::ClientState::Connected:
    case QOpcUaClient::ClientState::Disconnected:
        setOperationState(OperationState::Idle);
        break;
    }
}

/*!
 * \brief Converts client error changes into user-visible error text.
 */
void OpcUaService::onClientErrorChanged(QOpcUaClient::ClientError error)
{
    qInfo() << "OpcUaService client error changed:" << int(error);
    if (error == QOpcUaClient::ClientError::NoError)
        return;

    setOperationState(OperationState::Idle);
    setLastError(QStringLiteral("ClientError: %1").arg(int(error)));
}

/*!
 * \brief Converts connect errors into user-visible error text.
 */
void OpcUaService::onConnectError(QOpcUaErrorState *errorState)
{
    if (!errorState)
        return;

    qWarning() << "OpcUaService connect error."
               << "status:" << QOpcUa::statusToString(errorState->errorCode())
               << "connection step:" << int(errorState->connectionStep())
               << "client side:" << errorState->isClientSideError();

    // Security errors are intentionally not ignored. Trust is established by
    // placing the server DER certificate in pki/trusted/certs and reconnecting.
    const QString trustHint = errorState->connectionStep()
            == QOpcUaErrorState::ConnectionStep::CertificateValidation
        ? QStringLiteral(" Add the verified server certificate to %1/trusted/certs and retry.")
              .arg(m_pkiBaseDirectory)
        : QString();

    const QString errorText = QStringLiteral("Connect error (%1, step %2, %3).%4")
                                  .arg(QOpcUa::statusToString(errorState->errorCode()))
                                  .arg(int(errorState->connectionStep()))
                                  .arg(errorState->isClientSideError()
                                           ? QStringLiteral("client")
                                           : QStringLiteral("server"),
                                       trustHint);

    const auto applyError = [this, errorText]() {
        setOperationState(OperationState::Idle);
        setLastError(errorText);
    };
    if (isInObjectThread())
        applyError();
    else
        QMetaObject::invokeMethod(this, applyError, Qt::QueuedConnection);
}

/*!
 * \brief Handles a FindServers request that did not complete in time.
 */
void OpcUaService::handleFindServersTimeout()
{
    if (!m_findServersRequestPending)
        return;

    m_findServersRequestPending = false;
    setOperationState(OperationState::Idle);
    qWarning() << "FindServers timeout."
               << "url:" << m_lastFindServersRequestUrl
               << "timeout ms:" << m_findServersRequestTimeoutMs;
    setLastError(QStringLiteral("FindServers timeout for %1.")
                     .arg(m_lastFindServersRequestUrl.toString()));
}

/*!
 * \brief Handles a GetEndpoints request that did not complete in time.
 */
void OpcUaService::handleEndpointsTimeout()
{
    if (!m_endpointsRequestPending)
        return;

    m_endpointsRequestPending = false;
    setOperationState(OperationState::Idle);
    qWarning() << "GetEndpoints timeout."
               << "url:" << m_lastEndpointsRequestUrl
               << "timeout ms:" << m_endpointsRequestTimeoutMs;
    setLastError(QStringLiteral("GetEndpoints timeout for %1.")
                     .arg(m_lastEndpointsRequestUrl.toString()));
}

/*!
 * \brief Initializes support for custom structured OPC UA types after connect.
 */
void OpcUaService::namespacesArrayUpdated(const QStringList &namespaceArray)
{
    if (!m_client || namespaceArray.isEmpty())
        return;
    disconnect(m_client, &QOpcUaClient::namespaceArrayUpdated, this, &OpcUaService::namespacesArrayUpdated);
    m_genericStructHandler.reset(new QOpcUaGenericStructHandler(m_client));
    connect(m_genericStructHandler.get(), &QOpcUaGenericStructHandler::initializedChanged, this, &OpcUaService::handleGenericStructHandlerInitFinished);
    m_genericStructHandler->initialize();
}

/*!
 * \brief Reports generic structure handler initialization failure.
 */
void OpcUaService::handleGenericStructHandlerInitFinished(bool success)
{
    if (!success)
        setLastError(QStringLiteral("GenericStructHandler initialization failed."));
}
