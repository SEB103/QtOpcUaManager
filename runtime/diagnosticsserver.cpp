#include "diagnosticsserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

namespace {
/*! Interval between diagnostics publications. */
constexpr int kPublishIntervalMs = 1000;
} // namespace

/*!
 * \brief Creates the diagnostics server and wires the publish timer.
 */
DiagnosticsServer::DiagnosticsServer(UA_Server *server, const QString &pipeName, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_local(new QLocalServer(this))
    , m_pipeName(pipeName)
{
    connect(m_local, &QLocalServer::newConnection, this, &DiagnosticsServer::onNewConnection);
    m_timer.setInterval(kPublishIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &DiagnosticsServer::publish);
}

DiagnosticsServer::~DiagnosticsServer() = default;

/*!
 * \brief Starts listening on the pipe, clearing any stale server first.
 */
bool DiagnosticsServer::listen()
{
    QLocalServer::removeServer(m_pipeName);
    if (!m_local->listen(m_pipeName))
        return false;
    m_uptime.start();
    m_timer.start();
    return true;
}

/*!
 * \brief Accepts a new diagnostics client and drops it on disconnect.
 */
void DiagnosticsServer::onNewConnection()
{
    while (QLocalSocket *socket = m_local->nextPendingConnection()) {
        m_clients.append(socket);
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            m_clients.removeAll(socket);
            socket->deleteLater();
        });
        // Send an immediate snapshot so the GUI does not wait a full interval.
        publish();
    }
}

/*!
 * \brief Publishes one diagnostics JSON line to every connected client.
 */
void DiagnosticsServer::publish()
{
    if (m_clients.isEmpty())
        return;

    const UA_ServerStatistics stats = UA_Server_getStatistics(m_server);
    QJsonObject object;
    object["type"] = QStringLiteral("diagnostics");
    object["sessions"] = static_cast<double>(stats.ss.currentSessionCount);
    object["cumulatedSessions"] = static_cast<double>(stats.ss.cumulatedSessionCount);
    object["channels"] = static_cast<double>(stats.scs.currentChannelCount);
    object["uptimeMs"] = static_cast<double>(m_uptime.elapsed());

    QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact);
    line.append('\n');
    for (QLocalSocket *socket : std::as_const(m_clients)) {
        if (socket->state() == QLocalSocket::ConnectedState) {
            socket->write(line);
            socket->flush();
        }
    }
}
