// Integration test for the clone traversal against an in-process open62541
// server whose address space is built for the cases the Server Studio runtime
// cannot produce: more than 5000 nodes, a node reachable through two parents,
// an unreadable variable with a child, a vendor String subtype, and the node
// limit, depth limit, timeout and cancel outcomes.

#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaMultiDimensionalArray>
#include <QOpcUaNode>
#include <QOpcUaProvider>
#include <QOpcUaQualifiedName>

#include <open62541/plugin/log_stdout.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include "core/clonebrowser.h"
#include "qmlapi/opcuamanager.h"
#include "qmlapi/serverstudio.h"
#include "serverproject/serverprojectserializer.h"
#include "serverproject/serverprojectvalidator.h"

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

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

/*!
 * \internal
 * \brief Numeric id of the structure type ST_Button (no DataTypeDefinition).
 */
constexpr UA_UInt32 kButtonTypeId = 6000;

/*!
 * \internal
 * \brief TCP port of the Server Studio runtime started from the cloned project.
 */
constexpr quint16 kRuntimePort = 48442;

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
 * \li Objects/Structs (folder) with Button (ST_Button, members xPressed and
 *     strText), Buttons (ST_Button[2], elements Buttons[1] and Buttons[2]) and
 *     Matrix (Int32[2,3]); ST_Button is a Structure subtype without definition,
 *     like the structures of CODESYS.
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
        // Structure variables carry no value of their own, like on CODESYS.
        config.allowEmptyVariables = UA_RULEHANDLING_ACCEPT;
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
            while (m_running) {
                UA_Server_run_iterate(m_server, true);
                // An artificial delay after each iteration makes every request
                // wait, like a busy embedded PLC.
                if (const int delay = m_delayMs.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
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
     * \brief Delays every server iteration by \a delayMs milliseconds; 0 turns it off.
     */
    void setDelayMs(int delayMs) { m_delayMs = delayMs; }

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

        ok = ok && buildStructures();

        for (int i = 0; ok && i < kBulkCount; ++i) {
            const QByteArray name = QByteArrayLiteral("Bulk.V") + QByteArray::number(i);
            ok = addInt32(name.constData(), bulk, i, readWrite, hasComponent);
        }
        return ok;
    }

    /*!
     * \brief Adds a variable \a name of \a dataType and \a valueRank without a value.
     */
    bool addEmptyVariable(const char *name, const UA_NodeId &parent, const UA_NodeId &dataType,
                          UA_Int32 valueRank, UA_UInt32 length)
    {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(name));
        attr.dataType = dataType;
        attr.valueRank = valueRank;
        UA_UInt32 dimensions[1] = {length};
        if (valueRank == 1) {
            attr.arrayDimensions = dimensions;
            attr.arrayDimensionsSize = 1;
        }
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        const UA_StatusCode status = UA_Server_addVariableNode(
            m_server, UA_NODEID_STRING(m_ns, const_cast<char *>(name)), parent,
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(m_ns, const_cast<char *>(name)),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr, nullptr);
        if (status != UA_STATUSCODE_GOOD)
            qWarning("adding %s failed: %s", name, UA_StatusCode_name(status));
        return status == UA_STATUSCODE_GOOD;
    }

    /*!
     * \brief Adds the Structs folder: a structure, an array of structures and a 2-D array.
     */
    bool buildStructures()
    {
        const UA_NodeId structs = UA_NODEID_STRING(m_ns, const_cast<char *>("Structs"));
        const UA_NodeId buttonType = UA_NODEID_NUMERIC(m_ns, kButtonTypeId);
        const UA_NodeId hasComponent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        if (!addFolder("Structs", UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER)))
            return false;

        UA_DataTypeAttributes typeAttr = UA_DataTypeAttributes_default;
        typeAttr.displayName =
            UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("ST_Button"));
        if (UA_Server_addDataTypeNode(m_server, buttonType,
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_STRUCTURE),
                                      UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
                                      UA_QUALIFIEDNAME(m_ns, const_cast<char *>("ST_Button")),
                                      typeAttr, nullptr, nullptr)
            != UA_STATUSCODE_GOOD)
            return false;

        const UA_Byte readOnly = UA_ACCESSLEVELMASK_READ;
        bool ok = addEmptyVariable("Button", structs, buttonType, UA_VALUERANK_SCALAR, 0);
        const UA_NodeId button = UA_NODEID_STRING(m_ns, const_cast<char *>("Button"));
        ok = ok && addInt32("Button.Count", button, 3, readOnly, hasComponent);
        // A function block instance inside the structure, as CODESYS publishes
        // fbProduct inside stRecipe: an Object below a Variable.
        if (ok) {
            UA_ObjectAttributes fbAttr = UA_ObjectAttributes_default;
            fbAttr.displayName =
                UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("Fb"));
            const UA_StatusCode fbStatus = UA_Server_addObjectNode(
                m_server, UA_NODEID_STRING(m_ns, const_cast<char *>("Button.Fb")), button,
                hasComponent, UA_QUALIFIEDNAME(m_ns, const_cast<char *>("Fb")),
                UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), fbAttr, nullptr, nullptr);
            if (fbStatus != UA_STATUSCODE_GOOD)
                qWarning("adding Button.Fb failed: %s", UA_StatusCode_name(fbStatus));
            ok = fbStatus == UA_STATUSCODE_GOOD
                 && addInt32("Button.Fb.Speed",
                             UA_NODEID_STRING(m_ns, const_cast<char *>("Button.Fb")), 7,
                             readOnly, hasComponent);
        }

        UA_VariableAttributes textAttr = UA_VariableAttributes_default;
        textAttr.displayName =
            UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("strText"));
        textAttr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
        textAttr.valueRank = UA_VALUERANK_SCALAR;
        textAttr.accessLevel = readOnly;
        UA_String text = UA_STRING(const_cast<char *>("HMI Btn"));
        UA_Variant_setScalar(&textAttr.value, &text, &UA_TYPES[UA_TYPES_STRING]);
        ok = ok
             && UA_Server_addVariableNode(
                    m_server, UA_NODEID_STRING(m_ns, const_cast<char *>("Button.strText")),
                    button, hasComponent, UA_QUALIFIEDNAME(m_ns, const_cast<char *>("strText")),
                    UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), textAttr, nullptr,
                    nullptr)
                    == UA_STATUSCODE_GOOD;

        ok = ok && addEmptyVariable("Buttons", structs, buttonType, 1, 2);
        const UA_NodeId buttons = UA_NODEID_STRING(m_ns, const_cast<char *>("Buttons"));
        ok = ok && addEmptyVariable("Buttons[1]", buttons, buttonType, UA_VALUERANK_SCALAR, 0)
             && addEmptyVariable("Buttons[2]", buttons, buttonType, UA_VALUERANK_SCALAR, 0);

        // Int32[2,3] with ArrayDimensions on both the node and the value.
        UA_VariableAttributes matrixAttr = UA_VariableAttributes_default;
        matrixAttr.displayName =
            UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("Matrix"));
        matrixAttr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
        matrixAttr.valueRank = 2;
        UA_UInt32 dimensions[2] = {2, 3};
        matrixAttr.arrayDimensions = dimensions;
        matrixAttr.arrayDimensionsSize = 2;
        matrixAttr.accessLevel = readOnly;
        UA_Int32 values[6] = {1, 2, 3, 4, 5, 6};
        UA_Variant_setArray(&matrixAttr.value, values, 6, &UA_TYPES[UA_TYPES_INT32]);
        matrixAttr.value.arrayDimensions = dimensions;
        matrixAttr.value.arrayDimensionsSize = 2;
        ok = ok
             && UA_Server_addVariableNode(
                    m_server, UA_NODEID_STRING(m_ns, const_cast<char *>("Matrix")), structs,
                    hasComponent, UA_QUALIFIEDNAME(m_ns, const_cast<char *>("Matrix")),
                    UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), matrixAttr, nullptr,
                    nullptr)
                    == UA_STATUSCODE_GOOD;
        return ok;
    }

    UA_Server *m_server = nullptr;      //!< The embedded server.
    UA_UInt16 m_ns = 0;                 //!< Index of the test namespace.
    std::atomic<bool> m_running{false}; //!< Keeps the event loop running.
    std::atomic<int> m_delayMs{0};      //!< Artificial delay per iteration.
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

    /*!
        Scalar variables of a simple type are no longer browsed once the probe
        of their TypeDefinition found no children; every variable is still
        captured with its value, in about one read request per 16 variables.
    */
    void skipsLeafBrowsesAfterProbe();

    /*! maxArrayElements captures only the first elements of an array and reports the rest. */
    void limitsArrayElements();

    /*! browseScalarLeaves browses every variable, as before. */
    void browsesAllLeavesWhenRequested();

    /*!
        A probe that finds children of a scalar variable makes its
        TypeDefinition "always browse"; without a probe such children are lost.
    */
    void probeKeepsChildrenOfScalars();

    /*! A variable whose value cannot be read is still browsed for its children. */
    void browsesChildrenOfUnreadableVariable();

    /*! A vendor subtype of String resolves to String and keeps its id and name. */
    void resolvesVendorStringSubtype();

    /*! Structure variables get no Value read; their members and other variables do. */
    void skipsValueOfStructures();

    /*! On a slow server, structures still get no Value read and members do. */
    void skipsStructureValuesOnSlowServer();

    /*!
        A DataType whose lookups keep timing out leaves its variables without a
        value and with an error instead of reading a possible structure value.
    */
    void leavesValueUnreadWhenTypeLookupTimesOut();

    /*! The node limit ends the traversal as Truncated. */
    void reportsNodeLimit();

    /*! The depth limit leaves deep nodes unbrowsed and reports Truncated. */
    void reportsDepthLimit();

    /*! The overall timeout ends the traversal as TimedOut with pending work. */
    void reportsTimeout();

    /*! cancel() ends the traversal as Cancelled. */
    void reportsCancel();

    /*!
        Structures, arrays of structures and a 2-D array are cloned with their
        types and shapes, and the cloned project runs in the Server Studio runtime.
    */
    void clonesStructuresIntoRuntime();

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

void CloneTraversalTest::skipsLeafBrowsesAfterProbe()
{
    const Result result = run(m_server.nodeId("Bulk"));
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QCOMPARE(result.completion.nodeCount, kBulkCount);
    QVERIFY(result.completion.leafVariablesNotBrowsed > 0);
    QCOMPARE(result.completion.browseRequests + result.completion.leafVariablesNotBrowsed,
             kBulkCount + 1); // every variable plus the root is browsed or counted
    // One combined request per 16 variables, plus a few for the leftover values.
    QVERIFY2(result.completion.readRequests <= kBulkCount / 16 + 8,
             qPrintable(QString::number(result.completion.readRequests)));
    for (const CloneNode &node : result.nodes)
        QVERIFY2(node.value.isValid(), qPrintable(node.nodeId));
}

void CloneTraversalTest::limitsArrayElements()
{
    CloneOptions options;
    options.maxArrayElements = 1;
    const Result result = run(m_server.nodeId("Structs"), options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QVERIFY(!find(result.nodes, m_server.nodeId("Buttons[1]")).nodeId.isEmpty());
    QVERIFY(find(result.nodes, m_server.nodeId("Buttons[2]")).nodeId.isEmpty());
    QCOMPARE(result.completion.limitedArrays, 1);
    QCOMPARE(result.completion.arrayElementsSkipped, 1);

    const Result full = run(m_server.nodeId("Structs"));
    QVERIFY(!find(full.nodes, m_server.nodeId("Buttons[2]")).nodeId.isEmpty());
    QCOMPARE(full.completion.limitedArrays, 0);
}

void CloneTraversalTest::browsesAllLeavesWhenRequested()
{
    CloneOptions options;
    options.browseScalarLeaves = true;
    const Result result = run(m_server.nodeId("Bulk"), options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QCOMPARE(result.completion.leafVariablesNotBrowsed, 0);
    QCOMPARE(result.completion.browseRequests, kBulkCount + 1);
}

void CloneTraversalTest::probeKeepsChildrenOfScalars()
{
    // Locked is a scalar Int32 with the child Inner.
    const Result probed = run(m_server.nodeId("Area"));
    QVERIFY(probed.finished);
    QVERIFY(!find(probed.nodes, m_server.nodeId("Inner")).nodeId.isEmpty());
    QCOMPARE(probed.completion.leafVariablesNotBrowsed, 0);

    CloneOptions noProbe;
    noProbe.leafProbeCount = 0;
    const Result skipped = run(m_server.nodeId("Area"), noProbe);
    QVERIFY(skipped.finished);
    QVERIFY(find(skipped.nodes, m_server.nodeId("Inner")).nodeId.isEmpty());
    QVERIFY(skipped.completion.leafVariablesNotBrowsed > 0);
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

void CloneTraversalTest::skipsValueOfStructures()
{
    QCOMPARE(CloneOptions().overallTimeoutMs, 0); // no overall limit by default

    const Result result = run(m_server.nodeId("Structs"));
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QCOMPARE(result.completion.failedReads, 0);

    for (const char *name : {"Button", "Buttons"}) {
        const CloneNode structure = find(result.nodes, m_server.nodeId(name));
        QVERIFY2(structure.valueSkipped, name);
        QVERIFY(!structure.value.isValid());
        QVERIFY(structure.attributeErrors.isEmpty());
        QCOMPARE(structure.resolvedDataType.builtInBase, QStringLiteral("Structure"));
    }

    const CloneNode count = find(result.nodes, m_server.nodeId("Button.Count"));
    QVERIFY(!count.valueSkipped);
    QCOMPARE(count.value.toInt(), 3);

    const CloneNode matrix = find(result.nodes, m_server.nodeId("Matrix"));
    QVERIFY(!matrix.valueSkipped);
    QVERIFY(matrix.value.isValid());
}

void CloneTraversalTest::skipsStructureValuesOnSlowServer()
{
    m_server.setDelayMs(30);
    const Result result = run(m_server.nodeId("Structs"));
    m_server.setDelayMs(0);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QCOMPARE(result.completion.failedReads, 0);

    const CloneNode button = find(result.nodes, m_server.nodeId("Button"));
    QVERIFY(button.valueSkipped);
    QCOMPARE(button.resolvedDataType.builtInBase, QStringLiteral("Structure"));
    QCOMPARE(find(result.nodes, m_server.nodeId("Button.Count")).value.toInt(), 3);
}

void CloneTraversalTest::leavesValueUnreadWhenTypeLookupTimesOut()
{
    CloneOptions options;
    options.typeLookupTimeoutMs = 20; // far below the server's response time
    options.typeLookupAttempts = 2;
    m_server.setDelayMs(100);
    const Result result = run(m_server.nodeId("Structs"), options);
    m_server.setDelayMs(0);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QVERIFY(result.completion.failedReads > 0);

    for (const char *name : {"Button", "Buttons"}) {
        const CloneNode structure = find(result.nodes, m_server.nodeId(name));
        QVERIFY2(!structure.value.isValid(), name);
        QVERIFY2(!structure.valueSkipped, name);
        QVERIFY2(structure.attributeErrors.join(QLatin1Char(';'))
                     .contains(QLatin1String("DataType lookup timed out")),
                 name);
    }

    // Variables of namespace-0 types need no lookup and keep their values.
    QCOMPARE(find(result.nodes, m_server.nodeId("Button.Count")).value.toInt(), 3);
    QVERIFY(find(result.nodes, m_server.nodeId("Matrix")).value.isValid());
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

void CloneTraversalTest::clonesStructuresIntoRuntime()
{
    const Result result = run(m_server.nodeId("Structs"));
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);

    // Build the Server Studio project from the clone result.
    OpcUaManager manager;
    ServerStudio studio;
    studio.setOpcUaManager(&manager);
    emit manager.cloneSnapshotReady(0, result.nodes, m_client->namespaceArray(),
                                    result.completion);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("structs.uaserver"));
    QVERIFY(studio.saveProjectAs(path));
    ServerProject::Serializer::LoadResult loaded = ServerProject::Serializer::load(path);
    QVERIFY2(loaded.ok, qPrintable(loaded.errorString));
    ServerProject::ProjectData &project = loaded.data;
    const auto validation = ServerProject::Validator::validate(project);
    QVERIFY2(validation.errors.isEmpty(), qPrintable(validation.errors.join(QStringLiteral("; "))));

    const auto findNode = [&project](const QString &browseName) -> const ServerProject::Node * {
        for (const ServerProject::Node &node : project.nodes) {
            if (node.browseName == browseName)
                return &node;
        }
        return nullptr;
    };
    QCOMPARE(project.structTypes.size(), 1);
    QCOMPARE(project.structTypes.first().name, QStringLiteral("ST_Button"));

    const ServerProject::Node *button = findNode(QStringLiteral("Button"));
    QVERIFY(button && button->isStructure());
    QCOMPARE(findNode(QStringLiteral("strText"))->dataType, QStringLiteral("String"));
    const ServerProject::Node *buttons = findNode(QStringLiteral("Buttons"));
    QVERIFY(buttons && buttons->isStructure());
    QCOMPARE(buttons->valueRank, 1);
    QCOMPARE(buttons->arrayDimensions, QList<quint32>{2});
    QVERIFY(findNode(QStringLiteral("Buttons[1]"))->isStructure());
    const ServerProject::Node *matrix = findNode(QStringLiteral("Matrix"));
    QVERIFY(matrix);
    QCOMPARE(matrix->dataType, QStringLiteral("Int32"));
    QCOMPARE(matrix->valueRank, 2);
    QCOMPARE(matrix->arrayDimensions, (QList<quint32>{2, 3}));
    QCOMPARE(matrix->initialValue.toList().size(), 6);

    // Run the cloned project in the Server Studio runtime and read it back.
    const QString runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    if (runtimePath.isEmpty() || !QFileInfo::exists(runtimePath))
        QSKIP("OpcUaServerRuntime executable is not available.");
    project.server.endpoint.port = kRuntimePort;
    QVERIFY(ServerProject::Serializer::save(path, project).ok);

    QProcess runtime;
    runtime.setProcessChannelMode(QProcess::MergedChannels);
    runtime.start(runtimePath, {QStringLiteral("--project"), path, QStringLiteral("--port"),
                                QString::number(kRuntimePort)});
    QVERIFY2(runtime.waitForStarted(5000), "runtime failed to start");
    QString output;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !output.contains(QLatin1String("READY endpoint="))
           && runtime.state() == QProcess::Running) {
        if (runtime.waitForReadyRead(500))
            output += QString::fromUtf8(runtime.readAll());
    }
    QVERIFY2(output.contains(QLatin1String("READY endpoint=")), qPrintable(output));

    QOpcUaClient *client = m_provider.createClient(QStringLiteral("open62541"));
    QVERIFY(client);
    QSignalSpy endpointsSpy(client, &QOpcUaClient::endpointsRequestFinished);
    client->requestEndpoints(QUrl(QStringLiteral("opc.tcp://127.0.0.1:%1").arg(kRuntimePort)));
    QVERIFY(endpointsSpy.wait(10000));
    const auto endpoints = endpointsSpy.takeFirst().at(0).value<QList<QOpcUaEndpointDescription>>();
    QVERIFY(!endpoints.isEmpty());
    QSignalSpy connectedSpy(client, &QOpcUaClient::connected);
    client->connectToEndpoint(endpoints.first());
    QVERIFY(connectedSpy.wait(10000));
    client->updateNamespaceArray();
    QTRY_VERIFY(client->namespaceArray().size() > 1);
    const int ns = int(client->namespaceArray().indexOf(project.namespaces.first().uri));
    QVERIFY(ns > 0);

    // Reads attributes of the runtime node for project node id projectId.
    const auto read = [client, ns](const QString &projectId, QOpcUa::NodeAttributes attributes) {
        const QString runtimeId =
            QStringLiteral("ns=%1;%2").arg(ns).arg(projectId.section(QLatin1Char(';'), 1));
        QOpcUaNode *node = client->node(runtimeId);
        QSignalSpy readSpy(node, &QOpcUaNode::attributeRead);
        node->readAttributes(attributes);
        readSpy.wait(5000);
        return node;
    };

    QOpcUaNode *buttonsNode = read(buttons->nodeId, QOpcUa::NodeAttribute::DataType
                                                         | QOpcUa::NodeAttribute::ValueRank
                                                         | QOpcUa::NodeAttribute::ArrayDimensions);
    QCOMPARE(buttonsNode->attribute(QOpcUa::NodeAttribute::ValueRank).toInt(), 1);
    const QString typeId = buttonsNode->attribute(QOpcUa::NodeAttribute::DataType).toString();
    QOpcUaNode *typeNode = client->node(typeId);
    QSignalSpy typeSpy(typeNode, &QOpcUaNode::attributeRead);
    typeNode->readAttributes(QOpcUa::NodeAttribute::BrowseName);
    QVERIFY(typeSpy.wait(5000));
    QCOMPARE(typeNode->attribute(QOpcUa::NodeAttribute::BrowseName)
                 .value<QOpcUaQualifiedName>()
                 .name(),
             QStringLiteral("ST_Button"));

    // The function block object below the structure variable runs in the runtime too.
    const ServerProject::Node *speed = findNode(QStringLiteral("Button.Fb.Speed"));
    QVERIFY(speed);
    QOpcUaNode *speedNode = read(speed->nodeId, QOpcUa::NodeAttribute::Value);
    QCOMPARE(speedNode->attribute(QOpcUa::NodeAttribute::Value).toInt(), 7);
    delete speedNode;

    QOpcUaNode *matrixNode = read(matrix->nodeId, QOpcUa::NodeAttribute::Value
                                                       | QOpcUa::NodeAttribute::ValueRank);
    QCOMPARE(matrixNode->attribute(QOpcUa::NodeAttribute::ValueRank).toInt(), 2);
    const auto matrixValue =
        matrixNode->attribute(QOpcUa::NodeAttribute::Value).value<QOpcUaMultiDimensionalArray>();
    QCOMPARE(matrixValue.arrayDimensions(), (QList<quint32>{2, 3}));
    QCOMPARE(matrixValue.valueArray().value(5).toInt(), 6);

    delete buttonsNode;
    delete typeNode;
    delete matrixNode;
    QSignalSpy disconnectedSpy(client, &QOpcUaClient::disconnected);
    client->disconnectFromEndpoint();
    disconnectedSpy.wait(5000);
    delete client;
    runtime.kill();
    runtime.waitForFinished(5000);
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
