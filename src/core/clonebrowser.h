#ifndef CLONEBROWSER_H
#define CLONEBROWSER_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <QOpcUaReadResult>
#include <QOpcUaReferenceDescription>

#include "datatyperesolver.h"

class QOpcUaClient;
class QOpcUaNode;
class QTimer;

/**
 * One browsed node captured for an address-space clone.
 *
 * A neutral value type carried from the OPC UA worker thread to the GUI so the
 * server-project builder can recreate the node. It keeps the attributes as
 * read from the server, together with the status of each read, so a missing
 * value is never mistaken for a real one.
 */
struct CloneNode
{
    /** Original server node id. */
    QString nodeId;

    /** Original parent node id; empty for a direct child of the browse root. */
    QString parentNodeId;

    /** Further hierarchical parents referencing this node; it is captured only once. */
    QStringList additionalParentIds;

    /** Browse name reported by the server. */
    QString browseName;

    /** Namespace index of the browse name. */
    quint16 browseNameNamespace = 0;

    /** Display name reported by the server. */
    QString displayName;

    /** Integer value of QOpcUa::NodeClass. */
    int nodeClass = 0;

    /** Reference type of the parent reference, e.g. "ns=0;i=47" (HasComponent); empty for a captured root. */
    QString referenceTypeId;

    /** TypeDefinition node id from the browse reference, e.g. "ns=0;i=63". */
    QString typeDefinitionId;

    /** Variable DataType node id as read from the server, e.g. "ns=0;i=6"; empty for non-variables. */
    QString dataTypeId;

    /** Name and OPC UA base type of \c dataTypeId; unresolved when not determinable. */
    ResolvedDataType resolvedDataType;

    /** OPC UA ValueRank: -1 scalar, >=1 array. */
    int valueRank = -1;

    /** ArrayDimensions attribute; empty when not provided. */
    QList<quint32> arrayDimensions;

    /** AccessLevel attribute bits, or -1 when not read. */
    int accessLevel = -1;

    /** UserAccessLevel attribute bits, or -1 when not read. */
    int userAccessLevel = -1;

    /** Current value for variables: a scalar QVariant or a QVariantList array; invalid when not read. */
    QVariant value;

    /** Status code of the Value read (QOpcUa::UaStatusCode); Good when read successfully. */
    quint32 valueStatus = 0;

    /** Source timestamp of the value, when provided. */
    QDateTime sourceTimestamp;

    /** Server timestamp of the value, when provided. */
    QDateTime serverTimestamp;

    /** Whether the Value was deliberately not read because the DataType is a structure. */
    bool valueSkipped = false;

    /** Whether a read response for this variable arrived. */
    bool attributesRead = false;

    /** Failed reads as "Attribute: Status" texts, e.g. "Value: BadNotReadable". */
    QStringList attributeErrors;

    /** Browse failure of this node ("Status" text); empty when browsed or not browsed. */
    QString browseError;

    /** Whether the node is a FolderType object. */
    bool isFolder = false;

    /** Whether the node is a variable. */
    bool isVariable = false;
};

Q_DECLARE_METATYPE(CloneNode)
Q_DECLARE_METATYPE(QList<CloneNode>)

/** Limits of one clone traversal. */
struct CloneOptions
{
    /** Maximum number of captured nodes; reaching it marks the result Truncated. */
    int maxNodes = 200000;
    /** Maximum depth below the root; deeper nodes are captured but not browsed. */
    int maxDepth = 64;
    /** Overall time limit in milliseconds, 0 for none; reaching it marks the result TimedOut. */
    int overallTimeoutMs = 0;
    /** Time without any response in milliseconds after which the result is TimedOut. */
    int idleTimeoutMs = 30000;
    /** Browse requests in flight at the same time. */
    int maxConcurrentBrowses = 4;
    /** Attribute read requests in flight at the same time. */
    int maxConcurrentReads = 2;
    /** Variables per metadata read request (5 attributes each, so 16 stay below 100 items). */
    int variablesPerRead = 16;
    /** Variables per Value read request. */
    int valuesPerRead = 64;
    /** Timeout of one DataType lookup round during the traversal, in milliseconds. */
    int typeLookupTimeoutMs = 120000;
    /** Lookup rounds per DataType before its variables are left without a value. */
    int typeLookupAttempts = 3;
    /** Whether variable values are read; false captures structure and metadata only. */
    bool readValues = true;
    /** Whether the root node itself is captured as the first node. */
    bool captureRoot = false;
};

Q_DECLARE_METATYPE(CloneOptions)

/** Identity of the server a traversal ran against; empty fields were not available. */
struct ServerIdentity
{
    /** Endpoint URL of the session. */
    QString endpointUrl;
    /** Message security mode of the session: "None", "Sign" or "SignAndEncrypt". */
    QString securityMode;
    /** Security policy URI of the session. */
    QString securityPolicy;
    /** First entry of the ServerArray: the server's application URI. */
    QString applicationUri;
    /** BuildInfo ProductUri. */
    QString productUri;
    /** BuildInfo ProductName. */
    QString productName;
    /** BuildInfo ManufacturerName. */
    QString manufacturerName;
    /** BuildInfo SoftwareVersion. */
    QString softwareVersion;
    /** BuildInfo BuildNumber. */
    QString buildNumber;
    /** BuildInfo BuildDate; invalid when not read. */
    QDateTime buildDate;
};

/** Breakdown of the work of a running clone traversal. */
struct CloneProgress
{
    /** Captured nodes. */
    int captured = 0;
    /** Nodes waiting to be browsed. */
    int browseQueued = 0;
    /** Browse requests in flight. */
    int browsing = 0;
    /** Variables waiting for the metadata read. */
    int metadataQueued = 0;
    /** Variables waiting for the Value read. */
    int valuesQueued = 0;
    /** Variables waiting for a DataType lookup. */
    int waitingForType = 0;
    /** Variables in read requests in flight. */
    int reading = 0;
    /** Read requests in flight. */
    int readRequests = 0;
    /** DataType ids in the running lookup round; 0 when none runs. */
    int typeLookups = 0;

    /** Returns the number of nodes whose browse or read is queued or in flight. */
    int pending() const
    {
        return browseQueued + browsing + metadataQueued + valuesQueued + waitingForType + reading;
    }
};

Q_DECLARE_METATYPE(CloneProgress)

/** Outcome of one clone traversal. */
struct CloneCompletion
{
    /** How the traversal ended. */
    enum class State {
        /** Every reachable node within the limits was browsed and read. */
        Complete = 0,
        /** A node or depth limit stopped part of the traversal. */
        Truncated,
        /** The overall or idle timeout ended the traversal with requests pending. */
        TimedOut,
        /** The traversal was cancelled. */
        Cancelled,
        /** Nothing could be captured (no client, root not browsable). */
        Failed
    };

    /** How the traversal ended. */
    State state = State::Failed;
    /** Number of captured nodes. */
    int nodeCount = 0;
    /** Whether the node limit was reached. */
    bool nodeLimitReached = false;
    /** Nodes at the depth limit that were not browsed. */
    int depthLimitedNodes = 0;
    /** Browse and read requests still unanswered when the traversal ended. */
    int pendingRequests = 0;
    /** Nodes whose browse failed. */
    int failedBrowses = 0;
    /** Variables with at least one failed attribute read. */
    int failedReads = 0;
    /** Limits the traversal ran with. */
    CloneOptions options;
    /** Duration of the traversal in milliseconds. */
    qint64 elapsedMs = 0;
    /** Server the traversal ran against; not read for a cancelled traversal. */
    ServerIdentity server;

    /** Returns whether the traversal finished within its limits. */
    bool isComplete() const { return state == State::Complete; }
};

Q_DECLARE_METATYPE(CloneCompletion)

/**
 * Recursively browses a server subtree and reads its variables for a clone.
 *
 * Runs in the OPC UA worker thread that owns \a client. Starting at a root node
 * (typically the Objects folder), it walks the hierarchical Object/Variable
 * children breadth-first with a bounded number of requests in flight. Every
 * node is browsed whether or not its attributes could be read. Variable
 * metadata is read first; the Value is read afterwards, only for variables
 * whose DataType is not a structure. Before emitting, the remaining DataType
 * ids are resolved through DataTypeResolver. finished()
 * reports the nodes with a CloneCompletion that states whether the result is
 * complete, truncated, timed out or cancelled.
 */
class CloneBrowser : public QObject
{
    Q_OBJECT

public:
    /** Creates a browser for \a rootNodeId using \a client for request \a requestId. */
    CloneBrowser(QOpcUaClient *client, const QString &rootNodeId, quint64 requestId,
                 const CloneOptions &options = CloneOptions(), QObject *parent = nullptr);
    ~CloneBrowser() override;

    /** Starts the traversal; finished() reports the result. */
    void start();

    /** Stops the traversal; finished() reports the nodes captured so far as Cancelled. */
    void cancel();

signals:
    /** Emitted about twice a second with the breakdown of the work in \a progress. */
    void progress(quint64 requestId, const CloneProgress &progress);

    /**
     * Emitted once at the end. \a nodes lists every captured node, parents before
     * children; \a namespaceUris is the server's NamespaceArray; \a completion
     * tells whether the result is complete.
     */
    void finished(quint64 requestId, const QList<CloneNode> &nodes,
                  const QStringList &namespaceUris, const CloneCompletion &completion);

private:
    /** One node waiting to be browsed. */
    struct BrowseItem
    {
        QString nodeId; //!< Node to browse.
        int depth = 0;  //!< Depth below the root (root = 0).
    };

    /** Kind of an attribute read request. */
    enum class ReadKind {
        Metadata, //!< DataType, ValueRank, ArrayDimensions and access levels.
        Values,   //!< Value only.
        RootInfo  //!< NodeClass, BrowseName and DisplayName of a captured root.
    };

    /** One attribute read request in flight. */
    struct ReadBatch
    {
        QStringList nodeIds;               //!< Nodes in this request.
        QSet<QString> keySet;              //!< Same ids, used to claim the shared result signal.
        ReadKind kind = ReadKind::Metadata; //!< What the request reads.
    };

    void pump();
    void startBrowse(const BrowseItem &item);
    void handleBrowse(const BrowseItem &item, QOpcUaNode *node,
                      const QList<QOpcUaReferenceDescription> &references,
                      QOpcUa::UaStatusCode status);
    void markBrowseError(const QString &nodeId, const QString &reason);
    void startRead(const QStringList &nodeIds, ReadKind kind);
    void applyRootInfo(const QList<QOpcUaReadResult> &results);
    void routeValueRead(const QString &nodeId);
    void queueValueRead(CloneNode &node, const ResolvedDataType &type);
    void startTypeLookup();
    void handleTypeLookupFinished();
    void logReadFailure(const QString &nodeId, const QString &what);
    void handleReadResults(const QList<QOpcUaReadResult> &results,
                           QOpcUa::UaStatusCode serviceResult);
    void failRead(const QStringList &nodeIds, const QString &reason);
    void applyReadResult(CloneNode &node, const QOpcUaReadResult &result);
    void touch();
    void finishIfIdle();
    void finish(CloneCompletion::State state);
    void startIdentityRead();
    void applyIdentity(const QList<QOpcUaReadResult> &results);
    void publishIfReady();
    void publishResult();
    int pendingCount() const;
    CloneProgress progressSnapshot() const;
    void failTypeLookup(const QString &typeId);

    QOpcUaClient *m_client = nullptr;         //!< Client owning the protocol nodes.
    QString m_rootNodeId;                     //!< Node id of the browse root.
    quint64 m_requestId = 0;                  //!< GUI request id echoed in the signals.
    CloneOptions m_options;                   //!< Traversal limits.

    QList<CloneNode> m_nodes;                 //!< Captured nodes, parents first.
    QHash<QString, qsizetype> m_index;        //!< Node id -> index in m_nodes.
    QQueue<BrowseItem> m_browseQueue;         //!< Nodes waiting to be browsed.
    QSet<QOpcUaNode *> m_browsing;            //!< Browse requests in flight.
    QStringList m_readQueue;                  //!< Variables waiting for the metadata read.
    QStringList m_valueQueue;                 //!< Variables waiting for the Value read.
    QHash<QString, ResolvedDataType> m_typeCache;    //!< Resolved DataTypes by normalized id.
    QHash<QString, QStringList> m_waitingForType;    //!< Variables waiting for a type lookup.
    QStringList m_typeQueue;                  //!< DataType ids waiting for a lookup.
    QStringList m_typeLookupIds;              //!< DataType ids of the running lookup.
    DataTypeResolver *m_typeLookup = nullptr; //!< Type lookup running during the traversal.
    int m_loggedReadFailures = 0;             //!< Read failures written to the log so far.
    QHash<QString, int> m_typeLookupRounds;   //!< Lookup rounds that timed out, per DataType id.
    QSet<QString> m_typeLookupGivenUp;        //!< DataType ids whose lookups kept timing out.
    int m_progressTicks = 0;                  //!< Progress reports since the last log line.
    QList<ReadBatch> m_reading;               //!< Read requests in flight.
    QMetaObject::Connection m_readConnection; //!< Connection to the shared read signal.

    CloneCompletion m_completion;             //!< Outcome assembled at the end.
    QStringList m_resultNamespaces;           //!< Namespace array captured at the end.
    DataTypeResolver *m_resolver = nullptr;   //!< Resolves the captured DataType ids.
    QMetaObject::Connection m_identityConnection; //!< Claims the server identity read.
    bool m_resolverPending = false;           //!< Final type resolution still running.
    bool m_identityPending = false;           //!< Server identity read still running.
    bool m_published = false;                 //!< finished() was emitted.
    QTimer *m_overallTimer = nullptr;         //!< Overall time limit.
    QTimer *m_idleTimer = nullptr;            //!< Restarted on every response.
    QTimer *m_progressTimer = nullptr;        //!< Periodic progress reports.
    QElapsedTimer m_elapsed;                  //!< Measures the traversal duration.
    bool m_started = false;                   //!< Guards against a second start().
    bool m_rootBrowseFailed = false;          //!< The root itself could not be browsed.
    bool m_finished = false;                  //!< Traversal ended; responses are ignored.
};

#endif // CLONEBROWSER_H
