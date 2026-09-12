#ifndef DIAGNOSTICSSERVER_H
#define DIAGNOSTICSSERVER_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <open62541/server.h>

QT_BEGIN_NAMESPACE
class QLocalServer;
class QLocalSocket;
QT_END_NAMESPACE

/**
 * Publishes live runtime diagnostics to the GUI over a local socket.
 *
 * The runtime hosts a QLocalServer on a per-instance pipe whose name it prints
 * on stdout. The controller connects and receives a periodic JSON line with the
 * current session and secure-channel counts and the uptime. This is the live
 * side channel of the Manager<->Runtime boundary; the authoritative
 * configuration still travels through the project file.
 */
class DiagnosticsServer : public QObject
{
    Q_OBJECT

public:
    /** Creates the diagnostics server for \a server publishing on \a pipeName. */
    DiagnosticsServer(UA_Server *server, const QString &pipeName, QObject *parent = nullptr);
    ~DiagnosticsServer() override;

    /** Starts listening on the pipe; returns whether it succeeded. */
    bool listen();

    /** Returns the pipe name clients connect to. */
    QString pipeName() const { return m_pipeName; }

private:
    /** Accepts a newly connected diagnostics client. */
    void onNewConnection();

    /** Gathers server statistics and writes one JSON line to every client. */
    void publish();

    /** The open62541 server whose statistics are reported; not owned. */
    UA_Server *m_server = nullptr;

    /** Local-socket server accepting diagnostics clients; owned. */
    QLocalServer *m_local = nullptr;

    /** Pipe name the server listens on. */
    QString m_pipeName;

    /** Connected diagnostics clients. */
    QList<QLocalSocket *> m_clients;

    /** Publish timer. */
    QTimer m_timer;

    /** Uptime clock started when listening begins. */
    QElapsedTimer m_uptime;
};

#endif // DIAGNOSTICSSERVER_H
