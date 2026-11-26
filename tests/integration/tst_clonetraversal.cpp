// Integration test for the clone traversal against an in-process open62541
// server whose address space is built for the cases the Server Studio runtime
// cannot produce: more than 5000 nodes, a node reachable through two parents,
// an unreadable variable with a child, a vendor String subtype, and the node
// limit, depth limit, timeout and cancel outcomes.

#include <QSignalSpy>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaProvider>

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <atomic>
#include <cstring>
#include <thread>

#include "core/clonebrowser.h"

using State = CloneCompletion::State;

namespace {

/*!
 * \internal
 * \brief TCP port of the embedded test server.
 */
constexpr UA_UInt16 kPort = 48441;

/*!
 * \internal
 * \brief Number of variables in the Bulk folder; above the former 5000-node cap.
 */
constexpr int kBulkCount = 5200;

/*!
 * \internal
 * \brief Numeric id of the vendor String subtype in the test namespace.
 */
constexpr UA_UInt32 kVendorStringId = 5000;

} // namespace

/*!
 * \internal
 * \brief open62541 server running its event loop in a background thread.
 *
 * Address space in the test namespace:
 * \list
 * \li Objects/Area (folder) with Plant and Other (folders).
 * \li Plant/Shared (folder), also organized by Other.
 * \li Plant/Locked (Int32, AccessLevel 0) with the readable child Locked/Inner.
 * \li Plant/Label (VendorString, a subtype of String, value "abc").
 * \li Objects/Bulk (folder) with kBulkCount Int32 variables.
 * \endlist
 */
class EmbeddedServer
{
public:
    /*!
     * \brief Creates the server and its address space and starts the event loop.
     */
    bool start()
    {
        // Only errors are logged, so the test output stays readable.
        UA_ServerConfig config;
        memset(&config, 0, sizeof(config));
        config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_ERROR);
        if (UA_ServerConfig_setMinimal(&config, kPort, nullptr) != UA_STATUSCODE_GOOD)
            return false;
        m_server = UA_Server_newWithConfig(&config);
        if (!m_server)
            return false;
        m_ns = UA_Server_addNamespace(m_server, "urn:opcuamanager:clonetraversal");
        if (!buildAddressSpace())
            return false;
        if (UA_Server_run_startup(m_server) != UA_STATUSCODE_GOOD)
            return false;
        m_running = true;
        m_thread = std::thread([this]() {
            while (m_running)
                UA_Server_run_iterate(m_server, true);
        });
        return true;
    }

    /*!
     * \brief Stops the event loop and deletes the server.
     */
    void stop()
    {
        if (m_thread.joinable()) {
            m_running = false;
            m_thread.join();
            UA_Server_run_shutdown(m_server);
        }
        if (m_server) {
            UA_Server_delete(m_server);
            m_server = nullptr;
        }
    }

    /*!
     * \brief Returns the Qt OPC UA node id string of \a name in the test namespace.
     */
    QString nodeId(const char *name) const
    {
        return QStringLiteral("ns=%1;s=%2").arg(m_ns).arg(QString::fromLatin1(name));
    }

    /*!
     * \brief Returns the test namespace index.
     */
    UA_UInt16 ns() const { return m_ns; }

private:
    /*!
     * \brief Adds a folder \a name under \a parent.
     */
    bool addFolder(const char *name, const UA_NodeId &parent)
    {
        UA_ObjectAttributes attr = UA_ObjectAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(name));
        return UA_Server_addObjectNode(
                   m_server, UA_NODEID_STRING(m_ns, const_cast<char *>(name)), parent,
                   UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                   UA_QUALIFIEDNAME(m_ns, const_cast<char *>(name)),
                   UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), attr, nullptr, nullptr)
               == UA_STATUSCODE_GOOD;
    }

    /*!
     * \brief Adds the Int32 variable \a name with \a value and \a accessLevel under \a parent.
     */
    bool addInt32(const char *name, const UA_NodeId &parent, UA_Int32 value,
                  UA_Byte accessLevel, const UA_NodeId &referenceType)
    {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(name));
        attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
        attr.valueRank = UA_VALUERANK_SCALAR;
        attr.accessLevel = accessLevel;
        attr.userAccessLevel = accessLevel;
        UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_INT32]);
        return UA_Server_addVariableNode(
                   m_server, UA_NODEID_STRING(m_ns, const_cast<char *>(name)), parent,
                   referenceType, UA_QUALIFIEDNAME(m_ns, const_cast<char *>(name)),
                   UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr, nullptr)
               == UA_STATUSCODE_GOOD;
    }

    /*!
     * \brief Builds the address space described in the class documentation.
     */
    bool buildAddressSpace()
    {
        const UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        const UA_NodeId area = UA_NODEID_STRING(m_ns, const_cast<char *>("Area"));
        const UA_NodeId plant = UA_NODEID_STRING(m_ns, const_cast<char *>("Plant"));
        const UA_NodeId other = UA_NODEID_STRING(m_ns, const_cast<char *>("Other"));
        const UA_NodeId shared = UA_NODEID_STRING(m_ns, const_cast<char *>("Shared"));
        const UA_NodeId locked = UA_NODEID_STRING(m_ns, const_cast<char *>("Locked"));
        const UA_NodeId bulk = UA_NODEID_STRING(m_ns, const_cast<char *>("Bulk"));
        const UA_NodeId hasComponent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        const UA_Byte readWrite = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

        bool ok = addFolder("Area", objects) && addFolder("Plant", area)
                  && addFolder("Other", area) && addFolder("Shared", plant)
                  && addFolder("Bulk", objects);
        // Second hierarchical reference: Other also organizes Shared.
        ok = ok
             && UA_Server_addReference(m_server, other, UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                                       UA_EXPANDEDNODEID_STRING(m_ns, const_cast<char *>("Shared")),
                                       true)
                    == UA_STATUSCODE_GOOD;
        // Unreadable value with a readable child.
        ok = ok && addInt32("Locked", plant, 1, 0, hasComponent)
             && addInt32("Inner", locked, 5, readWrite, hasComponent);

        // Vendor String subtype and a variable of that type.
        UA_DataTypeAttributes typeAttr = UA_DataTypeAttributes_default;
        typeAttr.displayName =
            UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("VendorString"));
        ok = ok
             && UA_Server_addDataTypeNode(
                    m_server, UA_NODEID_NUMERIC(m_ns, kVendorStringId),
                    UA_NODEID_NUMERIC(0, UA_NS0ID_STRING),
                    UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
                    UA_QUALIFIEDNAME(m_ns, const_cast<char *>("VendorString")), typeAttr,
                    nullptr, nullptr)
                    == UA_STATUSCODE_GOOD;
        UA_VariableAttributes labelAttr = UA_VariableAttributes_default;
        labelAttr.displayName =
            UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("Label"));
        labelAttr.dataType = UA_NODEID_NUMERIC(m_ns, kVendorStringId);
        labelAttr.valueRank = UA_VALUERANK_SCALAR;
        labelAttr.accessLevel = readWrite;
        UA_String label = UA_STRING(const_cast<char *>("abc"));
        UA_Variant_setScalar(&labelAttr.value, &label, &UA_TYPES[UA_TYPES_STRING]);
        ok = ok
             && UA_Server_addVariableNode(
                    m_server, UA_NODEID_STRING(m_ns, const_cast<char *>("Label")), plant,
                    hasComponent, UA_QUALIFIEDNAME(m_ns, const_cast<char *>("Label")),
                    UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), labelAttr, nullptr,
                    nullptr)
                    == UA_STATUSCODE_GOOD;

        for (int i = 0; ok && i < kBulkCount; ++i) {
            const QByteArray name = QByteArrayLiteral("Bulk.V") + QByteArray::number(i);
            ok = addInt32(name.constData(), bulk, i, readWrite, hasComponent);
        }
        return ok;
    }

    UA_Server *m_server = nullptr;      //!< The embedded server.
    UA_UInt16 m_ns = 0;                 //!< Index of the test namespace.
    std::atomic<bool> m_running{false}; //!< Keeps the event loop running.
    std::thread m_thread;               //!< Runs the event loop.
};

/*! Verifies completeness reporting and node capture of CloneBrowser. */
class CloneTraversalTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    /*! More than 5000 nodes are captured completely with the default limits. */
    void capturesLargeAddressSpace();

    /*! A node with two hierarchical parents is captured once with both recorded. */
    void recordsAdditionalParents();

    /*! A variable whose value cannot be read is still browsed for its children. */
    void browsesChildrenOfUnreadableVariable();

    /*! A vendor subtype of String resolves to String and keeps its id and name. */
    void resolvesVendorStringSubtype();

    /*! The node limit ends the traversal as Truncated. */
    void reportsNodeLimit();

    /*! The depth limit leaves deep nodes unbrowsed and reports Truncated. */
    void reportsDepthLimit();

    /*! The overall timeout ends the traversal as TimedOut with pending work. */
    void reportsTimeout();

    /*! cancel() ends the traversal as Cancelled. */
    void reportsCancel();

    void cleanupTestCase();

private:
    /*! Result of one traversal. */
    struct Result
    {
        QList<CloneNode> nodes;     //!< Captured nodes.
        CloneCompletion completion; //!< Outcome.
        bool finished = false;      //!< Whether finished() arrived.
    };

    /*! Runs a traversal of \a rootNodeId with \a options; \a cancel cancels right after start. */
    Result run(const QString &rootNodeId, const CloneOptions &options = CloneOptions(),
               bool cancel = false);

    /*! Returns the captured node \a nodeId, or a default node. */
    static CloneNode find(const QList<CloneNode> &nodes, const QString &nodeId);

    EmbeddedServer m_server;
    QOpcUaProvider m_provider;
    QOpcUaClient *m_client = nullptr;
};

CloneTraversalTest::Result CloneTraversalTest::run(const QString &rootNodeId,
                                                   const CloneOptions &options, bool cancel)
{
    Result result;
    auto *browser = new CloneBrowser(m_client, rootNodeId, 1, options);
    QSignalSpy finishedSpy(browser, &CloneBrowser::finished);
    browser->start();
    if (cancel)
        browser->cancel();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(120000))
        return result;
    const QList<QVariant> args = finishedSpy.takeFirst();
    result.nodes = args.at(1).value<QList<CloneNode>>();
    result.completion = args.at(3).value<CloneCompletion>();
    result.finished = true;
    return result;
}

CloneNode CloneTraversalTest::find(const QList<CloneNode> &nodes, const QString &nodeId)
{
    for (const CloneNode &node : nodes) {
        if (node.nodeId == nodeId)
            return node;
    }
    return {};
}

void CloneTraversalTest::initTestCase()
{
    if (!m_provider.availableBackends().contains(QStringLiteral("open62541")))
        QSKIP("The open62541 client backend is not available.");
    QVERIFY2(m_server.start(), "embedded open62541 server failed to start");

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
    m_client->updateNamespaceArray();
    QTRY_VERIFY(!m_client->namespaceArray().isEmpty());
}

void CloneTraversalTest::capturesLargeAddressSpace()
{
    const Result result = run(m_server.nodeId("Bulk"));
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QCOMPARE(result.completion.nodeCount, kBulkCount);
    QCOMPARE(result.completion.pendingRequests, 0);
    QCOMPARE(result.completion.failedReads, 0);
    QCOMPARE(int(result.nodes.size()), kBulkCount);

    const CloneNode last = find(result.nodes, m_server.nodeId("Bulk.V5199"));
    QVERIFY(last.attributesRead);
    QCOMPARE(last.value.toInt(), 5199);
    QCOMPARE(last.resolvedDataType.builtInBase, QStringLiteral("Int32"));
    QVERIFY(last.accessLevel > 0);
}

void CloneTraversalTest::recordsAdditionalParents()
{
    const Result result = run(m_server.nodeId("Area"));
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);

    int sharedCount = 0;
    for (const CloneNode &node : result.nodes) {
        if (node.nodeId == m_server.nodeId("Shared"))
            ++sharedCount;
    }
    QCOMPARE(sharedCount, 1);

    const CloneNode shared = find(result.nodes, m_server.nodeId("Shared"));
    QStringList parents = shared.additionalParentIds;
    parents.append(shared.parentNodeId);
    QVERIFY(parents.contains(m_server.nodeId("Plant")));
    QVERIFY(parents.contains(m_server.nodeId("Other")));
}

void CloneTraversalTest::browsesChildrenOfUnreadableVariable()
{
    const Result result = run(m_server.nodeId("Area"));
    QVERIFY(result.finished);

    const CloneNode locked = find(result.nodes, m_server.nodeId("Locked"));
    QVERIFY(locked.isVariable);
    QVERIFY(locked.attributesRead);
    QVERIFY(!locked.value.isValid());
    QVERIFY(locked.valueStatus != 0);
    QVERIFY2(!locked.attributeErrors.filter(QStringLiteral("Value:")).isEmpty(),
             qPrintable(locked.attributeErrors.join(QStringLiteral("; "))));
    QVERIFY(result.completion.failedReads >= 1);

    const CloneNode inner = find(result.nodes, m_server.nodeId("Inner"));
    QVERIFY(inner.isVariable);
    QCOMPARE(inner.parentNodeId, locked.nodeId);
    QCOMPARE(inner.value.toInt(), 5);
}

void CloneTraversalTest::resolvesVendorStringSubtype()
{
    const Result result = run(m_server.nodeId("Area"));
    QVERIFY(result.finished);

    const CloneNode label = find(result.nodes, m_server.nodeId("Label"));
    QVERIFY(label.isVariable);
    QCOMPARE(label.dataTypeId, QStringLiteral("ns=%1;i=%2").arg(m_server.ns()).arg(kVendorStringId));
    QCOMPARE(label.resolvedDataType.resolution, ResolvedDataType::Resolution::Hierarchy);
    QCOMPARE(label.resolvedDataType.typeName, QStringLiteral("VendorString"));
    QCOMPARE(label.resolvedDataType.builtInBase, QStringLiteral("String"));
    QCOMPARE(label.value.toString(), QStringLiteral("abc"));
    QCOMPARE(label.browseNameNamespace, m_server.ns());
}

void CloneTraversalTest::reportsNodeLimit()
{
    CloneOptions options;
    options.maxNodes = 50;
    const Result result = run(m_server.nodeId("Bulk"), options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Truncated);
    QVERIFY(result.completion.nodeLimitReached);
    QCOMPARE(result.completion.nodeCount, 50);
}

void CloneTraversalTest::reportsDepthLimit()
{
    CloneOptions options;
    options.maxDepth = 1;
    const Result result = run(m_server.nodeId("Area"), options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Truncated);
    QCOMPARE(result.completion.depthLimitedNodes, 2); // Plant and Other
    QVERIFY(find(result.nodes, m_server.nodeId("Shared")).nodeId.isEmpty());
}

void CloneTraversalTest::reportsTimeout()
{
    CloneOptions options;
    options.overallTimeoutMs = 1;
    const Result result = run(m_server.nodeId("Bulk"), options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::TimedOut);
    QVERIFY(result.completion.pendingRequests > 0);
}

void CloneTraversalTest::reportsCancel()
{
    const Result result = run(m_server.nodeId("Bulk"), CloneOptions(), true);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Cancelled);
}

void CloneTraversalTest::cleanupTestCase()
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
    m_server.stop();
}

QTEST_GUILESS_MAIN(CloneTraversalTest)

#include "tst_clonetraversal.moc"
