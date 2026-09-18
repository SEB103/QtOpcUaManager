// Integration test for the behavior rule engine: a client write to a trigger
// variable drives the runtime to write the rule's target variables -- immediate
// copy-of-trigger handshakes (PageRequest -> PageResponse/CurrentPage), a
// delayed literal pulse, and a self-referential rule that must not recurse.

#include <QElapsedTimer>
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

/*! Verifies rules write their targets on client writes to the trigger. */
class ServerRulesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /*! A copy-of-trigger rule echoes the written value to its targets. */
    void copyTriggerHandshake();

    /*! A delayed literal action writes only after its delay elapses. */
    void delayedLiteralPulse();

    /*! A rule whose action targets its own trigger does not recurse. */
    void selfReferentialRuleDoesNotRecurse();

    void cleanupTestCase();

private:
    /*! Adds a writable-configurable scalar variable to \a project. */
    static void addVariable(ProjectData &project, const QString &id, const QString &name,
                            const QString &dataType, bool writable);

    /*! Resolves the node id of the child named \a name under \a parentNodeId. */
    QString findChild(const QString &parentNodeId, const QString &name);

    /*! Reads the Value attribute of \a nodeId, or an invalid variant on failure. */
    QVariant readValue(const QString &nodeId);

    /*! Writes \a value (typed \a type) to the Value attribute of \a nodeId. */
    bool writeValue(const QString &nodeId, const QVariant &value, QOpcUa::Types type);

    QProcess m_process;
    QTemporaryDir m_dir;
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;
    QString m_appId;
    static constexpr quint16 kPort = 48431;
};

void ServerRulesTest::addVariable(ProjectData &project, const QString &id, const QString &name,
                                  const QString &dataType, bool writable)
{
    Node node;
    node.kind = NodeKind::Variable;
    node.nodeId = id;
    node.parentNodeId = QStringLiteral("ns=1;s=App");
    node.browseName = name;
    node.displayName = name;
    node.dataType = dataType;
    node.valueRank = -1;
    node.writable = writable;
    node.initialValue = dataType == QLatin1String("Boolean") ? QVariant(false) : QVariant(0);
    project.nodes.append(node);
}

void ServerRulesTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");
    QVERIFY(m_dir.isValid());

    ProjectData project;
    project.displayName = QStringLiteral("Rules");
    project.server.endpoint.port = kPort;
    project.namespaces.append({QStringLiteral("urn:opcuamanager:rules")});

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=App");
    folder.browseName = QStringLiteral("App");
    folder.displayName = QStringLiteral("App");
    project.nodes.append(folder);

    addVariable(project, QStringLiteral("ns=1;s=App.PageRequest"), QStringLiteral("PageRequest"),
                QStringLiteral("Int32"), true);
    addVariable(project, QStringLiteral("ns=1;s=App.PageResponse"), QStringLiteral("PageResponse"),
                QStringLiteral("Int32"), false);
    addVariable(project, QStringLiteral("ns=1;s=App.CurrentPage"), QStringLiteral("CurrentPage"),
                QStringLiteral("Int32"), false);
    addVariable(project, QStringLiteral("ns=1;s=App.Button"), QStringLiteral("Button"),
                QStringLiteral("Boolean"), true);
    addVariable(project, QStringLiteral("ns=1;s=App.Lamp"), QStringLiteral("Lamp"),
                QStringLiteral("Boolean"), false);
    addVariable(project, QStringLiteral("ns=1;s=App.Toggle"), QStringLiteral("Toggle"),
                QStringLiteral("Boolean"), true);

    // Rule 1: PageRequest -> copy to PageResponse and CurrentPage (immediate).
    Rule pageRule;
    pageRule.triggerNodeId = QStringLiteral("ns=1;s=App.PageRequest");
    pageRule.actions.append({QStringLiteral("ns=1;s=App.PageResponse"),
                             RuleValueMode::CopyTrigger, QVariant(), 0.0});
    pageRule.actions.append({QStringLiteral("ns=1;s=App.CurrentPage"),
                             RuleValueMode::CopyTrigger, QVariant(), 0.0});
    project.rules.append(pageRule);

    // Rule 2: Button -> literal true to Lamp after 300 ms.
    Rule pulseRule;
    pulseRule.triggerNodeId = QStringLiteral("ns=1;s=App.Button");
    pulseRule.actions.append({QStringLiteral("ns=1;s=App.Lamp"),
                              RuleValueMode::Literal, QVariant(true), 300.0});
    project.rules.append(pulseRule);

    // Rule 3: Toggle -> literal false to Toggle itself (must not recurse).
    Rule selfRule;
    selfRule.triggerNodeId = QStringLiteral("ns=1;s=App.Toggle");
    selfRule.actions.append({QStringLiteral("ns=1;s=App.Toggle"),
                             RuleValueMode::Literal, QVariant(false), 0.0});
    project.rules.append(selfRule);

    const QString projectPath = m_dir.filePath(QStringLiteral("rules.uaserver"));
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

    m_appId = findChild(QStringLiteral("ns=0;i=85"), QStringLiteral("App"));
    QVERIFY2(!m_appId.isEmpty(), "App folder not found");
}

QString ServerRulesTest::findChild(const QString &parentNodeId, const QString &name)
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

QVariant ServerRulesTest::readValue(const QString &nodeId)
{
    std::unique_ptr<QOpcUaNode> node(m_client->node(nodeId));
    if (!node)
        return {};
    QSignalSpy spy(node.get(), &QOpcUaNode::attributeRead);
    node->readAttributes(QOpcUa::NodeAttribute::Value);
    if (!spy.wait(5000))
        return {};
    return node->valueAttribute();
}

bool ServerRulesTest::writeValue(const QString &nodeId, const QVariant &value, QOpcUa::Types type)
{
    std::unique_ptr<QOpcUaNode> node(m_client->node(nodeId));
    if (!node)
        return false;
    QSignalSpy spy(node.get(), &QOpcUaNode::attributeWritten);
    node->writeAttribute(QOpcUa::NodeAttribute::Value, value, type);
    if (!spy.wait(5000))
        return false;
    const auto args = spy.takeFirst();
    return args.at(1).value<QOpcUa::UaStatusCode>() == QOpcUa::UaStatusCode::Good;
}

void ServerRulesTest::copyTriggerHandshake()
{
    const QString requestId = findChild(m_appId, QStringLiteral("PageRequest"));
    const QString responseId = findChild(m_appId, QStringLiteral("PageResponse"));
    const QString currentId = findChild(m_appId, QStringLiteral("CurrentPage"));
    QVERIFY(!requestId.isEmpty() && !responseId.isEmpty() && !currentId.isEmpty());

    QVERIFY(writeValue(requestId, 12, QOpcUa::Types::Int32));

    // The rule writes the targets synchronously while the server processes the
    // write, so they are already updated; allow a couple of retries for safety.
    QTRY_COMPARE(readValue(responseId).toInt(), 12);
    QCOMPARE(readValue(currentId).toInt(), 12);
}

void ServerRulesTest::delayedLiteralPulse()
{
    const QString buttonId = findChild(m_appId, QStringLiteral("Button"));
    const QString lampId = findChild(m_appId, QStringLiteral("Lamp"));
    QVERIFY(!buttonId.isEmpty() && !lampId.isEmpty());

    QCOMPARE(readValue(lampId).toBool(), false);
    QVERIFY(writeValue(buttonId, true, QOpcUa::Types::Boolean));

    // The literal write is deferred by 300 ms, so the lamp is still off right
    // after the write and turns on only later.
    QCOMPARE(readValue(lampId).toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(readValue(lampId).toBool(), 3000);
}

void ServerRulesTest::selfReferentialRuleDoesNotRecurse()
{
    const QString toggleId = findChild(m_appId, QStringLiteral("Toggle"));
    QVERIFY(!toggleId.isEmpty());

    // Writing true fires a rule that writes false back to the same node. The
    // engine must not re-trigger on its own write; the final value is false and
    // the server stays responsive.
    QVERIFY(writeValue(toggleId, true, QOpcUa::Types::Boolean));
    QTRY_COMPARE(readValue(toggleId).toBool(), false);

    // A follow-up read proves the server did not hang in a write loop.
    const QString requestId = findChild(m_appId, QStringLiteral("PageRequest"));
    QVERIFY(!requestId.isEmpty());
}

void ServerRulesTest::cleanupTestCase()
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

QTEST_GUILESS_MAIN(ServerRulesTest)

#include "tst_serverrules.moc"
