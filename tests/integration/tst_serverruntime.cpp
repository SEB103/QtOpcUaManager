// Integration test for the headless OpcUaServerRuntime (Phase 0 proof of
// concept). It launches the runtime as a child process and drives a real
// QOpcUaClient against its opc.tcp endpoint, exercising the same connect,
// browse, read, write and subscription paths the application uses. This doubles
// as a deterministic fixture proving the separate-process architecture.

#include <QCoreApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaMonitoringParameters>
#include <QOpcUaNode>
#include <QOpcUaProvider>

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

/*! Drives a real client against the headless runtime process. */
class ServerRuntimeIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Starts the runtime, connects a real client, and waits for the endpoint. */
    void initTestCase();

    /*! Reads the seeded Test.IntValue and expects the fixed initial value. */
    void readsInitialValue();

    /*! Writes Test.IntValue and reads it back to confirm the write took effect. */
    void writesAndReadsBack();

    /*! Subscribes to Test.DoubleValue and receives a data change after a write. */
    void receivesSubscriptionUpdate();

    /*! Disconnects the client and stops the runtime process. */
    void cleanupTestCase();

private:
    /*! Reads the current Value attribute of \a node, returning it or an invalid variant. */
    QVariant readValue(QOpcUaNode *node);

    /*! The runtime child process serving the fixed address space. */
    QProcess m_process;

    /*! Provider and client used to reach the runtime endpoint. */
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;

    /*! Port the runtime listens on for this test run. */
    static constexpr quint16 kPort = 48405;
};

void ServerRuntimeIntegrationTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");

    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");

    // Start the runtime and wait for its READY line so the endpoint is up.
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start(runtimePath,
                    {QStringLiteral("--port"), QString::number(kPort)},
                    QIODevice::ReadWrite | QIODevice::Text);
    QVERIFY2(m_process.waitForStarted(5000), "runtime failed to start");

    bool ready = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !ready) {
        if (m_process.waitForReadyRead(1000)) {
            const QString output = QString::fromUtf8(m_process.readAllStandardOutput());
            if (output.contains(QLatin1String("READY endpoint=")))
                ready = true;
        }
        if (m_process.state() == QProcess::NotRunning)
            break;
    }
    QVERIFY2(ready, "runtime did not announce a ready endpoint");

    m_client = m_provider.createClient(QStringLiteral("open62541"));
    QVERIFY2(m_client, "failed to create the open62541 client");

    const QString url = QStringLiteral("opc.tcp://127.0.0.1:%1").arg(kPort);

    QSignalSpy endpointsSpy(m_client, &QOpcUaClient::endpointsRequestFinished);
    m_client->requestEndpoints(QUrl(url));
    QVERIFY2(endpointsSpy.wait(10000), "no endpoints response");

    const QList<QVariant> endpointArgs = endpointsSpy.takeFirst();
    const auto endpoints = endpointArgs.at(0).value<QList<QOpcUaEndpointDescription>>();
    QVERIFY2(!endpoints.isEmpty(), "runtime advertised no endpoints");

    QSignalSpy connectedSpy(m_client, &QOpcUaClient::connected);
    m_client->connectToEndpoint(endpoints.first());
    QVERIFY2(connectedSpy.wait(10000), "client did not connect");
}

QVariant ServerRuntimeIntegrationTest::readValue(QOpcUaNode *node)
{
    if (!node)
        return {};
    QSignalSpy readSpy(node, &QOpcUaNode::attributeRead);
    node->readAttributes(QOpcUa::NodeAttribute::Value);
    if (!readSpy.wait(5000))
        return {};
    return node->valueAttribute();
}

void ServerRuntimeIntegrationTest::readsInitialValue()
{
    QVERIFY(m_client);
    std::unique_ptr<QOpcUaNode> node(m_client->node(QStringLiteral("ns=1;s=Test.IntValue")));
    QVERIFY2(node, "Test.IntValue node not found");
    QCOMPARE(readValue(node.get()).toInt(), 42);
}

void ServerRuntimeIntegrationTest::writesAndReadsBack()
{
    QVERIFY(m_client);
    std::unique_ptr<QOpcUaNode> node(m_client->node(QStringLiteral("ns=1;s=Test.IntValue")));
    QVERIFY2(node, "Test.IntValue node not found");

    QSignalSpy writeSpy(node.get(), &QOpcUaNode::attributeWritten);
    node->writeAttribute(QOpcUa::NodeAttribute::Value, QVariant(100), QOpcUa::Types::Int32);
    QVERIFY2(writeSpy.wait(5000), "write did not complete");

    QCOMPARE(readValue(node.get()).toInt(), 100);
}

void ServerRuntimeIntegrationTest::receivesSubscriptionUpdate()
{
    QVERIFY(m_client);
    std::unique_ptr<QOpcUaNode> node(m_client->node(QStringLiteral("ns=1;s=Test.DoubleValue")));
    QVERIFY2(node, "Test.DoubleValue node not found");

    QSignalSpy dataChangeSpy(node.get(), &QOpcUaNode::dataChangeOccurred);
    node->enableMonitoring(QOpcUa::NodeAttribute::Value, QOpcUaMonitoringParameters(100.0));
    // The first data change carries the current value once monitoring is active.
    QVERIFY2(dataChangeSpy.wait(5000), "no initial subscription update");

    // A write must produce a further data change delivered over the subscription.
    dataChangeSpy.clear();
    QSignalSpy writeSpy(node.get(), &QOpcUaNode::attributeWritten);
    node->writeAttribute(QOpcUa::NodeAttribute::Value, QVariant(2.71828), QOpcUa::Types::Double);
    QVERIFY2(writeSpy.wait(5000), "write did not complete");
    QVERIFY2(dataChangeSpy.wait(5000), "no subscription update after write");
}

void ServerRuntimeIntegrationTest::cleanupTestCase()
{
    if (m_client) {
        if (m_client->state() == QOpcUaClient::ClientState::Connected) {
            QSignalSpy disconnectedSpy(m_client, &QOpcUaClient::disconnected);
            m_client->disconnectFromEndpoint();
            disconnectedSpy.wait(5000);
        }
        delete m_client;
        m_client = nullptr;
    }

    if (m_process.state() != QProcess::NotRunning) {
        m_process.write("STOP\n");
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
}

QTEST_GUILESS_MAIN(ServerRuntimeIntegrationTest)

#include "tst_serverruntime.moc"
