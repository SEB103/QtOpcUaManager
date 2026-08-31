#ifndef STRUCTUREDNODEREADER_H
#define STRUCTUREDNODEREADER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>

#include <QOpcUaNode>
#include <QOpcUaReferenceDescription>

#include "opcuavaluetree.h"

class QOpcUaClient;
class QTimer;

/**
 * Reads a structured OPC UA value by browsing the instance node tree.
 *
 * Some servers (for example CODESYS) do not expose the DataTypeDefinition
 * attribute, so QOpcUaGenericStructHandler cannot decode their ExtensionObject
 * values. Those servers still expose each structure member and array element as
 * a child node of the instance, exactly the layout the address-space tree shows.
 *
 * This reader recursively browses that instance subtree and reads each node's
 * Value, DataType, ValueRank, and DisplayName attributes, assembling an
 * OpcUaValueTreeNode tree that the StructuredValueFormatter renders as JSON/XML.
 * It runs in the OPC UA worker thread that owns the client and emits finished()
 * once the whole subtree is resolved or a safety timeout elapses.
 */
class StructuredNodeReader : public QObject
{
    Q_OBJECT

public:
    /** Creates a reader for \a rootNodeId using \a client for request \a requestId. */
    StructuredNodeReader(QOpcUaClient *client,
                         const QString &rootNodeId,
                         quint64 requestId,
                         QObject *parent = nullptr);
    ~StructuredNodeReader() override;

    /** Starts the recursive browse-and-read; finished() reports the result. */
    void start();

signals:
    /** Emitted once with the assembled \a root value tree for \a requestId. */
    void finished(quint64 requestId,
                  const QString &nodeId,
                  const OpcUaValueTreeNode &root,
                  bool success);

private:
    /** Mutable intermediate node with stable address during async assembly. */
    struct ReadNode
    {
        QString name;                       //!< Field name; empty for root/array elements.
        QString nodeId;                     //!< OPC UA node id being resolved.
        QString dataTypeId;                 //!< DataType node id, when known.
        int valueRank {-1};                 //!< OPC UA ValueRank (-1 scalar, >=1 array).
        int arrayIndex {-1};                //!< Element index for array children, else -1.
        int depth {0};                      //!< Depth from the root (recursion guard).
        QVariant value;                     //!< Raw scalar value for leaves.
        OpcUaValueTreeNode::Kind kind {OpcUaValueTreeNode::Kind::Scalar};
        QString typeName;                   //!< Resolved display type name.
        QVector<ReadNode *> children;       //!< Resolved child nodes.
        ReadNode *parent {nullptr};         //!< Owning parent, nullptr for the root.
        int pendingChildren {0};            //!< Child resolutions still in flight.
        QOpcUaNode *node {nullptr};         //!< Transient protocol node for this item.
        QOpcUa::NodeAttributes received {QOpcUa::NodeAttribute::None}; //!< Attributes seen so far.
        bool classified {false};            //!< Guards single classification of this node.
        bool browsed {false};               //!< Guards a single browse-result handling.
        bool finalized {false};             //!< Guards a single finalize of this node.
    };

    void resolveNode(ReadNode *item);
    void handleAttributes(ReadNode *item, QOpcUa::NodeAttributes attributes);
    void handleBrowse(ReadNode *item,
                      const QVector<QOpcUaReferenceDescription> &children);
    void finalizeNode(ReadNode *item);
    void finishSuccessfully(ReadNode *item);
    void emitResult(bool success);
    OpcUaValueTreeNode toValueTree(const ReadNode *item) const;
    static void deleteSubtree(ReadNode *item);

    QOpcUaClient *m_client {nullptr};       //!< Client owning the protocol nodes.
    QString m_rootNodeId;                   //!< Node id of the requested structure.
    quint64 m_requestId {0};                //!< GUI request id echoed in finished().
    ReadNode *m_root {nullptr};             //!< Root of the intermediate tree.
    int m_totalNodes {0};                   //!< Safety cap on total resolved nodes.
    bool m_finished {false};                //!< Guards against double emission.
    QTimer *m_timeout {nullptr};            //!< Safety timeout to force completion.
};

#endif // STRUCTUREDNODEREADER_H
