// Integration test for Phase 4: a simulated variable's value changes over time
// and a subscribed client receives ordinary DataChange notifications, exactly
// as from a real external server.

#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaMonitoringParameters>
#include <QOpcUaNode>
#include <QOpcUaProvider>
#include <QOpcUaReferenceDescription>

#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectserializer.h"

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

using namespace ServerProject;

/*! Verifies simulated values propagate through an ordinary subscription. */
class ServerSimulationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /*! A Counter-simulated variable delivers changing subscription updates. */
    void simulatedValueChanges();

    void cleanupTestCase();

private:
    QString findChild(const QString &parentNodeId, const QString &name);

    QProcess m_process;
    QTemporaryDir m_dir;
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;
    static constexpr quint16 kPort = 48415;
};

void ServerSimulationTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");
    QVERIFY(m_dir.isValid());

    // A Double sensor incremented by 10 every 200 ms.
    ProjectData project;
    project.displayName = QStringLiteral("Simulation");
    project.server.endpoint.port = kPort;
    project.namespaces.append({QStringLiteral("urn:opcuamanager:sim")});

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=Sensors");
    folder.browseName = QStringLiteral("Sensors");
    folder.displayName = QStringLiteral("Sensors");
    project.nodes.append(folder);

    Node sensor;
    sensor.kind = NodeKind::Variable;
    sensor.nodeId = QStringLiteral("ns=1;s=Sensors.Sensor");
    sensor.parentNodeId = folder.nodeId;
    sensor.browseName = QStringLiteral("Sensor");
    sensor.displayName = QStringLiteral("Sensor");
    sensor.dataType = QStringLiteral("Double");
    sensor.valueRank = -1;
    sensor.initialValue = 0.0;
    sensor.simulation.kind = SimulationKind::Counter;
    sensor.simulation.intervalMs = 200.0;
    sensor.simulation.min = 0.0;
    sensor.simulation.max = 1000.0;
    sensor.simulation.step = 10.0;
    project.nodes.append(sensor);

    const QString projectPath = m_dir.filePath(QStringLiteral("sim.uaserver"));
    const Serializer::SaveResult saved = Serializer::save(projectPath, project);
    QVERIFY2(saved.ok, qPrintable(saved.errorString));

    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start(runtimePath,
                    {QStringLiteral("--project"), projectPath,
                     QStringLiteral("--port"), QString::number(kPort)},
                    QIODevice::ReadWrite | QIODevice::Text);
    QVERIFY2(m_process.waitForStarted(5000), "runtime failed to start");

    bool ready = false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !ready) {
        if (m_process.waitForReadyRead(1000)) {
            if (QString::fromUtf8(m_process.readAllStandardOutput())
                    .contains(QLatin1String("READY endpoint=")))
                ready = true;
        }
        if (m_process.state() == QProcess::NotRunning)
            break;
    }
    QVERIFY2(ready, "runtime did not become ready");

    m_client = m_provider.createClient(QStringLiteral("open62541"));
    QVERIFY(m_client);
    const QString url = QStringLiteral("opc.tcp://127.0.0.1:%1").arg(kPort);
    QSignalSpy endpointsSpy(m_client, &QOpcUaClient::endpointsRequestFinished);
    m_client->requestEndpoints(QUrl(url));
    QVERIFY2(endpointsSpy.wait(10000), "no endpoints response");
    const auto endpoints =
        endpointsSpy.takeFirst().at(0).value<QList<QOpcUaEndpointDescription>>();
    QVERIFY(!endpoints.isEmpty());
    QSignalSpy connectedSpy(m_client, &QOpcUaClient::connected);
    m_client->connectToEndpoint(endpoints.first());
    QVERIFY2(connectedSpy.wait(10000), "client did not connect");
}

QString ServerSimulationTest::findChild(const QString &parentNodeId, const QString &name)
{
    std::unique_ptr<QOpcUaNode> parent(m_client->node(parentNodeId));
    if (!parent)
        return {};
    QSignalSpy browseSpy(parent.get(), &QOpcUaNode::browseFinished);
    parent->browseChildren();
    if (!browseSpy.wait(5000))
        return {};
    const auto refs = browseSpy.takeFirst().at(0).value<QList<QOpcUaReferenceDescription>>();
    for (const QOpcUaReferenceDescription &ref : refs) {
        if (ref.browseName().name() == name)
            return ref.targetNodeId().nodeId();
    }
    return {};
}

void ServerSimulationTest::simulatedValueChanges()
{
    QVERIFY(m_client);
    const QString sensorsId = findChild(QStringLiteral("ns=0;i=85"), QStringLiteral("Sensors"));
    QVERIFY2(!sensorsId.isEmpty(), "Sensors folder not found");
    const QString sensorId = findChild(sensorsId, QStringLiteral("Sensor"));
    QVERIFY2(!sensorId.isEmpty(), "Sensor variable not found");

    std::unique_ptr<QOpcUaNode> sensor(m_client->node(sensorId));
    QVERIFY(sensor);

    QSignalSpy dataChangeSpy(sensor.get(), &QOpcUaNode::dataChangeOccurred);
    sensor->enableMonitoring(QOpcUa::NodeAttribute::Value, QOpcUaMonitoringParameters(100.0));
    QVERIFY2(dataChangeSpy.wait(5000), "no initial subscription update");

    // Collect updates over roughly two seconds; the counter must produce several
    // distinct values.
    QSet<double> values;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2500) {
        if (dataChangeSpy.wait(1000)) {
            while (!dataChangeSpy.isEmpty()) {
                const QList<QVariant> args = dataChangeSpy.takeFirst();
                values.insert(args.at(1).toDouble());
            }
        }
    }

    QVERIFY2(values.size() >= 3,
             qPrintable(QStringLiteral("expected several distinct simulated values, got %1")
                            .arg(values.size())));
}

void ServerSimulationTest::cleanupTestCase()
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

QTEST_GUILESS_MAIN(ServerSimulationTest)

#include "tst_serversimulation.moc"
