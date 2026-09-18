// Integration test for the recursive clone browser: CloneBrowser walks the
// whole Objects subtree of a running server and captures folders, variables,
// their parent links and current values (scalars and arrays) -- unlike the lazy
// tree snapshot, without any manual expansion.

#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaProvider>

#include "core/clonebrowser.h"
#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectserializer.h"

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

using namespace ServerProject;

/*! Verifies CloneBrowser captures the full subtree with values. */
class ServerCloneBrowseTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /*! The whole Objects subtree is captured with parents and variable values. */
    void capturesSubtreeWithValues();

    void cleanupTestCase();

private:
    /*! Returns the captured node with browse name \a name, or a default. */
    static CloneNode nodeNamed(const QList<CloneNode> &nodes, const QString &name);

    QProcess m_process;
    QTemporaryDir m_dir;
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;
    static constexpr quint16 kPort = 48432;
};

CloneNode ServerCloneBrowseTest::nodeNamed(const QList<CloneNode> &nodes, const QString &name)
{
    for (const CloneNode &node : nodes) {
        if (node.browseName == name)
            return node;
    }
    return {};
}

void ServerCloneBrowseTest::initTestCase()
{
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");
    QVERIFY(m_dir.isValid());

    ProjectData project;
    project.displayName = QStringLiteral("CloneTest");
    project.server.endpoint.port = kPort;
    project.namespaces.append({QStringLiteral("urn:opcuamanager:clonetest")});

    Node app;
    app.kind = NodeKind::Folder;
    app.nodeId = QStringLiteral("ns=1;s=App");
    app.browseName = QStringLiteral("App");
    app.displayName = QStringLiteral("App");
    project.nodes.append(app);

    Node sub;
    sub.kind = NodeKind::Folder;
    sub.nodeId = QStringLiteral("ns=1;s=App.Sub");
    sub.parentNodeId = app.nodeId;
    sub.browseName = QStringLiteral("Sub");
    sub.displayName = QStringLiteral("Sub");
    project.nodes.append(sub);

    Node count;
    count.kind = NodeKind::Variable;
    count.nodeId = QStringLiteral("ns=1;s=App.Sub.Count");
    count.parentNodeId = sub.nodeId;
    count.browseName = QStringLiteral("Count");
    count.displayName = QStringLiteral("Count");
    count.dataType = QStringLiteral("Int32");
    count.valueRank = -1;
    count.initialValue = 7;
    project.nodes.append(count);

    Node nums;
    nums.kind = NodeKind::Variable;
    nums.nodeId = QStringLiteral("ns=1;s=App.Sub.Nums");
    nums.parentNodeId = sub.nodeId;
    nums.browseName = QStringLiteral("Nums");
    nums.displayName = QStringLiteral("Nums");
    nums.dataType = QStringLiteral("Double");
    nums.valueRank = 1;
    nums.initialValue = QVariantList{1.5, 2.5};
    project.nodes.append(nums);

    const QString projectPath = m_dir.filePath(QStringLiteral("clone.uaserver"));
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
    // The clone reads the server's namespace array; make sure it is populated.
    m_client->updateNamespaceArray();
    QTRY_VERIFY(!m_client->namespaceArray().isEmpty());
}

void ServerCloneBrowseTest::capturesSubtreeWithValues()
{
    QVERIFY(m_client);

    // The browser runs in this thread (the client's thread); drive it directly.
    auto *browser = new CloneBrowser(m_client, QStringLiteral("ns=0;i=85"), 1);
    QSignalSpy finishedSpy(browser, &CloneBrowser::finished);
    browser->start();
    QVERIFY2(finishedSpy.wait(20000), "clone browse did not finish");

    const QList<QVariant> args = finishedSpy.takeFirst();
    const auto nodes = args.at(1).value<QList<CloneNode>>();
    const auto namespaceUris = args.at(2).toStringList();
    const bool success = args.at(3).toBool();
    QVERIFY(success);
    QVERIFY(namespaceUris.contains(QStringLiteral("urn:opcuamanager:clonetest")));

    const CloneNode app = nodeNamed(nodes, QStringLiteral("App"));
    QCOMPARE(app.browseName, QStringLiteral("App"));
    QVERIFY(app.isFolder);
    QVERIFY(app.parentNodeId.isEmpty()); // direct child of Objects

    const CloneNode sub = nodeNamed(nodes, QStringLiteral("Sub"));
    QVERIFY(sub.isFolder);
    QCOMPARE(sub.parentNodeId, app.nodeId);

    const CloneNode count = nodeNamed(nodes, QStringLiteral("Count"));
    QVERIFY(count.isVariable);
    QCOMPARE(count.parentNodeId, sub.nodeId);
    QCOMPARE(count.value.toInt(), 7);

    const CloneNode nums = nodeNamed(nodes, QStringLiteral("Nums"));
    QVERIFY(nums.isVariable);
    QCOMPARE(nums.valueRank, 1);
    const QVariantList list = nums.value.toList();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(1).toDouble(), 2.5);
}

void ServerCloneBrowseTest::cleanupTestCase()
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

QTEST_GUILESS_MAIN(ServerCloneBrowseTest)

#include "tst_serverclonebrowse.moc"
