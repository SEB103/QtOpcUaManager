#ifndef SERVERRUNTIMECONTROLLER_H
#define SERVERRUNTIMECONTROLLER_H

#include <QObject>
#include <QProcess>
#include <QString>

/**
 * Owns and supervises the headless OpcUaServerRuntime child process.
 *
 * The controller is the GUI-side half of the process boundary: it starts,
 * stops, restarts and kills the runtime, tracks its lifecycle state, exposes
 * the actual endpoint the runtime is listening on, and forwards the runtime's
 * stdout/stderr into the application log under the "serverRuntime" category. It
 * never shares mutable state with the runtime; all interaction is through the
 * command line and the process channels.
 */
class ServerRuntimeController : public QObject
{
    Q_OBJECT

public:
    /** Lifecycle state of the supervised runtime process. */
    enum class State {
        Stopped,   /**< No process is running. */
        Starting,  /**< Process launched; endpoint not yet announced. */
        Running,   /**< Endpoint is listening and reachable. */
        Stopping,  /**< Graceful shutdown requested. */
        Crashed    /**< Process exited abnormally or failed to start. */
    };
    Q_ENUM(State)

    /** Creates the controller with no process running. */
    explicit ServerRuntimeController(QObject *parent = nullptr);

    /** Stops the process if still running and releases resources. */
    ~ServerRuntimeController() override;

    /** Returns the current lifecycle state. */
    State state() const { return m_state; }

    /** Returns the endpoint URL announced by the runtime, or empty when unknown. */
    QString endpointUrl() const { return m_endpointUrl; }

    /** Returns the port the controller last started, or the default 4840. */
    quint16 port() const { return m_port; }

    /**
     * Starts the runtime on \a port, optionally serving \a projectPath. Does
     * nothing when a process is already starting or running. Resolves the
     * runtime executable next to the application, overridable through the
     * OPCUAMANAGER_RUNTIME_PATH env var. When \a projectPath is empty the
     * runtime serves its built-in fixed address space.
     */
    void start(quint16 port = 4840, const QString &projectPath = QString());

    /** Requests a graceful shutdown (STOP over stdin), escalating to kill on timeout. */
    void stop();

    /** Terminates the process immediately without a graceful shutdown. */
    void kill();

    /** Stops the process (if running) and starts it again on the same port. */
    void restart();

signals:
    /** Emitted whenever the lifecycle state changes. */
    void stateChanged();

    /** Emitted when the announced endpoint URL changes. */
    void endpointUrlChanged();

private:
    /** Applies \a state and notifies observers when it actually changed. */
    void setState(State state);

    /** Resolves the runtime executable path, or empty when it cannot be found. */
    QString resolveRuntimeExecutable() const;

    /** Consumes buffered process output, routing lines to the log or endpoint. */
    void drainProcessOutput();

    /** Handles process termination and maps it to Stopped or Crashed. */
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /** Handles a process-level error such as a failed start. */
    void handleErrorOccurred(QProcess::ProcessError error);

    /** The supervised child process; owned by this controller. */
    QProcess *m_process = nullptr;

    /** Current lifecycle state. */
    State m_state = State::Stopped;

    /** Endpoint URL parsed from the runtime's READY line. */
    QString m_endpointUrl;

    /** Port most recently used to start the runtime. */
    quint16 m_port = 4840;

    /** Project path most recently used to start the runtime; empty for fixed. */
    QString m_projectPath;

    /** Accumulates partial output lines across readyRead notifications. */
    QString m_pendingOutput;

    /** True while a graceful stop is in progress, so exit is not read as a crash. */
    bool m_stopRequested = false;
};

#endif // SERVERRUNTIMECONTROLLER_H
