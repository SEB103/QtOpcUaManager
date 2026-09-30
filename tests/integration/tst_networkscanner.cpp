// Integration test for the two-stage network scanner. It scans 127.0.0.1 on
// three ports: one served by the headless OpcUaServerRuntime, one held by a
// QTcpServer that accepts connections but never speaks OPC UA, and one closed.

#include <QHash>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

#include <QOpcUaProvider>

#include "core/networkscanner.h"

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

/*! Drives NetworkScanner against real local listeners. */
class NetworkScannerIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Starts the runtime, the silent listener, and reserves a closed port. */
    void initTestCase();

    /*! Verifies OPC UA, silent, and closed ports are classified correctly. */
    void classifiesServerSilentAndClosedPorts();

    /*! Verifies a server is still found when a silent port is queried before it. */
    void findsServerAfterSilentPort();

    /*! Verifies that cancelling stops the scan without further results. */
    void cancelStopsTheScan();

    /*! Stops the runtime process. */
    void cleanupTestCase();

private:
    /*! Runs a scan of 127.0.0.1 on \a ports and returns the last result per port. */
    QHash<quint16, NetworkScanResult> scanLocalhost(NetworkScanner &scanner,
                                                    const QList<quint16> &ports);

    /*! The runtime child process serving the OPC UA endpoint. */
    QProcess m_process;
    /*! Accepts TCP connections but never answers. */
    QTcpServer m_silentServer;
    /*! A port that was free when the test started and is not listened on. */
    quint16 m_closedPort = 0;

    /*! Port the runtime listens on for this test run. */
    static constexpr quint16 kPort = 48440;
};

void NetworkScannerIntegrationTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    QOpcUaProvider provider;
    if (!provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");

    // The runtime stops when its stdin closes, so keep the write channel open.
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start(runtimePath, {QStringLiteral("--port"), QString::number(kPort)},
                    QIODevice::ReadWrite | QIODevice::Text);
    QVERIFY2(m_process.waitForStarted(5000), "runtime failed to start");
    bool ready = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !ready) {
        if (m_process.waitForReadyRead(1000))
            ready = QString::fromUtf8(m_process.readAllStandardOutput())
                        .contains(QLatin1String("READY endpoint="));
        if (m_process.state() == QProcess::NotRunning)
            break;
    }
    QVERIFY2(ready, "runtime did not announce a ready endpoint");

    QVERIFY(m_silentServer.listen(QHostAddress::LocalHost, 0));

    QTcpServer reserve;
    QVERIFY(reserve.listen(QHostAddress::LocalHost, 0));
    m_closedPort = reserve.serverPort();
    reserve.close();
}

QHash<quint16, NetworkScanResult> NetworkScannerIntegrationTest::scanLocalhost(
    NetworkScanner &scanner, const QList<quint16> &ports)
{
    QHash<quint16, NetworkScanResult> results;
    // The connection captures a local by reference, so it is removed before returning.
    const QMetaObject::Connection connection =
        connect(&scanner, &NetworkScanner::resultChanged, this,
                [&results](const NetworkScanResult &result) { results.insert(result.port, result); });
    QSignalSpy finishedSpy(&scanner, &NetworkScanner::finished);
    scanner.start({QHostAddress(QHostAddress::LocalHost)}, ports);
    if (!finishedSpy.wait(20000))
        qWarning() << "scan did not finish in time";
    disconnect(connection);
    return results;
}

/*!
 * \brief Verifies OPC UA, silent, and closed ports are classified correctly.
 */
void NetworkScannerIntegrationTest::classifiesServerSilentAndClosedPorts()
{
    NetworkScanner scanner;
    scanner.setOpcUaTimeoutMs(1500);
    const quint16 silentPort = m_silentServer.serverPort();

    const auto results = scanLocalhost(scanner, {kPort, silentPort, m_closedPort});

    QCOMPARE(int(scanner.state()), int(NetworkScanner::State::Finished));
    QCOMPARE(scanner.totalTargets(), 3);
    QCOMPARE(scanner.probedTargets(), 3);
    QCOMPARE(scanner.openPorts(), 2);
    QCOMPARE(scanner.opcUaServers(), 1);

    QVERIFY(results.contains(kPort));
    const NetworkScanResult server = results.value(kPort);
    QCOMPARE(int(server.status), int(NetworkScanStatus::OpcUa));
    QVERIFY(!server.applicationName.isEmpty());
    QVERIFY(server.securitySummary.contains(QStringLiteral("None")));
    QVERIFY(server.authSummary.contains(QStringLiteral("Anonymous")));
    QVERIFY(server.responseMs >= 0);

    QVERIFY(results.contains(silentPort));
    QCOMPARE(int(results.value(silentPort).status), int(NetworkScanStatus::NoOpcUaResponse));
    QVERIFY(!results.value(silentPort).errorText.isEmpty());

    QVERIFY(!results.contains(m_closedPort));
}

/*!
 * \brief Verifies a server is still found when a silent port is queried before it.
 *
 * A single parallel probe makes the silent port open, and be queried, first. Its
 * request blocks the backend thread of that client for longer than the scanner
 * timeout, so the server row is only correct if the scanner moves on to a fresh
 * client after the timeout.
 */
void NetworkScannerIntegrationTest::findsServerAfterSilentPort()
{
    NetworkScanner scanner;
    scanner.setOpcUaTimeoutMs(1500);
    scanner.setMaxParallelProbes(1);
    const quint16 silentPort = m_silentServer.serverPort();

    const auto results = scanLocalhost(scanner, {silentPort, kPort});

    QCOMPARE(int(scanner.state()), int(NetworkScanner::State::Finished));
    QCOMPARE(scanner.openPorts(), 2);
    QCOMPARE(scanner.opcUaServers(), 1);

    QVERIFY(results.contains(silentPort));
    QCOMPARE(int(results.value(silentPort).status), int(NetworkScanStatus::NoOpcUaResponse));

    QVERIFY(results.contains(kPort));
    const NetworkScanResult server = results.value(kPort);
    QCOMPARE(int(server.status), int(NetworkScanStatus::OpcUa));
    QVERIFY(!server.applicationName.isEmpty());
    QVERIFY(server.securitySummary.contains(QStringLiteral("None")));
    QVERIFY(server.authSummary.contains(QStringLiteral("Anonymous")));
}

/*!
 * \brief Verifies that cancelling stops the scan without further results.
 *
 * The wait after cancel() exceeds the backend's fixed discovery timeout, so the
 * late reply of the retired client is delivered and must not produce a result.
 */
void NetworkScannerIntegrationTest::cancelStopsTheScan()
{
    NetworkScanner scanner;
    scanner.setOpcUaTimeoutMs(1500);
    int resultsAfterCancel = 0;
    bool cancelled = false;
    connect(&scanner, &NetworkScanner::resultChanged, this,
            [&](const NetworkScanResult &) { if (cancelled) ++resultsAfterCancel; });
    QSignalSpy finishedSpy(&scanner, &NetworkScanner::finished);

    // The silent port opens at once and then blocks in the OPC UA stage.
    scanner.start({QHostAddress(QHostAddress::LocalHost)}, {m_silentServer.serverPort()});
    QTRY_COMPARE_WITH_TIMEOUT(scanner.openPorts(), 1, 3000);
    scanner.cancel();
    cancelled = true;

    QCOMPARE(int(scanner.state()), int(NetworkScanner::State::Cancelled));
    QTest::qWait(6500);
    QCOMPARE(resultsAfterCancel, 0);
    QCOMPARE(finishedSpy.size(), 0);
}

void NetworkScannerIntegrationTest::cleanupTestCase()
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

QTEST_GUILESS_MAIN(NetworkScannerIntegrationTest)

#include "tst_networkscanner.moc"
