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

    /** Value rank: -1 scalar, 1 one-dimensional array; ignored for non-variables. */
    int valueRank = -1;

    /** Whether the variable is writable (CurrentWrite in addition to CurrentRead). */
    bool writable = false;

    /** Initial value: a scalar QVariant, or a QVariantList for arrays. */
    QVariant initialValue;

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

    /** Address-space nodes belonging to the project. */
    QList<Node> nodes;

    /** Security and authentication configuration. */
    SecurityConfiguration security;
};

} // namespace ServerProject

#endif // SERVERPROJECTDATA_H
