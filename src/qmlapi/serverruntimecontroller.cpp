#include "serverruntimecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QStringList>

/*!
 * \internal
 * \brief Category for runtime output so it lands in the application log panel.
 *
 * The installed Qt message handler forwards every categorized message into the
 * in-memory LogModel, so simply logging here routes the headless runtime's
 * output into the GUI without any GUI dependency in this controller.
 */
Q_LOGGING_CATEGORY(lcServerRuntime, "serverRuntime")

namespace {
/*! Grace period before a requested stop is escalated to a hard kill. */
constexpr int kGracefulStopTimeoutMs = 4000;
} // namespace

/*!
 * \brief Creates the controller and its idle QProcess, wiring the channels.
 */
ServerRuntimeController::ServerRuntimeController(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    // Merge stderr into stdout so the READY line and open62541's own log output
    // arrive on one ordered stream.
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ServerRuntimeController::drainProcessOutput);
    connect(m_process, &QProcess::finished,
            this, &ServerRuntimeController::handleFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &ServerRuntimeController::handleErrorOccurred);

    m_control = new QLocalSocket(this);
    connect(m_control, &QLocalSocket::readyRead,
            this, &ServerRuntimeController::onControlReadyRead);
    connect(m_control, &QLocalSocket::disconnected,
            this, &ServerRuntimeController::resetDiagnostics);
}

/*!
 * \brief Stops the process, if any, before destruction.
 */
ServerRuntimeController::~ServerRuntimeController()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

/*!
 * \brief Updates the lifecycle \a state and notifies observers on change.
 */
void ServerRuntimeController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

/*!
 * \brief Resolves the runtime executable path.
 *
 * Prefers the OPCUAMANAGER_RUNTIME_PATH environment override (used by tests),
 * then falls back to a sibling of the application executable. Returns an empty
 * string when no candidate exists on disk.
 */
QString ServerRuntimeController::resolveRuntimeExecutable() const
{
    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("OPCUAMANAGER_RUNTIME_PATH"));
    if (!override.isEmpty() && QFileInfo::exists(override))
        return override;

    const QString exeName =
#ifdef Q_OS_WIN
        QStringLiteral("OpcUaServerRuntime.exe");
#else
        QStringLiteral("OpcUaServerRuntime");
#endif
    const QString candidate =
        QDir(QCoreApplication::applicationDirPath()).filePath(exeName);
    return QFileInfo::exists(candidate) ? candidate : QString();
}

/*!
 * \brief Starts the runtime on \a port.
 */
void ServerRuntimeController::start(quint16 port, const QString &projectPath,
                                    const QString &pkiPath)
{
    if (m_state == State::Starting || m_state == State::Running) {
        qCWarning(lcServerRuntime) << "Server runtime is already running.";
        return;
    }

    const QString executable = resolveRuntimeExecutable();
    if (executable.isEmpty()) {
        qCCritical(lcServerRuntime)
            << "OpcUaServerRuntime executable not found next to the application.";
        setState(State::Crashed);
        return;
    }

    m_port = port;
    m_projectPath = projectPath;
    m_pkiPath = pkiPath;
    m_endpointUrl.clear();
    emit endpointUrlChanged();
    m_pendingOutput.clear();
    m_controlBuffer.clear();
    m_stopRequested = false;
    resetDiagnostics();

    QStringList arguments{QStringLiteral("--port"), QString::number(port)};
    if (!projectPath.isEmpty())
        arguments << QStringLiteral("--project") << projectPath;
    if (!pkiPath.isEmpty())
        arguments << QStringLiteral("--pki") << pkiPath;

    setState(State::Starting);
    qCInfo(lcServerRuntime) << "Starting server runtime on port" << port
                            << (projectPath.isEmpty() ? QStringLiteral("(fixed address space)")
                                                      : QStringLiteral("with project %1").arg(projectPath));
    m_process->start(executable, arguments, QIODevice::ReadWrite | QIODevice::Text);
}

/*!
 * \brief Requests a graceful shutdown, escalating to a kill on timeout.
 */
void ServerRuntimeController::stop()
{
    if (m_process->state() == QProcess::NotRunning)
        return;

    m_stopRequested = true;
    setState(State::Stopping);
    qCInfo(lcServerRuntime) << "Stopping server runtime.";

    // Ask the runtime to stop over stdin, then close the write channel so it
    // also sees EOF. If it does not exit in time, escalate to a hard kill.
    m_process->write("STOP\n");
    m_process->closeWriteChannel();
    if (!m_process->waitForFinished(kGracefulStopTimeoutMs)) {
        qCWarning(lcServerRuntime) << "Graceful stop timed out; killing the runtime.";
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

/*!
 * \brief Kills the process immediately.
 */
void ServerRuntimeController::kill()
{
    if (m_process->state() == QProcess::NotRunning)
        return;
    m_stopRequested = true;
    qCWarning(lcServerRuntime) << "Killing server runtime.";
    m_process->kill();
    m_process->waitForFinished(1000);
}

/*!
 * \brief Stops the process (if running) and starts it again on the same port.
 */
void ServerRuntimeController::restart()
{
    const quint16 previousPort = m_port;
    const QString previousProject = m_projectPath;
    const QString previousPki = m_pkiPath;
    if (m_process->state() != QProcess::NotRunning)
        stop();
    start(previousPort, previousProject, previousPki);
}

/*!
 * \brief Consumes buffered output, extracting the endpoint and logging the rest.
 */
void ServerRuntimeController::drainProcessOutput()
{
    m_pendingOutput += QString::fromUtf8(m_process->readAllStandardOutput());

    int newlineIndex = m_pendingOutput.indexOf(QLatin1Char('\n'));
    while (newlineIndex >= 0) {
        QString line = m_pendingOutput.left(newlineIndex);
        m_pendingOutput.remove(0, newlineIndex + 1);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        if (line.startsWith(QLatin1String("READY endpoint="))) {
            m_endpointUrl = line.mid(int(qstrlen("READY endpoint="))).trimmed();
            emit endpointUrlChanged();
            setState(State::Running);
            qCInfo(lcServerRuntime) << "Runtime endpoint ready:" << m_endpointUrl;
        } else if (line.startsWith(QLatin1String("CONTROL pipe="))) {
            connectControlChannel(line.mid(int(qstrlen("CONTROL pipe="))).trimmed());
        } else if (!line.isEmpty()) {
            qCInfo(lcServerRuntime).noquote() << line;
        }

        newlineIndex = m_pendingOutput.indexOf(QLatin1Char('\n'));
    }
}

/*!
 * \brief Maps process termination to Stopped or Crashed.
 */
void ServerRuntimeController::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    drainProcessOutput();

    const bool clean = m_stopRequested
                       || (exitStatus == QProcess::NormalExit && exitCode == 0);
    if (clean) {
        qCInfo(lcServerRuntime) << "Server runtime stopped (exit code" << exitCode << ").";
        setState(State::Stopped);
    } else {
        qCCritical(lcServerRuntime)
            << "Server runtime crashed (exit code" << exitCode << ").";
        setState(State::Crashed);
    }

    m_endpointUrl.clear();
    emit endpointUrlChanged();
    m_stopRequested = false;

    if (m_control->state() != QLocalSocket::UnconnectedState)
        m_control->abort();
    resetDiagnostics();
}

/*!
 * \brief Reports a process-level error, treating a failed start as a crash.
 */
void ServerRuntimeController::handleErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        qCCritical(lcServerRuntime) << "Server runtime failed to start.";
        setState(State::Crashed);
    } else {
        qCWarning(lcServerRuntime) << "Server runtime process error:" << error;
    }
}

/*!
 * \brief Connects the diagnostics control socket to \a pipeName.
 */
void ServerRuntimeController::connectControlChannel(const QString &pipeName)
{
    if (pipeName.isEmpty())
        return;
    if (m_control->state() != QLocalSocket::UnconnectedState)
        m_control->abort();
    m_controlBuffer.clear();
    m_control->connectToServer(pipeName);
}

/*!
 * \brief Parses diagnostics JSON lines from the control socket.
 */
void ServerRuntimeController::onControlReadyRead()
{
    m_controlBuffer += m_control->readAll();

    int newlineIndex = m_controlBuffer.indexOf('\n');
    while (newlineIndex >= 0) {
        const QByteArray line = m_controlBuffer.left(newlineIndex);
        m_controlBuffer.remove(0, newlineIndex + 1);

        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("diagnostics")) {
            m_sessionCount = object.value(QStringLiteral("sessions")).toInt();
            m_channelCount = object.value(QStringLiteral("channels")).toInt();
            m_uptimeMs = static_cast<qint64>(object.value(QStringLiteral("uptimeMs")).toDouble());
            emit diagnosticsChanged();
        }

        newlineIndex = m_controlBuffer.indexOf('\n');
    }
}

/*!
 * \brief Resets diagnostics counters to zero and notifies observers.
 */
void ServerRuntimeController::resetDiagnostics()
{
    if (m_sessionCount == 0 && m_channelCount == 0 && m_uptimeMs == 0)
        return;
    m_sessionCount = 0;
    m_channelCount = 0;
    m_uptimeMs = 0;
    emit diagnosticsChanged();
}
