// Headless OPC UA server runtime (Phase 0 proof of concept).
//
// Serves a small, fixed address space so the existing QtOpcUaManager client can
// connect over a real opc.tcp endpoint and browse/read/write/subscribe. The GUI
// controls this process only through its command line, stdout and stdin -- there
// is no shared state across the process boundary.
//
// Contract with the controller:
//   * argument  --port <N>            the endpoint port (default 4840)
//   * stdout    "READY endpoint=..."  printed once the endpoint is listening
//   * stdin     a line "STOP" or EOF  requests a graceful shutdown
//   * exit code 0 on clean shutdown, non-zero on a startup/validation failure

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>

#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include "projectbuilder.h"
#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectserializer.h"
#include "serverproject/serverprojectvalidator.h"

namespace {

/*! Cleared to request the open62541 run loop to stop. */
std::atomic<bool> g_running {true};

/*! Handles SIGINT/SIGTERM by asking the run loop to stop gracefully. */
extern "C" void handleStopSignal(int)
{
    g_running.store(false);
}

/*! Builds an English localized text without the const-cast noise at call sites. */
UA_LocalizedText makeText(const char *text)
{
    return UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(text));
}

/*! Builds a qualified name in \a namespaceIndex from \a name. */
UA_QualifiedName makeName(UA_UInt16 namespaceIndex, const char *name)
{
    return UA_QUALIFIEDNAME(namespaceIndex, const_cast<char *>(name));
}

/*! Builds a string node id in \a namespaceIndex from \a identifier. */
UA_NodeId makeStringNodeId(UA_UInt16 namespaceIndex, const char *identifier)
{
    return UA_NODEID_STRING(namespaceIndex, const_cast<char *>(identifier));
}

/*!
 * Adds one writable scalar variable of \a type under \a parent.
 *
 * The initial \a value must already reference storage of the matching type; the
 * server copies it into the new node, so the caller keeps ownership.
 */
void addScalarVariable(UA_Server *server, const UA_NodeId &parent, const char *identifier,
                       const char *displayName, const UA_DataType &type, void *value)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = makeText(displayName);
    attr.dataType = type.typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    UA_Variant_setScalar(&attr.value, value, &type);

    UA_Server_addVariableNode(server, makeStringNodeId(1, identifier), parent,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                              makeName(1, displayName),
                              UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                              attr, nullptr, nullptr);
}

/*!
 * Populates the fixed proof-of-concept address space:
 *
 *   Objects/Test/{ BooleanValue, IntValue, DoubleValue, TextValue }
 *
 * All four are read/write scalars so the client can exercise browse, read,
 * write and subscription against known nodes.
 */
void buildFixedAddressSpace(UA_Server *server)
{
    UA_NodeId testFolder = UA_NODEID_NULL;
    UA_ObjectAttributes folderAttr = UA_ObjectAttributes_default;
    folderAttr.displayName = makeText("Test");
    UA_Server_addObjectNode(server, makeStringNodeId(1, "Test"),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                            makeName(1, "Test"),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE),
                            folderAttr, nullptr, &testFolder);

    UA_Boolean booleanValue = true;
    addScalarVariable(server, testFolder, "Test.BooleanValue", "BooleanValue",
                      UA_TYPES[UA_TYPES_BOOLEAN], &booleanValue);

    UA_Int32 intValue = 42;
    addScalarVariable(server, testFolder, "Test.IntValue", "IntValue",
                      UA_TYPES[UA_TYPES_INT32], &intValue);

    UA_Double doubleValue = 3.14159;
    addScalarVariable(server, testFolder, "Test.DoubleValue", "DoubleValue",
                      UA_TYPES[UA_TYPES_DOUBLE], &doubleValue);

    UA_String textValue = UA_STRING(const_cast<char *>("Hello OPC UA"));
    addScalarVariable(server, testFolder, "Test.TextValue", "TextValue",
                      UA_TYPES[UA_TYPES_STRING], &textValue);

    UA_NodeId_clear(&testFolder);
}

/*!
 * Reads stdin on a background thread and requests shutdown on "STOP" or EOF.
 *
 * This gives the controller a portable graceful-stop channel that does not
 * depend on Windows console signal delivery: the controller writes "STOP" (or
 * closes the write channel) and the blocking run loop notices g_running.
 */
void watchStdinForStop()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "STOP" || line == "stop") {
            g_running.store(false);
            return;
        }
    }
    // getline failed: stdin reached EOF (the controller closed the pipe).
    g_running.store(false);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OpcUaServerRuntime"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Headless OPC UA server runtime for QtOpcUaManager Server Studio."));
    parser.addHelpOption();
    QCommandLineOption portOption(
        QStringLiteral("port"),
        QStringLiteral("TCP port for the opc.tcp endpoint (default 4840)."),
        QStringLiteral("port"), QStringLiteral("4840"));
    parser.addOption(portOption);
    QCommandLineOption projectOption(
        QStringLiteral("project"),
        QStringLiteral("Path to a .uaserver project describing the address space."),
        QStringLiteral("path"));
    parser.addOption(projectOption);
    parser.process(app);

    bool portOk = false;
    const uint parsedPort = parser.value(portOption).toUInt(&portOk);
    if (!portOk || parsedPort == 0 || parsedPort > 65535) {
        std::fprintf(stderr, "ERROR invalid port '%s'\n",
                     parser.value(portOption).toLocal8Bit().constData());
        return 2;
    }
    auto port = static_cast<UA_UInt16>(parsedPort);

    // Load the project (if any) before touching open62541, so a malformed
    // project fails fast without opening a socket.
    bool hasProject = false;
    ServerProject::ProjectData project;
    if (parser.isSet(projectOption)) {
        const QString projectPath = parser.value(projectOption);
        const ServerProject::Serializer::LoadResult loaded =
            ServerProject::Serializer::load(projectPath);
        if (!loaded.ok) {
            std::fprintf(stderr, "ERROR failed to load project: %s\n",
                         loaded.errorString.toLocal8Bit().constData());
            return 6;
        }
        const ServerProject::Validator::Result validation =
            ServerProject::Validator::validate(loaded.data);
        if (!validation.ok) {
            for (const QString &issue : validation.errors)
                std::fprintf(stderr, "ERROR %s\n", issue.toLocal8Bit().constData());
            return 7;
        }
        project = loaded.data;
        hasProject = true;
        // The project's port applies unless the command line overrode it.
        if (!parser.isSet(portOption))
            port = project.server.endpoint.port;
    }

    std::signal(SIGINT, handleStopSignal);
    std::signal(SIGTERM, handleStopSignal);

    UA_Server *server = UA_Server_new();
    if (!server) {
        std::fprintf(stderr, "ERROR failed to allocate the OPC UA server\n");
        return 3;
    }

    UA_ServerConfig *config = UA_Server_getConfig(server);
    const UA_StatusCode configStatus = UA_ServerConfig_setMinimal(config, port, nullptr);
    if (configStatus != UA_STATUSCODE_GOOD) {
        std::fprintf(stderr, "ERROR failed to configure the endpoint on port %u: %s\n",
                     static_cast<unsigned>(port), UA_StatusCode_name(configStatus));
        UA_Server_delete(server);
        return 4;
    }

    if (hasProject) {
        QString buildError;
        if (!ProjectBuilder::build(server, project, buildError)) {
            std::fprintf(stderr, "ERROR %s\n", buildError.toLocal8Bit().constData());
            UA_Server_delete(server);
            return 8;
        }
    } else {
        buildFixedAddressSpace(server);
    }

    const UA_StatusCode startupStatus = UA_Server_run_startup(server);
    if (startupStatus != UA_STATUSCODE_GOOD) {
        std::fprintf(stderr, "ERROR failed to start the endpoint on port %u: %s\n",
                     static_cast<unsigned>(port), UA_StatusCode_name(startupStatus));
        UA_Server_delete(server);
        return 5;
    }

    // The endpoint is now listening: announce it so the controller can connect.
    std::printf("READY endpoint=opc.tcp://127.0.0.1:%u\n", static_cast<unsigned>(port));
    std::fflush(stdout);

    std::thread stdinWatcher(watchStdinForStop);
    stdinWatcher.detach();

    while (g_running.load()) {
        UA_Server_run_iterate(server, true);
    }

    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    std::printf("STOPPED\n");
    std::fflush(stdout);
    return 0;
}
