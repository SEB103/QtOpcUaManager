// Integration tests for Phase 3: the runtime publishes live diagnostics over
// its local-socket control channel, and a client can reconnect after the
// runtime process is killed and restarted (the failure-testing workflow the
// separate-process architecture enables).

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaProvider>

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

/*! Verifies live diagnostics and the kill/reconnect workflow. */
class ServerDiagnosticsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /*! A connected session is reflected in the diagnostics channel. */
    void diagnosticsReportSession();

    /*! A client reconnects after the runtime is killed and restarted. */
    void reconnectAfterKill();

    void cleanup();

private:
    /*! Starts the runtime on \a port (fixed address space); captures the pipe. */
    bool launch(quint16 port);

    /*! Stops the runtime process if running. */
    void stopRuntime();

    /*! Connects \a client to the runtime on \a port; returns whether connected. */
    bool connectClient(QOpcUaClient *client, quint16 port);

    QProcess m_process;
    QString m_runtimePath;
    QString m_pipeName;
    QOpcUaProvider m_provider;
    bool m_available = false;
};

void ServerDiagnosticsTest::initTestCase()
{
    m_runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    m_available = !m_runtimePath.isEmpty() && QFileInfo::exists(m_runtimePath)
                  && m_provider.availableBackends().contains(QStringLiteral("open62541"));
    if (!m_available)
        QSKIP("OpcUaServerRuntime or open62541 client backend is not available.");
}

bool ServerDiagnosticsTest::launch(quint16 port)
{
    m_pipeName.clear();
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start(m_runtimePath,
                    {QStringLiteral("--port"), QString::number(port)},
                    QIODevice::ReadWrite | QIODevice::Text);
    if (!m_process.waitForStarted(5000))
        return false;

    // Wait for both the READY line and the CONTROL pipe line, which the runtime
    // prints as separate writes, so neither is missed by an early exit.
    bool ready = false;
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !(ready && !m_pipeName.isEmpty())) {
        if (m_process.waitForReadyRead(1000))
            buffer += m_process.readAllStandardOutput();
        for (const QByteArray &line : buffer.split('\n')) {
            if (line.startsWith("READY endpoint="))
                ready = true;
            else if (line.startsWith("CONTROL pipe="))
                m_pipeName = QString::fromUtf8(line.mid(int(qstrlen("CONTROL pipe=")))).trimmed();
        }
        if (m_process.state() == QProcess::NotRunning)
            break;
    }
    return ready;
}

void ServerDiagnosticsTest::stopRuntime()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.write("STOP\n");
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
}

bool ServerDiagnosticsTest::connectClient(QOpcUaClient *client, quint16 port)
{
    const QString url = QStringLiteral("opc.tcp://127.0.0.1:%1").arg(port);
    QSignalSpy endpointsSpy(client, &QOpcUaClient::endpointsRequestFinished);
    client->requestEndpoints(QUrl(url));
    if (!endpointsSpy.wait(10000))
        return false;
    const auto endpoints =
        endpointsSpy.takeFirst().at(0).value<QList<QOpcUaEndpointDescription>>();
    if (endpoints.isEmpty())
        return false;
    QSignalSpy connectedSpy(client, &QOpcUaClient::connected);
    client->connectToEndpoint(endpoints.first());
    return connectedSpy.wait(10000);
}

void ServerDiagnosticsTest::diagnosticsReportSession()
{
    const quint16 port = 48413;
    QVERIFY2(launch(port), "runtime did not become ready");
    QVERIFY2(!m_pipeName.isEmpty(), "runtime did not announce a control pipe");

    std::unique_ptr<QOpcUaClient> client(m_provider.createClient(QStringLiteral("open62541")));
    QVERIFY(client);
    QVERIFY2(connectClient(client.get(), port), "client did not connect");

    // Connect to the diagnostics channel after the session exists, so the
    // immediate snapshot already reflects it.
    QLocalSocket socket;
    socket.connectToServer(m_pipeName);
    QVERIFY2(socket.waitForConnected(3000), "could not connect to the diagnostics pipe");

    int reportedSessions = -1;
    qint64 uptimeMs = -1;
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 6000 && reportedSessions < 1) {
        if (!socket.waitForReadyRead(1500))
            continue;
        buffer += socket.readAll();
        int newline = buffer.indexOf('\n');
        while (newline >= 0) {
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            const QJsonObject object = QJsonDocument::fromJson(line).object();
            if (object.value(QStringLiteral("type")).toString() == QLatin1String("diagnostics")) {
                reportedSessions = object.value(QStringLiteral("sessions")).toInt();
                uptimeMs = qint64(object.value(QStringLiteral("uptimeMs")).toDouble());
            }
            newline = buffer.indexOf('\n');
        }
    }

    QVERIFY2(reportedSessions >= 1, "diagnostics did not report the connected session");
    QVERIFY2(uptimeMs >= 0, "diagnostics did not report uptime");

    client->disconnectFromEndpoint();
}

void ServerDiagnosticsTest::reconnectAfterKill()
{
    const quint16 port = 48414;
    QVERIFY2(launch(port), "runtime did not become ready");

    std::unique_ptr<QOpcUaClient> client(m_provider.createClient(QStringLiteral("open62541")));
    QVERIFY(client);
    QVERIFY2(connectClient(client.get(), port), "client did not connect");

    // Kill the runtime: the client must observe a real connection loss.
    QSignalSpy disconnectedSpy(client.get(), &QOpcUaClient::disconnected);
    m_process.kill();
    QVERIFY2(m_process.waitForFinished(5000), "runtime did not terminate");
    QVERIFY2(disconnectedSpy.wait(15000), "client did not detect the connection loss");

    // Restart the runtime on the same port and reconnect.
    QVERIFY2(launch(port), "runtime did not restart");
    QVERIFY2(connectClient(client.get(), port), "client did not reconnect after restart");

    client->disconnectFromEndpoint();
}

void ServerDiagnosticsTest::cleanup()
{
    stopRuntime();
}

QTEST_GUILESS_MAIN(ServerDiagnosticsTest)

#include "tst_serverdiagnostics.moc"
