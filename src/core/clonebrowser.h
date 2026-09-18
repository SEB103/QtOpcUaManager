#ifndef CLONEBROWSER_H
#define CLONEBROWSER_H

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <QOpcUaNode>
#include <QOpcUaReferenceDescription>

class QOpcUaClient;
class QTimer;

/**
 * One browsed node captured for an address-space clone.
 *
 * A neutral value type carried from the OPC UA worker thread to the GUI so the
 * server-project builder can recreate the node. Unlike the lazy tree snapshot,
 * this includes each variable's current value (scalars and arrays).
 */
struct CloneNode
{
    /** Original server node id. */
    QString nodeId;

    /** Original parent node id; empty for a direct child of the browse root. */
    QString parentNodeId;

    /** Browse name reported by the server. */
    QString browseName;

    /** Display name reported by the server. */
    QString displayName;

    /** Integer value of QOpcUa::NodeClass. */
    int nodeClass = 0;

    /** Variable DataType node id, e.g. "ns=0;i=6"; empty for non-variables. */
    QString dataTypeId;

    /** OPC UA ValueRank: -1 scalar, >=1 array. */
    int valueRank = -1;

    /** Current value for variables: a scalar QVariant or a QVariantList array. */
    QVariant value;

    /** Whether the node is a FolderType object. */
    bool isFolder = false;

    /** Whether the node is a variable. */
    bool isVariable = false;
};

Q_DECLARE_METATYPE(CloneNode)
Q_DECLARE_METATYPE(QList<CloneNode>)

/**
 * Recursively browses a server subtree and reads each variable's value.
 *
 * Runs in the OPC UA worker thread that owns \a client. Starting at a root node
 * (typically the Objects folder), it walks the hierarchical Object/Variable
 * children, reading DataType, ValueRank and Value for variables, and emits the
 * whole subtree as a flat pre-order list once complete or a safety timeout
 * elapses. This captures the full address space regardless of what the user
 * expanded in the client tree, which the lazy tree snapshot cannot.
 */
class CloneBrowser : public QObject
{
    Q_OBJECT

public:
    /** Creates a browser for \a rootNodeId using \a client for request \a requestId. */
    CloneBrowser(QOpcUaClient *client, const QString &rootNodeId, quint64 requestId,
                 QObject *parent = nullptr);
    ~CloneBrowser() override;

    /** Starts the recursive browse-and-read; finished() reports the result. */
    void start();

signals:
    /**
     * Emitted once when the subtree is assembled. \a nodes is the pre-order node
     * list, \a namespaceUris is the server's NamespaceArray (indexed by
     * namespace index), \a success is whether anything was captured, and
     * \a truncated is whether the node cap stopped the browse early.
     */
    void finished(quint64 requestId, const QList<CloneNode> &nodes,
                  const QStringList &namespaceUris, bool success, bool truncated);

private:
    /** Mutable intermediate node with a stable address during async assembly. */
    struct WorkNode
    {
        QString nodeId;             //!< Node id being resolved.
        QString parentNodeId;       //!< Parent id; empty for a direct child of the root.
        QString browseName;         //!< Browse name from the parent's browse reference.
        QString displayName;        //!< Display name from the parent's browse reference.
        int nodeClass = 0;          //!< QOpcUa::NodeClass value.
        QString dataTypeId;         //!< DataType node id (variables).
        int valueRank = -1;         //!< OPC UA ValueRank (variables).
        QVariant value;             //!< Current value (variables).
        bool isRoot = false;        //!< Whether this is the (non-emitted) browse root.
        bool isVariable = false;    //!< Whether the node is a variable.
        bool isFolder = false;      //!< Whether the node is a FolderType object.
        int depth = 0;              //!< Depth from the root (recursion guard).
        QVector<WorkNode *> children; //!< Resolved child nodes.
        WorkNode *parent = nullptr; //!< Owning parent, nullptr for the root.
        int pendingChildren = 0;    //!< Child resolutions still in flight.
        QOpcUaNode *node = nullptr; //!< Transient protocol node for this item.
        QOpcUa::NodeAttributes received {QOpcUa::NodeAttribute::None}; //!< Attributes seen so far.
        bool classified = false;    //!< Guards single classification of a variable.
        bool browsed = false;       //!< Guards a single browse-result handling.
        bool finalized = false;     //!< Guards a single finalize of this node.
    };

    void resolveNode(WorkNode *item);
    void handleAttributes(WorkNode *item, QOpcUa::NodeAttributes attributes);
    void browseNode(WorkNode *item);
    void handleBrowse(WorkNode *item, const QVector<QOpcUaReferenceDescription> &children);
    void finalizeNode(WorkNode *item);
    void emitResult(bool success);
    void collect(const WorkNode *item, QList<CloneNode> &out) const;
    static void deleteSubtree(WorkNode *item);

    QOpcUaClient *m_client = nullptr; //!< Client owning the protocol nodes.
    QString m_rootNodeId;             //!< Node id of the browse root.
    quint64 m_requestId = 0;          //!< GUI request id echoed in finished().
    WorkNode *m_root = nullptr;       //!< Root of the intermediate tree.
    int m_totalNodes = 0;             //!< Safety cap counter on resolved nodes.
    bool m_truncated = false;         //!< Whether the node cap stopped the browse.
    bool m_finished = false;          //!< Guards against double emission.
    QSet<QString> m_visited;          //!< Node ids already queued, to break cycles.
    QTimer *m_timeout = nullptr;      //!< Safety timeout to force completion.
};

#endif // CLONEBROWSER_H
