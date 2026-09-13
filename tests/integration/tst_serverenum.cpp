// Integration test for Phase 5 enumerations: a variable typed as a custom enum
// carries the enum DataType, and the enum type exposes its EnumValues so a
// client can resolve the value names.

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

/*! Verifies a custom enum type and an enum-typed variable are served. */
class ServerEnumTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /*! The enum variable reads back its value, enum DataType and EnumValues. */
    void servesEnumType();

    void cleanupTestCase();

private:
    QString findChild(const QString &parentNodeId, const QString &name);
    QVariant readAttribute(const QString &nodeId, QOpcUa::NodeAttribute attribute);

    QProcess m_process;
    QTemporaryDir m_dir;
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;
    static constexpr quint16 kPort = 48416;
};

void ServerEnumTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");
    QVERIFY(m_dir.isValid());

    ProjectData project;
    project.displayName = QStringLiteral("Enum");
    project.server.endpoint.port = kPort;
    project.namespaces.append({QStringLiteral("urn:opcuamanager:enum")});

    EnumType color;
    color.name = QStringLiteral("Color");
    color.nodeId = QStringLiteral("ns=1;s=Enum.Color");
    color.entries = {{0, QStringLiteral("Red")}, {1, QStringLiteral("Green")},
                     {2, QStringLiteral("Blue")}};
    project.enumTypes.append(color);

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=Colors");
    folder.browseName = QStringLiteral("Colors");
    folder.displayName = QStringLiteral("Colors");
    project.nodes.append(folder);

    Node status;
    status.kind = NodeKind::Variable;
    status.nodeId = QStringLiteral("ns=1;s=Colors.Status");
    status.parentNodeId = folder.nodeId;
    status.browseName = QStringLiteral("Status");
    status.displayName = QStringLiteral("Status");
    status.enumTypeId = color.nodeId;
    status.initialValue = 1; // Green
    project.nodes.append(status);

    const QString projectPath = m_dir.filePath(QStringLiteral("enum.uaserver"));
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

QString ServerEnumTest::findChild(const QString &parentNodeId, const QString &name)
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

QVariant ServerEnumTest::readAttribute(const QString &nodeId, QOpcUa::NodeAttribute attribute)
{
    std::unique_ptr<QOpcUaNode> node(m_client->node(nodeId));
    if (!node)
        return {};
    QSignalSpy readSpy(node.get(), &QOpcUaNode::attributeRead);
    node->readAttributes(attribute);
    if (!readSpy.wait(5000))
        return {};
    return node->attribute(attribute);
}

void ServerEnumTest::servesEnumType()
{
    QVERIFY(m_client);

    const QString folderId = findChild(QStringLiteral("ns=0;i=85"), QStringLiteral("Colors"));
    QVERIFY2(!folderId.isEmpty(), "Colors folder not found");
    const QString statusId = findChild(folderId, QStringLiteral("Status"));
    QVERIFY2(!statusId.isEmpty(), "Status variable not found");

    // The value is the Int32 enum value.
    QCOMPARE(readAttribute(statusId, QOpcUa::NodeAttribute::Value).toInt(), 1);

    // The DataType attribute points at the custom enum type node.
    const QString dataTypeId =
        readAttribute(statusId, QOpcUa::NodeAttribute::DataType).toString();
    QVERIFY2(dataTypeId.contains(QLatin1String("Enum.Color")),
             qPrintable(QStringLiteral("unexpected DataType id: %1").arg(dataTypeId)));

    // The enum type exposes its three EnumValues.
    const QString enumValuesId = findChild(dataTypeId, QStringLiteral("EnumValues"));
    QVERIFY2(!enumValuesId.isEmpty(), "EnumValues property not found on the enum type");
    const QVariant enumValues = readAttribute(enumValuesId, QOpcUa::NodeAttribute::Value);
    QCOMPARE(enumValues.toList().size(), 3);
}

void ServerEnumTest::cleanupTestCase()
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

QTEST_GUILESS_MAIN(ServerEnumTest)

#include "tst_serverenum.moc"
