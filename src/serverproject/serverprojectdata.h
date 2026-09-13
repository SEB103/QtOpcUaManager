#ifndef SERVERPROJECTDATA_H
#define SERVERPROJECTDATA_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

/**
 * In-memory domain model for a Server Studio project (.uaserver).
 *
 * This layer is deliberately Qt-Core only: it has no GUI, Quick, Qt OPC UA or
 * open62541 dependency, so both the editor (in the GUI application) and the
 * headless runtime can share it. It carries more than the OPC UA address space
 * (server configuration, namespaces, and later security and simulation), which
 * is why it is a native project format rather than NodeSet2 XML.
 *
 * Namespace identity: nodes reference a namespace only through the \c ns index
 * embedded in their canonical node id, where \c ns=0 is the OPC UA base
 * namespace and \c ns=1 is the first custom namespace in
 * ServerProjectData::namespaces. The runtime remaps these project-relative
 * indices to the indices open62541 actually assigns, so a stored index is never
 * treated as a globally stable identifier.
 */
namespace ServerProject {

/** Current .uaserver JSON schema version written by this build. */
inline constexpr int kServerProjectFormatVersion = 1;

/** Kind of an address-space node supported in this phase. */
enum class NodeKind {
    Folder,   /**< Organizes-referenced FolderType object. */
    Object,   /**< HasComponent-referenced BaseObjectType object. */
    Variable  /**< HasComponent-referenced BaseDataVariableType variable. */
};

/** Value source driving a variable's runtime value. */
enum class SimulationKind {
    Manual,   /**< No automatic updates; the value is written by clients. */
    Constant, /**< Held at the minimum value. */
    Counter,  /**< Increments by step each interval, wrapping at the maximum. */
    Toggle,   /**< Alternates between minimum and maximum each interval. */
    Random,   /**< A new random value in [min, max] each interval. */
    Sine,     /**< A sine wave between min and max with the given period. */
    Ramp      /**< A linear sawtooth from min to max over the period. */
};

/** Returns the canonical string for \a kind (e.g. "Sine"). */
QString simulationKindToString(SimulationKind kind);

/** Parses \a text into a SimulationKind, defaulting to Manual. */
SimulationKind simulationKindFromString(const QString &text);

/** Returns the supported simulation kind names, Manual first. */
QStringList simulationKindNames();

/** Value-source definition for a variable node. */
struct SimulationDefinition
{
    /** The value source; Manual means no automatic updates. */
    SimulationKind kind = SimulationKind::Manual;
    /** Update interval in milliseconds. */
    double intervalMs = 1000.0;
    /** Lower bound of the generated value. */
    double min = 0.0;
    /** Upper bound of the generated value. */
    double max = 100.0;
    /** Increment per interval for Counter. */
    double step = 1.0;
    /** Period in milliseconds for Sine and Ramp. */
    double periodMs = 10000.0;
};

/** Returns the canonical string for \a kind ("Folder"/"Object"/"Variable"). */
QString nodeKindToString(NodeKind kind);

/** Parses \a text into a NodeKind, defaulting to Folder for unknown input. */
NodeKind nodeKindFromString(const QString &text);

/**
 * Returns the supported built-in scalar data type names.
 *
 * These map to OPC UA built-in types in the runtime and gate the value editor
 * and validation. The list is intentionally limited to the scalar/array types
 * useful for exercising an OPC UA client in this phase.
 */
QStringList builtinDataTypeNames();

/** Result of parsing a canonical node-id string. */
struct ParsedNodeId
{
    /** Whether the source string was a well-formed node id. */
    bool valid = false;
    /** Project-relative namespace index (0 = base namespace). */
    quint16 ns = 0;
    /** Whether the identifier is numeric ("i=") rather than a string ("s="). */
    bool numeric = false;
    /** The identifier text (decimal digits when numeric). */
    QString identifier;
};

/**
 * Parses a canonical node id of the form "ns=<n>;s=<id>" or "ns=<n>;i=<num>".
 *
 * Returns a result with \c valid set to false when \a nodeId does not match
 * that form. This is shared by the validator, the runtime address-space builder
 * and the design-time tree model.
 */
ParsedNodeId parseNodeId(const QString &nodeId);

/** One named value of an enumeration data type. */
struct EnumEntry
{
    /** Integer value on the wire. */
    int value = 0;
    /** Human-readable name shown to clients. */
    QString name;

    /** Returns whether both fields match \a other. */
    bool operator==(const EnumEntry &other) const
    {
        return value == other.value && name == other.name;
    }
};

/**
 * A custom enumeration data type.
 *
 * Enumerations are Int32 on the wire with an EnumValues property listing the
 * named values, so they need no custom binary encoding and can be created at
 * runtime (unlike structures/ExtensionObjects, which require compile-time
 * generated type layouts and are deferred).
 */
struct EnumType
{
    /** Display name / browse name of the enumeration type. */
    QString name;
    /** Canonical node id of the enumeration DataType node, e.g. "ns=1;s=Enum.Color". */
    QString nodeId;
    /** The named values of the enumeration. */
    QList<EnumEntry> entries;
};

/** A custom OPC UA namespace declared by the project. */
struct Namespace
{
    /** Namespace URI; the stable identity of the namespace. */
    QString uri;

    /** Returns whether two namespaces have the same URI. */
    bool operator==(const Namespace &other) const { return uri == other.uri; }
};

/**
 * One address-space node.
 *
 * Node ids use the OPC UA string form "ns=<index>;s=<id>" or "ns=<index>;i=<n>"
 * where the index is project-relative (see the namespace note above). A node
 * with an empty \c parentNodeId is placed under the standard Objects folder.
 */
struct Node
{
    /** Node kind (folder, object or variable). */
    NodeKind kind = NodeKind::Folder;

    /** Canonical node id, for example "ns=1;s=Test". */
    QString nodeId;

    /** Parent node id; empty means the standard Objects folder (ns=0;i=85). */
    QString parentNodeId;

    /** Browse name text (its namespace follows the node id's namespace). */
    QString browseName;

    /** Human-readable display name. */
    QString displayName;

    /** Optional description. */
    QString description;

    /** Built-in data type name for variables; empty for folders and objects. */
    QString dataType;

    /**
     * Node id of a custom EnumType this variable is an instance of. When set,
     * the variable is an enumeration (Int32 on the wire) and \c dataType is
     * ignored.
     */
    QString enumTypeId;

    /** Value rank: -1 scalar, 1 one-dimensional array; ignored for non-variables. */
    int valueRank = -1;

    /** Whether the variable is writable (CurrentWrite in addition to CurrentRead). */
    bool writable = false;

    /** Initial value: a scalar QVariant, or a QVariantList for arrays. */
    QVariant initialValue;

    /** Value source for the variable (Manual by default). */
    SimulationDefinition simulation;

    /** Returns whether this node is a variable. */
    bool isVariable() const { return kind == NodeKind::Variable; }
};

/** Endpoint configuration for the server's single opc.tcp endpoint. */
struct EndpointConfiguration
{
    /** TCP port for the opc.tcp endpoint. */
    quint16 port = 4840;
};

/** Server-wide identity and endpoint configuration. */
struct Configuration
{
    /** Application URI advertised by the server. */
    QString applicationUri = QStringLiteral("urn:opcuamanager:serverruntime");

    /** Application (server) display name. */
    QString applicationName = QStringLiteral("OpcUaManager Server");

    /** The server's endpoint configuration. */
    EndpointConfiguration endpoint;
};

/**
 * A username/password login accepted by the test server.
 *
 * These are throwaway credentials for a local development/test server, stored
 * in the (private, local) .uaserver file so authentication scenarios are
 * reproducible. They must never be real secrets.
 */
struct UserCredential
{
    /** Login user name. */
    QString username;
    /** Login password (test credential; see the struct note). */
    QString password;

    /** Returns whether both fields match \a other. */
    bool operator==(const UserCredential &other) const
    {
        return username == other.username && password == other.password;
    }
};

/**
 * Security configuration: which endpoints the server offers and how clients
 * authenticate.
 */
struct SecurityConfiguration
{
    /** Whether anonymous sessions are accepted. */
    bool allowAnonymous = true;

    /** Accepted username/password logins. */
    QList<UserCredential> users;

    /** Whether a SecurityPolicy#None endpoint is offered. */
    bool allowNone = true;

    /**
     * Whether encrypted endpoints are offered (Basic256Sha256 and the AES
     * policies, each with Sign and SignAndEncrypt). Requires a server
     * certificate, which the runtime generates into the server PKI if absent.
     */
    bool enableSecurity = false;

    /**
     * Whether the server accepts any client certificate (the convenience
     * default, preserving older behavior). When false, the runtime enforces a
     * real trust list from the server PKI: unknown client certificates are
     * rejected and written to the rejected store until the user trusts them.
     * Only relevant when \l enableSecurity is true.
     */
    bool acceptAllClientCerts = true;
};

/**
 * Full in-memory representation of one .uaserver project.
 *
 * A project file is the single source of truth for the server: its identity and
 * endpoint, its custom namespaces, and its address-space nodes.
 */
struct ProjectData
{
    /** Schema version of the file this data was read from or is written to. */
    int formatVersion = kServerProjectFormatVersion;

    /** Human-readable project name shown in the launcher and window title. */
    QString displayName;

    /** Server identity and endpoint configuration. */
    Configuration server;

    /** Custom namespaces; entry 0 corresponds to node-id namespace index 1. */
    QList<Namespace> namespaces;

    /** Custom enumeration data types. */
    QList<EnumType> enumTypes;

    /** Address-space nodes belonging to the project. */
    QList<Node> nodes;

    /** Security and authentication configuration. */
    SecurityConfiguration security;
};

} // namespace ServerProject

#endif // SERVERPROJECTDATA_H
