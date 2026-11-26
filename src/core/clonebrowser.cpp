#include "clonebrowser.h"

#include <QLoggingCategory>
#include <QOpcUaClient>
#include <QOpcUaQualifiedName>
#include <QTimer>

/*!
 * \internal
 * \brief Logging category for clone diagnostics.
 *
 * Debug output is disabled by default. Enable it with
 * QT_LOGGING_RULES="opcua.clone.debug=true" to list every captured DataType id
 * with its name, base type and resolution.
 */
Q_LOGGING_CATEGORY(lcOpcUaClone, "opcua.clone", QtInfoMsg)

/*!
 * \class CloneBrowser
 * \inmodule OpcUaManager
 * \brief Recursively browses a server subtree and reads variable values for a clone.
 */

namespace {

/*! \internal Safety caps guarding against runaway or cyclic address spaces. */
constexpr int kMaxDepth = 64;
constexpr int kMaxNodes = 5000;
/*! \internal Force completion if some request never returns (milliseconds). */
constexpr int kTimeoutMs = 30000;

/*! \internal Node id of the standard FolderType, used to tag folders. */
const QString kFolderTypeId = QStringLiteral("ns=0;i=61");

/*! \internal Attributes read for every variable node. */
constexpr QOpcUa::NodeAttributes kVariableAttributes =
    QOpcUa::NodeAttribute::Value | QOpcUa::NodeAttribute::DataType
    | QOpcUa::NodeAttribute::ValueRank;

} // namespace

/*!
 * \brief Constructs the browser for \a rootNodeId using \a client.
 */
CloneBrowser::CloneBrowser(QOpcUaClient *client, const QString &rootNodeId, quint64 requestId,
                           QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_rootNodeId(rootNodeId)
    , m_requestId(requestId)
{
}

/*!
 * \brief Releases the intermediate tree and any transient protocol nodes.
 */
CloneBrowser::~CloneBrowser()
{
    deleteSubtree(m_root);
}

/*!
 * \brief Starts resolving the root node and arms the safety timeout.
 */
void CloneBrowser::start()
{
    if (!m_client || m_rootNodeId.isEmpty()) {
        emitResult(false);
        return;
    }

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        // Emit whatever was assembled so the request is always answered.
        emitResult(m_root != nullptr);
    });
    m_timeout->start(kTimeoutMs);

    m_root = new WorkNode;
    m_root->nodeId = m_rootNodeId;
    m_root->isRoot = true;
    m_visited.insert(m_rootNodeId);
    resolveNode(m_root);
}

/*!
 * \brief Reads a variable's attributes, or browses a non-variable directly.
 */
void CloneBrowser::resolveNode(WorkNode *item)
{
    ++m_totalNodes;

    if (!item->isVariable) {
        browseNode(item);
        return;
    }

    item->node = m_client->node(item->nodeId);
    if (!item->node) {
        browseNode(item);
        return;
    }
    item->node->setParent(this);

    // Some backends deliver the requested attributes across more than one
    // emission, so accumulate and proceed only once all have arrived.
    connect(item->node, &QOpcUaNode::attributeRead, this,
            [this, item](const QOpcUa::NodeAttributes &attributes) {
                handleAttributes(item, attributes);
            });

    if (!item->node->readAttributes(kVariableAttributes))
        browseNode(item);
}

/*!
 * \brief Captures a variable's value/type once its attributes arrive, then browses.
 */
void CloneBrowser::handleAttributes(WorkNode *item, QOpcUa::NodeAttributes attributes)
{
    if (item->classified || !item->node)
        return;

    item->received |= attributes;
    if ((item->received & kVariableAttributes) != kVariableAttributes)
        return;

    item->classified = true;
    item->value = item->node->attribute(QOpcUa::NodeAttribute::Value);
    item->dataTypeId = item->node->attribute(QOpcUa::NodeAttribute::DataType).toString();
    const QVariant valueRank = item->node->attribute(QOpcUa::NodeAttribute::ValueRank);
    item->valueRank = valueRank.isValid() ? valueRank.toInt() : -1;

    browseNode(item);
}

/*!
 * \brief Browses \a item's hierarchical Object/Variable children.
 */
void CloneBrowser::browseNode(WorkNode *item)
{
    if (!item->node) {
        item->node = m_client->node(item->nodeId);
        if (item->node)
            item->node->setParent(this);
    }
    if (!item->node || item->depth >= kMaxDepth) {
        finalizeNode(item);
        return;
    }

    connect(item->node, &QOpcUaNode::browseFinished, this,
            [this, item](const QVector<QOpcUaReferenceDescription> &children, QOpcUa::UaStatusCode) {
                handleBrowse(item, children);
            },
            Qt::SingleShotConnection);

    if (!item->node->browseChildren(QOpcUa::ReferenceTypeId::HierarchicalReferences,
                                    QOpcUa::NodeClass::Object | QOpcUa::NodeClass::Variable)) {
        finalizeNode(item);
    }
}

/*!
 * \brief Creates child work nodes from a browse result and resolves each.
 */
void CloneBrowser::handleBrowse(WorkNode *item,
                                const QVector<QOpcUaReferenceDescription> &children)
{
    if (item->browsed)
        return;
    item->browsed = true;

    QVector<WorkNode *> kids;
    kids.reserve(children.size());
    for (const QOpcUaReferenceDescription &reference : children) {
        if (m_totalNodes >= kMaxNodes) {
            m_truncated = true;
            break;
        }

        const QString childNodeId = reference.targetNodeId().nodeId();
        if (childNodeId.isEmpty() || m_visited.contains(childNodeId))
            continue; // Skip cycles and nodes reachable by more than one reference.
        m_visited.insert(childNodeId);

        WorkNode *child = new WorkNode;
        child->parent = item;
        child->depth = item->depth + 1;
        child->nodeId = childNodeId;
        child->parentNodeId = item->isRoot ? QString() : item->nodeId;
        child->browseName = reference.browseName().name();
        child->displayName = reference.displayName().text();
        child->nodeClass = static_cast<int>(reference.nodeClass());
        const QString typeDefinitionId = reference.typeDefinition().nodeId();
        child->isVariable = reference.nodeClass() == QOpcUa::NodeClass::Variable;
        child->isFolder = reference.nodeClass() == QOpcUa::NodeClass::Object
                          && typeDefinitionId == kFolderTypeId;
        kids.push_back(child);
    }

    item->children = kids;
    item->pendingChildren = kids.size();

    if (kids.isEmpty()) {
        finalizeNode(item);
        return;
    }

    for (WorkNode *child : kids)
        resolveNode(child);
}

/*!
 * \brief Marks \a item resolved and completes the parent when it is the last child.
 */
void CloneBrowser::finalizeNode(WorkNode *item)
{
    if (item->finalized)
        return;
    item->finalized = true;

    if (item->node) {
        item->node->deleteLater();
        item->node = nullptr;
    }

    if (!item->parent) {
        emitResult(true);
        return;
    }

    if (--item->parent->pendingChildren == 0)
        finalizeNode(item->parent);
}

/*!
 * \brief Assembles the flat node list and resolves its DataType ids.
 *
 * Runs once, either when the whole tree finalized or when the safety timeout
 * forces completion. The distinct DataType ids of the captured variables are
 * then resolved to their base types; publishResult() emits finished() when
 * the resolver settles.
 */
void CloneBrowser::emitResult(bool success)
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_timeout)
        m_timeout->stop();

    m_resultSuccess = success;
    if (m_root)
        collect(m_root, m_resultNodes);
    m_resultNamespaces = m_client ? m_client->namespaceArray() : QStringList();

    QStringList typeIds;
    QSet<QString> seen;
    for (const CloneNode &node : std::as_const(m_resultNodes)) {
        if (!node.isVariable || node.dataTypeId.isEmpty() || seen.contains(node.dataTypeId))
            continue;
        seen.insert(node.dataTypeId);
        typeIds.append(node.dataTypeId);
    }

    if (!m_client || typeIds.isEmpty()) {
        publishResult();
        return;
    }

    m_resolver = new DataTypeResolver(m_client, this);
    connect(m_resolver, &DataTypeResolver::finished, this, &CloneBrowser::publishResult);
    m_resolver->resolve(typeIds);
}

/*!
 * \internal
 * \brief Attaches the resolved DataTypes and emits finished() exactly once.
 *
 * A variable whose DataType attribute was never read keeps an empty id and is
 * reported as unresolved, so consumers do not mistake it for a known type.
 */
void CloneBrowser::publishResult()
{
    const DataTypeHierarchy localOnly;
    QHash<QString, ResolvedDataType> cache;
    for (CloneNode &node : m_resultNodes) {
        if (!node.isVariable)
            continue;
        auto it = cache.constFind(node.dataTypeId);
        if (it == cache.constEnd()) {
            ResolvedDataType resolved = m_resolver ? m_resolver->result(node.dataTypeId)
                                                   : localOnly.resolve(node.dataTypeId);
            if (node.dataTypeId.isEmpty())
                resolved.reason = QStringLiteral("DataType attribute was not read");
            it = cache.insert(node.dataTypeId, resolved);
            qCDebug(lcOpcUaClone).noquote()
                << "DataType" << (node.dataTypeId.isEmpty() ? QStringLiteral("<none>")
                                                            : node.dataTypeId)
                << "name:" << resolved.typeName << "base:" << resolved.builtInBase
                << "resolution:" << int(resolved.resolution) << resolved.reason;
        }
        node.resolvedDataType = it.value();
    }

    emit finished(m_requestId, m_resultNodes, m_resultNamespaces,
                  m_resultSuccess && m_root != nullptr && !m_resultNodes.isEmpty(), m_truncated);
    deleteLater();
}

/*!
 * \brief Appends \a item's descendants (pre-order) to \a out, skipping the root.
 */
void CloneBrowser::collect(const WorkNode *item, QList<CloneNode> &out) const
{
    if (!item->isRoot) {
        CloneNode node;
        node.nodeId = item->nodeId;
        node.parentNodeId = item->parentNodeId;
        node.browseName = item->browseName;
        node.displayName = item->displayName;
        node.nodeClass = item->nodeClass;
        node.dataTypeId = item->dataTypeId;
        node.valueRank = item->valueRank;
        node.value = item->value;
        node.isFolder = item->isFolder;
        node.isVariable = item->isVariable;
        out.append(node);
    }
    for (const WorkNode *child : item->children)
        collect(child, out);
}

/*!
 * \brief Recursively frees \a item and its children.
 */
void CloneBrowser::deleteSubtree(WorkNode *item)
{
    if (!item)
        return;
    for (WorkNode *child : item->children)
        deleteSubtree(child);
    if (item->node)
        item->node->deleteLater();
    delete item;
}
