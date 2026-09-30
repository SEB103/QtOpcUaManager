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
 * Lives in the GUI thread; all work is asynchronous.
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
    /** Stops all work and releases the private OPC UA client. */
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
        QHostAddress address;
        quint16 port = 0;
    };

    /** A running TCP probe. */
    struct Probe
    {
        Target target;
        QElapsedTimer timer;
        /** Single-shot connect timeout, owned by the probe's socket. */
        QTimer *timeout = nullptr;
    };

    void launchProbes();
    void onProbeConnected(QTcpSocket *socket);
    void finishProbe(QTcpSocket *socket);
    void queryNext();
    void onFindServersFinished(const QList<QOpcUaApplicationDescription> &servers,
                               QOpcUa::UaStatusCode statusCode, const QUrl &requestUrl);
    void onEndpointsFinished(const QList<QOpcUaEndpointDescription> &endpoints,
                             QOpcUa::UaStatusCode statusCode, const QUrl &requestUrl);
    void onOpcUaTimeout();
    void completeQuery(NetworkScanStatus status, const QString &errorText);
    bool ensureClient();
    /** Gives every running probe its full timeout again after a thread stall. */
    void restartProbeTimeouts();
    void retireClient();
    void abortAll();
    void checkFinished();
    void setState(State state);

    State m_state = State::Idle;
    QList<Target> m_targets;
    qsizetype m_nextTarget = 0;
    QHash<QTcpSocket *, Probe> m_probes;
    QList<NetworkScanResult> m_queryQueue;
    std::optional<NetworkScanResult> m_current;
    QUrl m_currentRequestUrl;
    QOpcUaProvider *m_provider = nullptr;
    QOpcUaClient *m_client = nullptr;
    QList<QOpcUaClient *> m_retiredClients;
    QTimer *m_opcUaTimer = nullptr;
    QElapsedTimer m_elapsed;
    qint64 m_elapsedMs = 0;
    int m_probed = 0;
    int m_openPorts = 0;
    int m_opcUaServers = 0;
    int m_probeTimeoutMs = 400;
    int m_maxParallelProbes = 64;
    int m_opcUaTimeoutMs = 3000;
};

#endif // NETWORKSCANNER_H
