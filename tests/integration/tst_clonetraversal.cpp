// Integration test for the clone traversal against an in-process open62541
// server whose address space is built for the cases the Server Studio runtime
// cannot produce: more than 5000 nodes, a node reachable through two parents,
// an unreadable variable with a child, a vendor String subtype, and the node
// limit, depth limit, timeout and cancel outcomes.

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
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
#include <string>
#include <thread>

#include "core/clonebrowser.h"
#include "qmlapi/opcuamanager.h"
#include "qmlapi/serversnapshotcontroller.h"
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

        ok = ok && buildStructures() && buildTables();

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

    /*!
     * \brief Adds the variable \a id named \a browseName below \a parent.
     *
     * A \a dataType of Int32 gets \a value; any other type (a structure) has no
     * value. \a length > 0 makes it a one-dimensional array of that length.
     */
    bool addNamed(const std::string &id, const std::string &browseName, const UA_NodeId &parent,
                  const UA_NodeId &dataType, UA_Int32 value = 0, UA_UInt32 length = 0)
    {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                            const_cast<char *>(browseName.c_str()));
        attr.dataType = dataType;
        attr.valueRank = length > 0 ? UA_VALUERANK_ONE_DIMENSION : UA_VALUERANK_SCALAR;
        UA_UInt32 dimensions[1] = {length};
        if (length > 0) {
            attr.arrayDimensions = dimensions;
            attr.arrayDimensionsSize = 1;
        }
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        if (UA_NodeId_equal(&dataType, &UA_TYPES[UA_TYPES_INT32].typeId))
            UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_INT32]);
        const UA_StatusCode status = UA_Server_addVariableNode(
            m_server, UA_NODEID_STRING(m_ns, const_cast<char *>(id.c_str())), parent,
            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(m_ns, const_cast<char *>(browseName.c_str())),
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr, nullptr);
        if (status != UA_STATUSCODE_GOOD)
            qWarning("adding %s failed: %s", id.c_str(), UA_StatusCode_name(status));
        return status == UA_STATUSCODE_GOOD;
    }

    /*!
     * \brief Adds the Tables folder with arrays of structures named like CODESYS publishes them.
     *
     * Rows[1..50] (ST_Button) each have Count (= index) and Cells[1..3] with Flag
     * (= index); Rows[7] has no Count, so a copy of the template differs from the
     * server. Odd[1..3] has element NodeIds outside the "<array>[i]" pattern, and
     * Fbs[1..3] elements contain an Object.
     */
    bool buildTables()
    {
        const UA_NodeId objects = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
        const UA_NodeId buttonType = UA_NODEID_NUMERIC(m_ns, kButtonTypeId);
        const UA_NodeId int32Type = UA_TYPES[UA_TYPES_INT32].typeId;
        const auto id = [this](const std::string &text) {
            return UA_NODEID_STRING(m_ns, const_cast<char *>(text.c_str()));
        };
        if (!addFolder("Tables", objects))
            return false;
        const std::string tables = "Tables";

        bool ok = addNamed("Rows", "Rows", id(tables), buttonType, 0, 50);
        for (int i = 1; ok && i <= 50; ++i) {
            const std::string row = "Rows[" + std::to_string(i) + "]";
            ok = addNamed(row, row, id("Rows"), buttonType);
            if (ok && i != 7)
                ok = addNamed(row + ".Count", "Count", id(row), int32Type, i);
            ok = ok && addNamed(row + ".Cells", "Cells", id(row), buttonType, 0, 3);
            // CODESYS publishes array properties with a "|vprop|" NodeId prefix.
            ok = ok && addNamed("|vprop|" + row + ".Cells.Dimensions", "Dimensions",
                                id(row + ".Cells"), int32Type, 1);
            for (int j = 1; ok && j <= 3; ++j) {
                const std::string cell = row + ".Cells[" + std::to_string(j) + "]";
                ok = addNamed(cell, "Cells[" + std::to_string(j) + "]", id(row + ".Cells"),
                              buttonType)
                     && addNamed(cell + ".Flag", "Flag", id(cell), int32Type, j);
            }
        }

        ok = ok && addNamed("Odd", "Odd", id(tables), buttonType, 0, 3);
        for (int i = 1; ok && i <= 3; ++i) {
            const std::string element = "OddElement" + std::to_string(i);
            ok = addNamed(element, "Odd[" + std::to_string(i) + "]", id("Odd"), buttonType)
                 && addNamed(element + ".X", "X", id(element), int32Type, i);
        }

        ok = ok && addNamed("Fbs", "Fbs", id(tables), buttonType, 0, 3);
        for (int i = 1; ok && i <= 3; ++i) {
            const std::string element = "Fbs[" + std::to_string(i) + "]";
            ok = addNamed(element, element, id("Fbs"), buttonType);
            UA_ObjectAttributes fbAttr = UA_ObjectAttributes_default;
            fbAttr.displayName = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                                  const_cast<char *>("Fb"));
            ok = ok
                 && UA_Server_addObjectNode(m_server, id(element + ".Fb"), id(element),
                                            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                            UA_QUALIFIEDNAME(m_ns, const_cast<char *>("Fb")),
                                            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                            fbAttr, nullptr, nullptr)
                        == UA_STATUSCODE_GOOD
                 && addNamed(element + ".Fb.Speed", "Speed", id(element + ".Fb"), int32Type, i);
        }
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

    /*!
        Arrays of structures that follow the CODESYS NodeId pattern are built
        from their first element and verified by reading; an element that differs
        from the template is browsed instead.
    */
    void expandsArraysFromTemplate();

    /*! Arrays outside the NodeId pattern or with an Object in the template are browsed. */
    void browsesArraysThatCannotBeExpanded();

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

    /*! captureRoot captures an object root first and records reference types. */
    void capturesObjectRoot();

    /*! captureRoot on a variable reads the root's own metadata as well. */
    void capturesVariableRoot();

    /*! readValues off captures metadata only and reads no value at all. */
    void skipsAllValuesWhenDisabled();

    /*! The completion carries the endpoint and the server's BuildInfo. */
    void readsServerIdentity();

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

    /*!
        A snapshot of the Objects folder taken through ServerSnapshotController
        is written as a complete document with typed values and reported as
        incomplete because one variable is not readable.
    */
    void writesSnapshotThroughController();

    /*!
        The controller ignores results of other requests, writes nothing for a
        cancelled browse, and needs a selected node for the node scope.
    */
    void snapshotControllerGuards();

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

void CloneTraversalTest::expandsArraysFromTemplate()
{
    const Result result = run(m_server.nodeId("Tables"));
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    QCOMPARE(result.completion.failedReads, 0);
    QCOMPARE(result.completion.templateMisses, 1); // Rows[7] has no Count
    QVERIFY(result.completion.expandedArrays >= 2); // Rows and a nested Cells
    QVERIFY(result.completion.synthesizedNodes > 300);

    const auto nodeAt = [&](const std::string &name) {
        return find(result.nodes, m_server.nodeId(name.c_str()));
    };
    for (int i = 1; i <= 50; ++i) {
        const std::string row = "Rows[" + std::to_string(i) + "]";
        if (i == 7) {
            QVERIFY(nodeAt(row + ".Count").nodeId.isEmpty());
        } else {
            const CloneNode count = nodeAt(row + ".Count");
            QVERIFY2(!count.nodeId.isEmpty(), row.c_str());
            QCOMPARE(count.value.toInt(), i);
            QCOMPARE(count.browseName, QStringLiteral("Count"));
            QCOMPARE(count.resolvedDataType.builtInBase, QStringLiteral("Int32"));
        }
        for (int j = 1; j <= 3; ++j) {
            const CloneNode flag =
                nodeAt(row + ".Cells[" + std::to_string(j) + "].Flag");
            QVERIFY2(!flag.nodeId.isEmpty(), row.c_str());
            QCOMPARE(flag.value.toInt(), j);
            QCOMPARE(flag.parentNodeId,
                     m_server.nodeId((row + ".Cells[" + std::to_string(j) + "]").c_str()));
        }
    }
    QVERIFY(nodeAt("Rows[50].Cells[3].Flag").synthesized);
    // The "|vprop|" property of a nested array maps to every element.
    for (int i = 1; i <= 50; ++i) {
        const CloneNode dims = nodeAt("|vprop|Rows[" + std::to_string(i) + "].Cells.Dimensions");
        QVERIFY2(!dims.nodeId.isEmpty(), std::to_string(i).c_str());
        QCOMPARE(dims.value.toInt(), 1);
    }
    // Only the templates and the fallback element were browsed, not 50 rows.
    QVERIFY2(result.completion.browseRequests < 60,
             qPrintable(QString::number(result.completion.browseRequests)));

    CloneOptions noTemplate;
    noTemplate.expandArraysByTemplate = false;
    const Result browsed = run(m_server.nodeId("Tables"), noTemplate);
    QCOMPARE(browsed.completion.synthesizedNodes, 0);
    QCOMPARE(browsed.nodes.size(), result.nodes.size());
}

void CloneTraversalTest::browsesArraysThatCannotBeExpanded()
{
    const Result result = run(m_server.nodeId("Tables"));
    QVERIFY(result.finished);
    for (int i = 1; i <= 3; ++i) {
        const std::string odd = "OddElement" + std::to_string(i) + ".X";
        QCOMPARE(find(result.nodes, m_server.nodeId(odd.c_str())).value.toInt(), i);
        QVERIFY(!find(result.nodes, m_server.nodeId(odd.c_str())).synthesized);
        const std::string speed = "Fbs[" + std::to_string(i) + "].Fb.Speed";
        const CloneNode fbSpeed = find(result.nodes, m_server.nodeId(speed.c_str()));
        QCOMPARE(fbSpeed.value.toInt(), i);
        QVERIFY(!fbSpeed.synthesized);
    }
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

void CloneTraversalTest::capturesObjectRoot()
{
    CloneOptions options;
    options.captureRoot = true;
    const QString structs = m_server.nodeId("Structs");
    const Result result = run(structs, options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);

    const CloneNode root = result.nodes.first();
    QCOMPARE(root.nodeId, structs);
    QCOMPARE(root.browseName, QStringLiteral("Structs"));
    QCOMPARE(root.displayName, QStringLiteral("Structs"));
    QCOMPARE(root.nodeClass, int(QOpcUa::NodeClass::Object));
    QVERIFY(!root.isVariable);
    QVERIFY(root.parentNodeId.isEmpty());
    QVERIFY(root.referenceTypeId.isEmpty());

    const CloneNode button = find(result.nodes, m_server.nodeId("Button"));
    QCOMPARE(button.parentNodeId, structs);
    QCOMPARE(button.referenceTypeId, QStringLiteral("ns=0;i=47")); // HasComponent
}

void CloneTraversalTest::capturesVariableRoot()
{
    CloneOptions options;
    options.captureRoot = true;
    const QString buttonId = m_server.nodeId("Button");
    const Result result = run(buttonId, options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);

    const CloneNode root = result.nodes.first();
    QCOMPARE(root.nodeId, buttonId);
    QVERIFY(root.isVariable);
    QCOMPARE(root.resolvedDataType.builtInBase, QStringLiteral("Structure"));
    QVERIFY(root.valueSkipped);

    const CloneNode count = find(result.nodes, m_server.nodeId("Button.Count"));
    QCOMPARE(count.parentNodeId, buttonId);
    QCOMPARE(count.value.toInt(), 3);
}

void CloneTraversalTest::skipsAllValuesWhenDisabled()
{
    CloneOptions options;
    options.readValues = false;
    const Result result = run(m_server.nodeId("Area"), options);
    QVERIFY(result.finished);
    QCOMPARE(result.completion.state, State::Complete);
    // Locked is not readable; without Value reads it is no longer a failure.
    QCOMPARE(result.completion.failedReads, 0);

    const CloneNode label = find(result.nodes, m_server.nodeId("Label"));
    QVERIFY(label.isVariable);
    QVERIFY(!label.value.isValid());
    QVERIFY(!label.valueSkipped);
    QCOMPARE(label.resolvedDataType.builtInBase, QStringLiteral("String"));
}

void CloneTraversalTest::readsServerIdentity()
{
    const Result result = run(m_server.nodeId("Area"));
    QVERIFY(result.finished);
    const ServerIdentity &server = result.completion.server;
    QVERIFY2(server.endpointUrl.startsWith(QLatin1String("opc.tcp://")),
             qPrintable(server.endpointUrl));
    QCOMPARE(server.securityMode, QStringLiteral("None"));
    QVERIFY(!server.securityPolicy.isEmpty());
    QVERIFY(!server.applicationUri.isEmpty());
    QVERIFY(!server.productName.isEmpty());
    QVERIFY(!server.softwareVersion.isEmpty());
    QVERIFY(server.buildDate.isValid());
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
    QSignalSpy requestSpy(&manager, &OpcUaManager::cloneBrowseRequested);
    QVERIFY(studio.cloneFromClient(false));
    QCOMPARE(requestSpy.size(), 1);
    emit manager.cloneSnapshotReady(requestSpy.first().at(1).toULongLong(), result.nodes,
                                    m_client->namespaceArray(), result.completion);

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

void CloneTraversalTest::writesSnapshotThroughController()
{
    OpcUaManager manager;
    ServerSnapshotController controller;
    controller.setOpcUaManager(&manager);
    QSignalSpy requestSpy(&manager, &OpcUaManager::cloneBrowseRequested);
    QSignalSpy incompleteSpy(&controller, &ServerSnapshotController::snapshotIncomplete);
    QSignalSpy notificationSpy(&controller, &ServerSnapshotController::notification);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plant.uasnapshot.json"));
    QVERIFY(controller.start(ServerSnapshotController::ObjectsScope, true, 0,
                             QUrl::fromLocalFile(path).toString()));
    QVERIFY(controller.inProgress());
    QCOMPARE(requestSpy.size(), 1);
    const QString root = requestSpy.first().at(0).toString();
    const quint64 requestId = requestSpy.first().at(1).toULongLong();
    const CloneOptions options = requestSpy.first().at(2).value<CloneOptions>();
    QCOMPARE(root, QStringLiteral("ns=0;i=85"));
    QVERIFY(options.readValues);
    QVERIFY(!options.captureRoot);

    // The browse the worker runs for this request, here on the test client.
    const Result result = run(root, options);
    QVERIFY(result.finished);
    emit manager.cloneSnapshotReady(requestId, result.nodes, m_client->namespaceArray(),
                                    result.completion);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.inProgress(), 30000);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject document = QJsonDocument::fromJson(file.readAll()).object();
    QCOMPARE(document.value(QStringLiteral("format")).toString(), AddressSpaceSnapshot::formatName());
    QVERIFY(document.value(QStringLiteral("server")).toObject()
                .value(QStringLiteral("endpointUrl")).toString().startsWith(QLatin1String("opc.tcp://")));
    QCOMPARE(document.value(QStringLiteral("scope")).toObject().value(QStringLiteral("kind")),
             QJsonValue(QStringLiteral("Objects")));

    QHash<QString, QJsonObject> byPath;
    for (const QJsonValue &node : document.value(QStringLiteral("nodes")).toArray())
        byPath.insert(node.toObject().value(QStringLiteral("browsePath")).toString(), node.toObject());
    const auto valueAt = [&byPath](const char *browsePath) {
        return byPath.value(QLatin1String(browsePath))
            .value(QStringLiteral("variable")).toObject()
            .value(QStringLiteral("value")).toObject();
    };
    QCOMPARE(valueAt("/Objects/Area/Plant/Label").value(QStringLiteral("data")).toString(),
             QStringLiteral("abc"));
    QCOMPARE(valueAt("/Objects/Structs/Button").value(QStringLiteral("skipped")).toString(),
             QStringLiteral("structure"));
    QCOMPARE(valueAt("/Objects/Structs/Matrix").value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("dimensions")).toArray(),
             (QJsonArray{2, 3}));
    QVERIFY(valueAt("/Objects/Area/Plant/Locked").value(QStringLiteral("status")).toString()
                .startsWith(QLatin1String("Bad")));

    bool vendorType = false;
    for (const QJsonValue &type : document.value(QStringLiteral("dataTypes")).toArray()) {
        const QJsonObject object = type.toObject();
        if (object.value(QStringLiteral("name")).toString() == QLatin1String("VendorString"))
            vendorType = object.value(QStringLiteral("builtInBase")).toString() == QLatin1String("String");
    }
    QVERIFY(vendorType);

    // Locked cannot be read, so the snapshot is written but reported as incomplete.
    QCOMPARE(incompleteSpy.size(), 1);
    QVERIFY(notificationSpy.last().at(1).toString().contains(path));
    QVERIFY(!manager.addressSpaceBrowseInProgress());
}

void CloneTraversalTest::snapshotControllerGuards()
{
    OpcUaManager manager;
    ServerSnapshotController controller;
    controller.setOpcUaManager(&manager);
    QSignalSpy requestSpy(&manager, &OpcUaManager::cloneBrowseRequested);
    QSignalSpy notificationSpy(&controller, &ServerSnapshotController::notification);

    // The node scope needs a node selected in the client tree.
    QVERIFY(!controller.start(ServerSnapshotController::SelectedNodeScope, true, 0,
                              QStringLiteral("x.uasnapshot.json")));
    QVERIFY(requestSpy.isEmpty());
    QVERIFY(controller.suggestedFileName().endsWith(QLatin1String(".uasnapshot.json")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("server.uasnapshot.json"));
    QVERIFY(controller.start(ServerSnapshotController::WholeServerScope, false, 1000, path));
    QCOMPARE(requestSpy.size(), 1);
    QCOMPARE(requestSpy.first().at(0).toString(), QStringLiteral("ns=0;i=84"));
    const quint64 requestId = requestSpy.first().at(1).toULongLong();
    const CloneOptions options = requestSpy.first().at(2).value<CloneOptions>();
    QVERIFY(!options.readValues);
    QCOMPARE(options.maxNodes, 1000);

    // A second snapshot is refused while the first one runs.
    QVERIFY(!controller.start(ServerSnapshotController::ObjectsScope, true, 0, path));

    CloneCompletion cancelled;
    cancelled.state = CloneCompletion::State::Cancelled;
    emit manager.cloneSnapshotReady(requestId + 100, {}, {}, cancelled); // another request
    QVERIFY(controller.inProgress());
    emit manager.cloneSnapshotReady(requestId, {}, {}, cancelled);
    QVERIFY(!controller.inProgress());
    QVERIFY(!QFile::exists(path));
    QVERIFY(notificationSpy.last().at(1).toString().contains(QLatin1String("cancelled")));
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
