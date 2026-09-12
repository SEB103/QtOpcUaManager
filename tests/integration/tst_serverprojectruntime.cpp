// Integration test: the runtime builds its address space from a .uaserver
// project and a real client sees the designed nodes. Nodes are located by
// browse name because the runtime remaps project-relative namespace indices to
// whatever open62541 assigns, so the client must not assume a fixed ns index.

#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaNode>
#include <QOpcUaProvider>
#include <QOpcUaReferenceDescription>

#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectserializer.h"

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

using namespace ServerProject;

/*! Verifies the runtime builds a designed address space reachable by a client. */
class ServerProjectRuntimeTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Writes a project, launches the runtime with it, and connects a client. */
    void initTestCase();

    /*! Browses to the designed variable and reads its initial value. */
    void servesDesignedNodes();

    /*! Disconnects and stops the runtime. */
    void cleanupTestCase();

private:
    /*! Returns the node id of the child of \a parent whose browse name is \a name. */
    QString findChild(const QString &parentNodeId, const QString &name);

    QProcess m_process;
    QTemporaryDir m_dir;
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;
    static constexpr quint16 kPort = 48406;
};

void ServerProjectRuntimeTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");
    QVERIFY(m_dir.isValid());

    // Build and save a small project: Demo folder with a writable Int32 "Answer".
    ProjectData project;
    project.displayName = QStringLiteral("Fixture");
    project.server.endpoint.port = kPort;
    project.namespaces.append({QStringLiteral("urn:opcuamanager:fixture")});

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=Demo");
    folder.browseName = QStringLiteral("Demo");
    folder.displayName = QStringLiteral("Demo");
    project.nodes.append(folder);

    Node variable;
    variable.kind = NodeKind::Variable;
    variable.nodeId = QStringLiteral("ns=1;s=Demo.Answer");
    variable.parentNodeId = folder.nodeId;
    variable.browseName = QStringLiteral("Answer");
    variable.displayName = QStringLiteral("Answer");
    variable.dataType = QStringLiteral("Int32");
    variable.valueRank = -1;
    variable.writable = true;
    variable.initialValue = 42;
    project.nodes.append(variable);

    const QString projectPath = m_dir.filePath(QStringLiteral("fixture.uaserver"));
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
    QVERIFY2(ready, "runtime did not announce a ready endpoint");

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

QString ServerProjectRuntimeTest::findChild(const QString &parentNodeId, const QString &name)
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

void ServerProjectRuntimeTest::servesDesignedNodes()
{
    QVERIFY(m_client);

    const QString demoId = findChild(QStringLiteral("ns=0;i=85"), QStringLiteral("Demo"));
    QVERIFY2(!demoId.isEmpty(), "Demo folder not found under Objects");

    const QString answerId = findChild(demoId, QStringLiteral("Answer"));
    QVERIFY2(!answerId.isEmpty(), "Answer variable not found under Demo");

    std::unique_ptr<QOpcUaNode> answer(m_client->node(answerId));
    QVERIFY(answer);
    QSignalSpy readSpy(answer.get(), &QOpcUaNode::attributeRead);
    answer->readAttributes(QOpcUa::NodeAttribute::Value);
    QVERIFY2(readSpy.wait(5000), "value read did not complete");
    QCOMPARE(answer->valueAttribute().toInt(), 42);
}

void ServerProjectRuntimeTest::cleanupTestCase()
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

QTEST_GUILESS_MAIN(ServerProjectRuntimeTest)

#include "tst_serverprojectruntime.moc"
