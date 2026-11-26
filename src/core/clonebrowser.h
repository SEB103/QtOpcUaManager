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

    /** Whether the node was built from a template array element instead of being browsed. */
    bool synthesized = false;

    /** For a synthesized node: the array element whose subtree it belongs to. */
    QString expansionElementId;

    /** Set while traversing for a synthesized node the server does not have; never published. */
    bool missing = false;

    /**
     * Whether the server does not provide the DataType of this variable although
     * its members are browsable, e.g. a CODESYS structure that contains a
     * function block instance. Such a container is not a failed read.
     */
    bool dataTypeNotProvided = false;

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
    /** Whether scalar variables of simple types are browsed for children too. */
    bool browseScalarLeaves = false;
    /**
     * Scalar variables of simple types browsed per TypeDefinition before the
     * others of that TypeDefinition are left unbrowsed; 0 skips them at once.
     */
    int leafProbeCount = 200;
    /**
     * Elements captured per array (children named like "name[3]" below an
     * array variable); 0 captures all. Further elements are counted as skipped.
     */
    int maxArrayElements = 0;
    /**
     * Whether the elements of an array of structures are built from its first
     * element (by the NodeId pattern) and verified by reading them, instead of
     * browsing every element.
     */
    bool expandArraysByTemplate = true;
    /** Upper bound of variables per metadata read once the server's MaxNodesPerRead is known. */
    int maxVariablesPerRead = 64;
    /** Upper bound of variables per Value read once the server's MaxNodesPerRead is known. */
    int maxValuesPerRead = 256;
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
    /** Array elements waiting to be built from their template element. */
    int waitingForTemplate = 0;

    /** Returns the number of nodes whose browse or read is queued or in flight. */
    int pending() const
    {
        return browseQueued + browsing + metadataQueued + valuesQueued + waitingForType + reading
               + waitingForTemplate;
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
    /** Scalar variables of simple types that were not browsed for children. */
    int leafVariablesNotBrowsed = 0;
    /** Arrays whose elements were cut at CloneOptions::maxArrayElements. */
    int limitedArrays = 0;
    /** Array elements not captured because of CloneOptions::maxArrayElements. */
    int arrayElementsSkipped = 0;
    /** Arrays whose elements were built from a template element. */
    int expandedArrays = 0;
    /** Nodes built from template elements and confirmed by reading them. */
    int synthesizedNodes = 0;
    /** Elements whose built nodes did not exist on the server; they were browsed instead. */
    int templateMisses = 0;
    /** The server's MaxNodesPerRead (0: no limit), or -1 when it could not be read. */
    int serverMaxNodesPerRead = -1;
    /** Browse requests sent. */
    int browseRequests = 0;
    /** Read requests sent (metadata, values or both). */
    int readRequests = 0;
    /** Limits the traversal ran with. */
    CloneOptions options;
    /** Duration of the traversal in milliseconds. */
    qint64 elapsedMs = 0;

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

    /**
     * Marks the variables in \a nodes whose only failure is an unreadable
     * DataType but which have captured children as containers
     * (CloneNode::dataTypeNotProvided); returns how many were marked.
     */
    static int markContainersWithoutDataType(QList<CloneNode> &nodes);

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

    /** One attribute read request in flight. */
    struct ReadBatch
    {
        QStringList metadataIds; //!< Variables whose metadata attributes are read.
        QStringList valueIds;    //!< Variables whose Value is read.
        QSet<QString> keySet;    //!< Both lists, used to claim the shared result signal.
        qint64 startedMs = 0;    //!< Traversal time when the request was sent.
    };

    /** Probe of whether scalar variables of one TypeDefinition have children. */
    struct LeafProbe
    {
        int issued = 0;           //!< Probe browses sent.
        int answered = 0;         //!< Probe browses answered.
        bool hasChildren = false; //!< A probe found children: always browse this kind.
        QStringList waiting;      //!< Variables waiting for the probe's outcome.
    };

    /** Array whose other elements are built from its template element. */
    struct ExpansionJob
    {
        QString arrayId;          //!< The array variable.
        QString templateId;       //!< Element that is browsed and copied.
        QStringList elementIds;   //!< Elements built from the template.
        int depth = 0;            //!< Depth of the elements.
    };

    /** Request statistics of one kind over a time window. */
    struct RequestStats
    {
        int count = 0;       //!< Answered requests.
        qint64 totalMs = 0;  //!< Sum of their latencies.
        int items = 0;       //!< Nodes or read operations they carried.
        int empty = 0;       //!< Browses that returned no children.
    };

    void pump();
    void startBrowse(const BrowseItem &item);
    void handleBrowse(const BrowseItem &item, QOpcUaNode *node,
                      const QList<QOpcUaReferenceDescription> &references,
                      QOpcUa::UaStatusCode status);
    void markBrowseError(const QString &nodeId, const QString &reason);
    void startRead(const QStringList &metadataIds, const QStringList &valueIds);
    void routeVariable(const QString &nodeId);
    void settleVariable(CloneNode &node, const ResolvedDataType &type);
    bool isScalarLeaf(const CloneNode &node, const ResolvedDataType &type) const;
    void browseVariable(const QString &nodeId);
    void recordProbe(const QString &nodeId, bool hasChildren);
    void logProgress();
    void planExpansion(const BrowseItem &item, const QList<qsizetype> &elementIndices);
    void expandJobs();
    void expandJob(const ExpansionJob &job);
    void handleTemplateMiss(const CloneNode &node);
    void appendNode(const CloneNode &node);
    void startLimitsRead();
    void applyLimits(const QList<QOpcUaReadResult> &results);
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
    QHash<QString, int> m_pendingBrowseDepth; //!< Variables whose browse waits for their metadata.
    QHash<QString, LeafProbe> m_leafProbes;   //!< Leaf probes per TypeDefinition id.
    QHash<QString, QString> m_probeNodes;     //!< Probe browses in flight: node -> TypeDefinition.
    QHash<QOpcUaNode *, qint64> m_browseStarted; //!< Start time of each browse in flight.
    RequestStats m_browseWindow;              //!< Browse statistics since the last log line.
    RequestStats m_readWindow;                //!< Read statistics since the last log line.
    QHash<QString, QList<qsizetype>> m_children; //!< Parent node id -> indices of its children.
    QList<ExpansionJob> m_jobs;               //!< Arrays waiting for template expansion.
    QHash<QString, int> m_deferredElements;   //!< Elements waiting for expansion -> depth.
    QHash<QString, int> m_expandedElements;   //!< Expanded elements -> depth, for a fallback browse.
    QSet<QString> m_fallbackElements;         //!< Expanded elements browsed after a miss.
    QMetaObject::Connection m_limitsConnection; //!< Claims the MaxNodesPerRead read.
    bool m_limitsPending = false;             //!< MaxNodesPerRead read in flight.
    QList<ReadBatch> m_reading;               //!< Read requests in flight.
    QMetaObject::Connection m_readConnection; //!< Connection to the shared read signal.

    CloneCompletion m_completion;             //!< Outcome assembled at the end.
    QStringList m_resultNamespaces;           //!< Namespace array captured at the end.
    DataTypeResolver *m_resolver = nullptr;   //!< Resolves the captured DataType ids.
    QTimer *m_overallTimer = nullptr;         //!< Overall time limit.
    QTimer *m_idleTimer = nullptr;            //!< Restarted on every response.
    QTimer *m_progressTimer = nullptr;        //!< Periodic progress reports.
    QElapsedTimer m_elapsed;                  //!< Measures the traversal duration.
    bool m_started = false;                   //!< Guards against a second start().
    bool m_rootBrowseFailed = false;          //!< The root itself could not be browsed.
    bool m_finished = false;                  //!< Traversal ended; responses are ignored.
};

#endif // CLONEBROWSER_H
