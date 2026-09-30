#include "networkscanner.h"

#include "opcuaendpointaddress.h"

#include <QOpcUaClient>
#include <QOpcUaConnectionSettings>
#include <QOpcUaProvider>
#include <QOpcUaUserTokenPolicy>
#include <QTcpSocket>
#include <QTimer>

#include <chrono>

namespace {

/*!
 * \internal
 * \brief Returns the distinct security policy names of \a endpoints, e.g. "None, Basic256Sha256".
 */
QString securitySummary(const QList<QOpcUaEndpointDescription> &endpoints)
{
    QStringList names;
    for (const QOpcUaEndpointDescription &endpoint : endpoints) {
        const QString name = endpoint.securityPolicy().section(QLatin1Char('#'), -1);
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    return names.join(QStringLiteral(", "));
}

/*!
 * \internal
 * \brief Returns the distinct user token types of \a endpoints, e.g. "Anonymous, Username".
 */
QString authSummary(const QList<QOpcUaEndpointDescription> &endpoints)
{
    QStringList names;
    for (const QOpcUaEndpointDescription &endpoint : endpoints) {
        for (const QOpcUaUserTokenPolicy &token : endpoint.userIdentityTokens()) {
            QString name;
            switch (token.tokenType()) {
            case QOpcUaUserTokenPolicy::TokenType::Anonymous:
                name = QStringLiteral("Anonymous");
                break;
            case QOpcUaUserTokenPolicy::TokenType::Username:
                name = QStringLiteral("Username");
                break;
            case QOpcUaUserTokenPolicy::TokenType::Certificate:
                name = QStringLiteral("Certificate");
                break;
            case QOpcUaUserTokenPolicy::TokenType::IssuedToken:
                name = QStringLiteral("IssuedToken");
                break;
            }
            if (!name.isEmpty() && !names.contains(name))
                names << name;
        }
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

/*!
 * \brief Creates an idle scanner owned by \a parent.
 */
NetworkScanner::NetworkScanner(QObject *parent)
    : QObject(parent)
    , m_opcUaTimer(new QTimer(this))
{
    qRegisterMetaType<NetworkScanResult>();
    m_opcUaTimer->setSingleShot(true);
    connect(m_opcUaTimer, &QTimer::timeout, this, &NetworkScanner::onOpcUaTimeout);
}

/*!
 * \brief Stops all work and releases the private OPC UA clients.
 *
 * Deleting a client waits for its backend thread. A client that is still in a
 * request (the current one, or one retired after a timeout) can therefore block
 * the calling thread for up to the backend's fixed discovery timeout of about
 * five seconds.
 *
 * The provider is intentionally never deleted. QOpcUaProvider deletes the
 * backend plugins it used, but a plugin is one process-wide instance shared by
 * every provider in the process. The OPC UA service owns another provider that
 * is destroyed first at application exit, so deleting this one as well would
 * delete the plugin a second time. The service's provider cannot be used
 * instead: it lives in the service's worker thread, and
 * QOpcUaProvider::createClient() updates its plugin table without locking. The
 * provider is a small object and stays alive until the process ends.
 */
NetworkScanner::~NetworkScanner()
{
    abortAll();
    delete m_client;
    qDeleteAll(m_retiredClients);
}

/*!
 * \brief Scans every combination of \a addresses and \a ports.
 * \param addresses IPv4 addresses to probe.
 * \param ports TCP ports to probe on every address.
 *
 * Results of a previous scan are dropped; late OPC UA replies for it are
 * ignored because the pending query is cleared.
 */
void NetworkScanner::start(const QList<QHostAddress> &addresses, const QList<quint16> &ports)
{
    abortAll();
    m_targets.clear();
    for (const QHostAddress &address : addresses) {
        for (const quint16 port : ports)
            m_targets.append({address, port});
    }
    m_nextTarget = 0;
    m_probed = 0;
    m_openPorts = 0;
    m_opcUaServers = 0;
    m_elapsedMs = 0;
    m_elapsed.start();

    setState(State::Scanning);
    emit progressChanged();
    launchProbes();
    checkFinished();
}

/*!
 * \brief Stops the running scan; results reported so far stay valid.
 */
void NetworkScanner::cancel()
{
    if (m_state != State::Scanning)
        return;
    abortAll();
    m_elapsedMs = m_elapsed.elapsed();
    setState(State::Cancelled);
    emit progressChanged();
}

NetworkScanner::State NetworkScanner::state() const
{
    return m_state;
}

int NetworkScanner::totalTargets() const
{
    return int(m_targets.size());
}

int NetworkScanner::probedTargets() const
{
    return m_probed;
}

int NetworkScanner::openPorts() const
{
    return m_openPorts;
}

int NetworkScanner::opcUaServers() const
{
    return m_opcUaServers;
}

qint64 NetworkScanner::elapsedMs() const
{
    return m_state == State::Scanning ? m_elapsed.elapsed() : m_elapsedMs;
}

void NetworkScanner::setProbeTimeoutMs(int timeoutMs)
{
    m_probeTimeoutMs = qMax(1, timeoutMs);
}

void NetworkScanner::setMaxParallelProbes(int count)
{
    m_maxParallelProbes = qMax(1, count);
}

void NetworkScanner::setOpcUaTimeoutMs(int timeoutMs)
{
    m_opcUaTimeoutMs = qMax(1, timeoutMs);
    if (m_client) {
        QOpcUaConnectionSettings settings = m_client->connectionSettings();
        settings.setConnectTimeout(std::chrono::milliseconds(m_opcUaTimeoutMs));
        settings.setRequestTimeout(std::chrono::milliseconds(m_opcUaTimeoutMs));
        m_client->setConnectionSettings(settings);
    }
}

/*!
 * \internal
 * \brief Starts TCP probes until the pool is full or no target is left.
 *
 * Each socket has its own timeout timer as a child, so deleting the socket also
 * removes the timer.
 */
void NetworkScanner::launchProbes()
{
    while (m_state == State::Scanning && m_probes.size() < m_maxParallelProbes
           && m_nextTarget < m_targets.size()) {
        const Target target = m_targets.at(m_nextTarget++);
        auto *socket = new QTcpSocket(this);
        auto *timeout = new QTimer(socket);
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, this, [this, socket] { finishProbe(socket); });
        connect(socket, &QTcpSocket::connected, this, [this, socket] { onProbeConnected(socket); });
        connect(socket, &QTcpSocket::errorOccurred, this, [this, socket] { finishProbe(socket); });

        Probe probe;
        probe.target = target;
        probe.timer.start();
        probe.timeout = timeout;
        m_probes.insert(socket, probe);
        timeout->start(m_probeTimeoutMs);
        socket->connectToHost(target.address, target.port);
    }
}

/*!
 * \internal
 * \brief Records an open port and queues it for the OPC UA stage.
 */
void NetworkScanner::onProbeConnected(QTcpSocket *socket)
{
    const auto it = m_probes.constFind(socket);
    if (it == m_probes.cend())
        return;

    NetworkScanResult result;
    result.address = it->target.address.toString();
    result.port = it->target.port;
    result.responseMs = int(it->timer.elapsed());
    result.status = NetworkScanStatus::PortOpen;
    ++m_openPorts;
    m_queryQueue.append(result);
    emit resultChanged(result);

    finishProbe(socket);
    queryNext();
}

/*!
 * \internal
 * \brief Ends the probe on \a socket and starts the next one.
 *
 * Signals are disconnected before aborting so the abort cannot report the same
 * probe a second time.
 */
void NetworkScanner::finishProbe(QTcpSocket *socket)
{
    if (!m_probes.remove(socket))
        return;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
    ++m_probed;
    emit progressChanged();
    launchProbes();
    checkFinished();
}

/*!
 * \internal
 * \brief Sends FindServers for the next queued open port, if none is in flight.
 */
void NetworkScanner::queryNext()
{
    if (m_current || m_queryQueue.isEmpty() || m_state != State::Scanning)
        return;

    if (!ensureClient()) {
        while (!m_queryQueue.isEmpty()) {
            NetworkScanResult result = m_queryQueue.takeFirst();
            result.status = NetworkScanStatus::NoOpcUaResponse;
            result.errorText = QStringLiteral("NoOpcUaBackend");
            emit resultChanged(result);
        }
        checkFinished();
        return;
    }

    m_current = m_queryQueue.takeFirst();
    m_currentRequestUrl = QUrl(m_current->url());
    m_opcUaTimer->start(m_opcUaTimeoutMs);
    if (!m_client->findServers(m_currentRequestUrl))
        completeQuery(NetworkScanStatus::NoOpcUaResponse, QStringLiteral("DispatchFailed"));
}

/*!
 * \internal
 * \brief Takes the server description and requests the endpoints.
 *
 * GetEndpoints goes to the advertised discovery URL redirected to the address
 * that answered, so a server advertising an unresolvable host name still works.
 */
void NetworkScanner::onFindServersFinished(const QList<QOpcUaApplicationDescription> &servers,
                                           QOpcUa::UaStatusCode statusCode,
                                           const QUrl &requestUrl)
{
    if (!m_current || requestUrl != m_currentRequestUrl)
        return;
    if (statusCode != QOpcUa::UaStatusCode::Good || servers.isEmpty()) {
        completeQuery(NetworkScanStatus::NoOpcUaResponse,
                      statusCode == QOpcUa::UaStatusCode::Good ? QStringLiteral("NoServers")
                                                              : QOpcUa::statusToString(statusCode));
        return;
    }

    const QOpcUaApplicationDescription &server = servers.first();
    m_current->applicationName = server.applicationName().text();
    m_current->applicationUri = server.applicationUri();
    m_current->productUri = server.productUri();

    QUrl endpointsUrl = m_currentRequestUrl;
    for (const QString &discoveryUrl : server.discoveryUrls()) {
        const QUrl advertised = OpcUaEndpointAddress::normalizeDiscoveryUrl(discoveryUrl);
        if (advertised.isValid() && advertised.scheme() == QLatin1String("opc.tcp")) {
            endpointsUrl = OpcUaEndpointAddress::reachableUrl(advertised, m_currentRequestUrl);
            break;
        }
    }

    m_currentRequestUrl = endpointsUrl;
    m_opcUaTimer->start(m_opcUaTimeoutMs);
    if (!m_client->requestEndpoints(endpointsUrl))
        completeQuery(NetworkScanStatus::OpcUa, QStringLiteral("DispatchFailed"));
}

/*!
 * \internal
 * \brief Summarizes security and login options and completes the current query.
 */
void NetworkScanner::onEndpointsFinished(const QList<QOpcUaEndpointDescription> &endpoints,
                                         QOpcUa::UaStatusCode statusCode,
                                         const QUrl &requestUrl)
{
    if (!m_current || requestUrl != m_currentRequestUrl)
        return;
    if (statusCode != QOpcUa::UaStatusCode::Good) {
        completeQuery(NetworkScanStatus::OpcUa, QOpcUa::statusToString(statusCode));
        return;
    }
    m_current->securitySummary = securitySummary(endpoints);
    m_current->authSummary = authSummary(endpoints);
    completeQuery(NetworkScanStatus::OpcUa, QString());
}

/*!
 * \internal
 * \brief Completes the current query after the OPC UA request timed out.
 *
 * A server that already answered FindServers stays an OPC UA row.
 */
void NetworkScanner::onOpcUaTimeout()
{
    if (!m_current)
        return;
    const bool answered = !m_current->applicationUri.isEmpty()
                          || !m_current->applicationName.isEmpty();
    // The silent server keeps the backend busy; the next query needs its own client.
    retireClient();
    completeQuery(answered ? NetworkScanStatus::OpcUa : NetworkScanStatus::NoOpcUaResponse,
                  QStringLiteral("BadTimeout"));
}

/*!
 * \internal
 * \brief Reports the current row with \a status and \a errorText and moves on.
 */
void NetworkScanner::completeQuery(NetworkScanStatus status, const QString &errorText)
{
    m_opcUaTimer->stop();
    NetworkScanResult result = *m_current;
    m_current.reset();
    m_currentRequestUrl.clear();

    result.status = status;
    result.errorText = errorText;
    if (status == NetworkScanStatus::OpcUa)
        ++m_opcUaServers;
    emit resultChanged(result);
    emit progressChanged();

    queryNext();
    checkFinished();
}

/*!
 * \internal
 * \brief Creates the private OPC UA client on first use.
 *
 * The connection settings are aligned with the scanner timeout. The backend
 * runs FindServers and GetEndpoints synchronously on its own thread with a fixed
 * timeout that these settings do not change, so a silent port blocks the client
 * for longer than the scanner waits; retireClient() handles that case.
 *
 * Creating an open62541 client blocks the calling thread for several hundred
 * milliseconds (the backend generates an RSA key to test SHA-1 support), which
 * can exceed the probe timeout. The probes running meanwhile get their timeout
 * restarted, so a port that connected during the stall is not reported closed.
 */
bool NetworkScanner::ensureClient()
{
    if (m_client)
        return true;
    if (!m_provider)
        m_provider = new QOpcUaProvider;
    const QStringList backends = m_provider->availableBackends();
    if (backends.isEmpty())
        return false;
    const QString backend = backends.contains(QStringLiteral("open62541"))
                                ? QStringLiteral("open62541")
                                : backends.first();
    m_client = m_provider->createClient(backend);
    if (!m_client)
        return false;

    QOpcUaConnectionSettings settings;
    settings.setConnectTimeout(std::chrono::milliseconds(m_opcUaTimeoutMs));
    settings.setRequestTimeout(std::chrono::milliseconds(m_opcUaTimeoutMs));
    m_client->setConnectionSettings(settings);

    connect(m_client, &QOpcUaClient::findServersFinished, this,
            &NetworkScanner::onFindServersFinished);
    connect(m_client, &QOpcUaClient::endpointsRequestFinished, this,
            &NetworkScanner::onEndpointsFinished);
    restartProbeTimeouts();
    return true;
}

/*!
 * \internal
 * \brief Restarts the connect timeout of every running probe.
 *
 * Called after a synchronous stall of the GUI thread. A timeout that expired
 * during the stall has not been delivered yet; restarting the timer drops it,
 * so the pending connected() notification of the socket is handled first.
 */
void NetworkScanner::restartProbeTimeouts()
{
    for (const Probe &probe : std::as_const(m_probes))
        probe.timeout->start(m_probeTimeoutMs);
}

/*!
 * \internal
 * \brief Stops using the current client while it may still be blocked in a request.
 *
 * Deleting a client waits for its backend thread, so a request stuck on a silent
 * port would freeze the GUI thread. The client is detached from the scanner and
 * deleted once its pending request has ended; the next query gets a new client.
 */
void NetworkScanner::retireClient()
{
    if (!m_client)
        return;
    QOpcUaClient *client = m_client;
    m_client = nullptr;
    client->disconnect(this);
    m_retiredClients.append(client);
    const auto release = [this, client] {
        m_retiredClients.removeOne(client);
        client->deleteLater();
    };
    connect(client, &QOpcUaClient::findServersFinished, this, release);
    connect(client, &QOpcUaClient::endpointsRequestFinished, this, release);
}

/*!
 * \internal
 * \brief Closes all probes and drops queued and in-flight OPC UA queries.
 */
void NetworkScanner::abortAll()
{
    const QList<QTcpSocket *> sockets = m_probes.keys();
    m_probes.clear();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_queryQueue.clear();
    if (m_current)
        retireClient();
    m_current.reset();
    m_currentRequestUrl.clear();
    m_opcUaTimer->stop();
}

/*!
 * \internal
 * \brief Switches to Finished when all probes and queries are done.
 */
void NetworkScanner::checkFinished()
{
    if (m_state != State::Scanning)
        return;
    if (m_probed < m_targets.size() || !m_queryQueue.isEmpty() || m_current)
        return;
    m_elapsedMs = m_elapsed.elapsed();
    setState(State::Finished);
    emit progressChanged();
    emit finished();
}

/*!
 * \internal
 * \brief Stores \a state and emits stateChanged() on a change.
 */
void NetworkScanner::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}
