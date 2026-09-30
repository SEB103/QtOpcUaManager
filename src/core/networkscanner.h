#ifndef NETWORKSCANNER_H
#define NETWORKSCANNER_H

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QOpcUaApplicationDescription>
#include <QOpcUaEndpointDescription>
#include <QtOpcUa/qopcuatype.h>
#include <QUrl>

#include <optional>

#include "networkscanresult.h"

class QOpcUaClient;
class QOpcUaProvider;
class QTcpSocket;
class QTimer;

/**
 * Finds OPC UA servers by probing TCP ports and then querying the open ones.
 *
 * Stage 1 connects to every address/port pair with a bounded pool of sockets.
 * Stage 2 sends FindServers and GetEndpoints to each open port, one at a time,
 * on a private QOpcUaClient, so the application's session is never touched.
 * Lives in the GUI thread; all work is asynchronous, but destruction may wait up
 * to about five seconds for a client whose backend thread is still in a request.
 * The private QOpcUaProvider is kept until the process ends, because deleting it
 * would also delete the backend plugin that other providers share.
 */
class NetworkScanner : public QObject
{
    Q_OBJECT

public:
    /** Scan lifecycle state; the integer values are used by QML. */
    enum class State { Idle = 0, Scanning = 1, Finished = 2, Cancelled = 3 };
    Q_ENUM(State)

    /** Creates an idle scanner owned by \a parent. */
    explicit NetworkScanner(QObject *parent = nullptr);
    /** Stops all work and releases the private OPC UA clients; may block up to about 5 s. */
    ~NetworkScanner() override;

    /** Scans every combination of \a addresses and \a ports; a running scan is cancelled first. */
    void start(const QList<QHostAddress> &addresses, const QList<quint16> &ports);
    /** Stops the running scan; results reported so far stay valid. */
    void cancel();

    /** Returns the current state. */
    State state() const;
    /** Returns the number of address/port pairs of the current scan. */
    int totalTargets() const;
    /** Returns the number of pairs whose TCP probe has completed. */
    int probedTargets() const;
    /** Returns the number of pairs that accepted a TCP connection. */
    int openPorts() const;
    /** Returns the number of pairs where an OPC UA server answered. */
    int opcUaServers() const;
    /** Returns the scan duration so far, or the final duration after it ended. */
    qint64 elapsedMs() const;

    /** Sets the TCP connect timeout per pair in milliseconds (default 400). */
    void setProbeTimeoutMs(int timeoutMs);
    /** Sets the maximum number of concurrent TCP connects (default 64). */
    void setMaxParallelProbes(int count);
    /** Sets the timeout per OPC UA request in milliseconds (default 3000). */
    void setOpcUaTimeoutMs(int timeoutMs);

signals:
    /** Emitted when a row is found or its OPC UA details change. */
    void resultChanged(const NetworkScanResult &result);
    /** Emitted when any progress counter changes. */
    void progressChanged();
    /** Emitted when the state changes to \a state. */
    void stateChanged(NetworkScanner::State state);
    /** Emitted once when a scan completes without being cancelled. */
    void finished();

private:
    /** One address/port pair to probe. */
    struct Target
    {
        /** IPv4 address to connect to. */
        QHostAddress address;
        /** TCP port to connect to. */
        quint16 port = 0;
    };

    /** A running TCP probe. */
    struct Probe
    {
        /** The address/port pair being probed. */
        Target target;
        /** Measures the connect time reported as the response time. */
        QElapsedTimer timer;
        /** Single-shot connect timeout, owned by the probe's socket. */
        QTimer *timeout = nullptr;
    };

    /** Starts TCP probes until the pool is full or no target is left. */
    void launchProbes();
    /** Records the open port of \a socket and queues it for the OPC UA stage. */
    void onProbeConnected(QTcpSocket *socket);
    /** Ends the probe on \a socket and starts the next one. */
    void finishProbe(QTcpSocket *socket);
    /** Sends FindServers for the next queued open port, if none is in flight. */
    void queryNext();
    /** Stores the server description from FindServers and requests the endpoints. */
    void onFindServersFinished(const QList<QOpcUaApplicationDescription> &servers,
                               QOpcUa::UaStatusCode statusCode, const QUrl &requestUrl);
    /** Summarizes the endpoints from GetEndpoints and completes the current query. */
    void onEndpointsFinished(const QList<QOpcUaEndpointDescription> &endpoints,
                             QOpcUa::UaStatusCode statusCode, const QUrl &requestUrl);
    /** Completes the current query after its OPC UA request timed out. */
    void onOpcUaTimeout();
    /** Reports the current row with \a status and \a errorText and moves on. */
    void completeQuery(NetworkScanStatus status, const QString &errorText);
    /** Creates the private OPC UA client on first use; returns false without a backend. */
    bool ensureClient();
    /** Gives every running probe its full timeout again after a thread stall. */
    void restartProbeTimeouts();
    /** Detaches the current client and deletes it once its pending request ends. */
    void retireClient();
    /** Closes all probes and drops queued and in-flight OPC UA queries. */
    void abortAll();
    /** Switches to Finished when all probes and queries are done. */
    void checkFinished();
    /** Stores \a state and emits stateChanged() on a change. */
    void setState(State state);

    /** Current scan state. */
    State m_state = State::Idle;
    /** All address/port pairs of the current scan. */
    QList<Target> m_targets;
    /** Index of the next target in m_targets to probe. */
    qsizetype m_nextTarget = 0;
    /** Running TCP probes by socket. */
    QHash<QTcpSocket *, Probe> m_probes;
    /** Open ports waiting for the OPC UA stage. */
    QList<NetworkScanResult> m_queryQueue;
    /** Row whose OPC UA query is in flight, if any. */
    std::optional<NetworkScanResult> m_current;
    /** URL of the in-flight request; replies for other URLs are ignored. */
    QUrl m_currentRequestUrl;
    /** Provider of the private clients; never deleted, see the destructor. */
    QOpcUaProvider *m_provider = nullptr;
    /** Client used for the next or in-flight query, or null. */
    QOpcUaClient *m_client = nullptr;
    /** Clients still blocked in a request; deleted when the request ends. */
    QList<QOpcUaClient *> m_retiredClients;
    /** Single-shot timeout of the in-flight OPC UA request. */
    QTimer *m_opcUaTimer = nullptr;
    /** Measures the duration of the running scan. */
    QElapsedTimer m_elapsed;
    /** Final duration of the last completed or cancelled scan. */
    qint64 m_elapsedMs = 0;
    /** Number of targets whose TCP probe has completed. */
    int m_probed = 0;
    /** Number of targets that accepted a TCP connection. */
    int m_openPorts = 0;
    /** Number of targets where an OPC UA server answered. */
    int m_opcUaServers = 0;
    /** TCP connect timeout per target in milliseconds. */
    int m_probeTimeoutMs = 400;
    /** Maximum number of concurrent TCP probes. */
    int m_maxParallelProbes = 64;
    /** Scanner timeout per OPC UA request in milliseconds. */
    int m_opcUaTimeoutMs = 3000;
};

#endif // NETWORKSCANNER_H
