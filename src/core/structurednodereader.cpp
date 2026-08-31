#include "structurednodereader.h"

#include <QLoggingCategory>
#include <QMetaType>
#include <QOpcUaClient>
#include <QOpcUaExtensionObject>
#include <QOpcUaLocalizedText>
#include <QOpcUaQualifiedName>
#include <QTimer>
#include <QVariantList>
#include <algorithm>

/*!
 * \class StructuredNodeReader
 * \brief Assembles a structured OPC UA value by browsing the instance subtree.
 */

Q_DECLARE_LOGGING_CATEGORY(lcOpcUaStruct)

namespace {

/*! \internal Safety caps guarding against runaway or cyclic address spaces. */
constexpr int kMaxDepth = 32;
constexpr int kMaxNodes = 5000;
/*! \internal Force completion if some read never returns (milliseconds). */
constexpr int kTimeoutMs = 20000;

/*! \internal Attributes read for every node during structured assembly. */
constexpr QOpcUa::NodeAttributes kNodeAttributes = QOpcUa::NodeAttribute::Value
    | QOpcUa::NodeAttribute::DataType | QOpcUa::NodeAttribute::ValueRank
    | QOpcUa::NodeAttribute::DisplayName;

/*!
 * \internal
 * \brief Returns a friendly OPC UA type name for scalar value \a value.
 *
 * Mirrors the scalar naming used elsewhere in the service so JSON/XML output is
 * consistent; unknown metatypes fall back to the raw QVariant type name.
 */
QString scalarTypeName(const QVariant &value)
{
    if (!value.isValid())
        return {};

    switch (value.typeId()) {
    case QMetaType::QString: return QStringLiteral("String");
    case QMetaType::Bool: return QStringLiteral("Boolean");
    case QMetaType::Double: return QStringLiteral("Double");
    case QMetaType::Float: return QStringLiteral("Float");
    case QMetaType::Int: return QStringLiteral("Int32");
    case QMetaType::UInt: return QStringLiteral("UInt32");
    case QMetaType::LongLong: return QStringLiteral("Int64");
    case QMetaType::ULongLong: return QStringLiteral("UInt64");
    case QMetaType::Short: return QStringLiteral("Int16");
    case QMetaType::UShort: return QStringLiteral("UInt16");
    case QMetaType::SChar: return QStringLiteral("SByte");
    case QMetaType::UChar: return QStringLiteral("Byte");
    case QMetaType::QDateTime: return QStringLiteral("DateTime");
    default: return QString::fromLatin1(value.typeName());
    }
}

/*!
 * \internal
 * \brief Derives a readable structure/array type name from DataType id \a dataTypeId.
 *
 * CODESYS exposes DataType ids such as
 * "ns=4;s=|type|...Application.AGX.ST_ValueList"; the last dotted segment is the
 * user type name. Returns an empty string when nothing usable can be extracted.
 */
QString structTypeName(const QString &dataTypeId)
{
    if (dataTypeId.isEmpty())
        return {};

    if (dataTypeId.contains(QLatin1String("|type|"), Qt::CaseInsensitive)) {
        const int hash = dataTypeId.lastIndexOf(QLatin1Char('#'));
        const int dot = dataTypeId.lastIndexOf(QLatin1Char('.'));
        if (hash != -1 && hash > dot)
            return dataTypeId.mid(hash + 1);
        if (dot != -1)
            return dataTypeId.mid(dot + 1);
    }
    return {};
}

/*!
 * \internal
 * \brief Formats scalar \a value as display text for the XML representation.
 *
 * Array-like values are joined with commas; every other value uses QVariant
 * string conversion.
 */
QString scalarText(const QVariant &value)
{
    if (!value.isValid())
        return {};

    if (value.typeId() != QMetaType::QString && value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (!list.isEmpty()) {
            QStringList parts;
            parts.reserve(list.size());
            for (const QVariant &element : list)
                parts.push_back(element.toString());
            return parts.join(QStringLiteral(", "));
        }
    }
    return value.toString();
}

/*!
 * \internal
 * \brief Extracts the numeric array index from a bracketed name like "x[3]".
 *
 * Returns -1 when \a name carries no "[index]" suffix.
 */
int parseArrayIndex(const QString &name)
{
    const int open = name.lastIndexOf(QLatin1Char('['));
    const int close = name.lastIndexOf(QLatin1Char(']'));
    if (open == -1 || close == -1 || close <= open + 1)
        return -1;
    bool ok = false;
    const int index = name.mid(open + 1, close - open - 1).toInt(&ok);
    return ok ? index : -1;
}

} // namespace

/*!
 * \brief Constructs the reader for \a rootNodeId using \a client.
 * \param client Connected OPC UA client owning the transient nodes.
 * \param rootNodeId Node id of the structured value to resolve.
 * \param requestId GUI request id echoed back in finished().
 * \param parent Owning QObject.
 */
StructuredNodeReader::StructuredNodeReader(QOpcUaClient *client,
                                           const QString &rootNodeId,
                                           quint64 requestId,
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
StructuredNodeReader::~StructuredNodeReader()
{
    deleteSubtree(m_root);
}

/*!
 * \brief Starts resolving the root node and arms the safety timeout.
 *
 * If the client is missing the reader completes immediately with a failure so
 * the caller's request is always answered.
 */
void StructuredNodeReader::start()
{
    if (!m_client || m_rootNodeId.isEmpty()) {
        emitResult(false);
        return;
    }

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        qCWarning(lcOpcUaStruct) << "StructuredNodeReader timeout for" << m_rootNodeId
                                 << "- emitting partial tree.";
        emitResult(m_root != nullptr);
    });
    m_timeout->start(kTimeoutMs);

    m_root = new ReadNode;
    m_root->nodeId = m_rootNodeId;
    resolveNode(m_root);
}

/*!
 * \brief Reads the attributes of \a item, then browses it when it is composite.
 */
void StructuredNodeReader::resolveNode(ReadNode *item)
{
    ++m_totalNodes;

    item->node = m_client->node(item->nodeId);
    if (!item->node) {
        finalizeNode(item);
        return;
    }
    item->node->setParent(this);

    // Do not use a single-shot connection: some backends deliver the requested
    // attributes across more than one attributeRead emission. handleAttributes
    // accumulates them and proceeds only once the required attributes arrived.
    connect(item->node, &QOpcUaNode::attributeRead, this,
            [this, item](const QOpcUa::NodeAttributes &attributes) {
                handleAttributes(item, attributes);
            });

    if (!item->node->readAttributes(kNodeAttributes))
        finalizeNode(item);
}

/*!
 * \brief Classifies \a item from its attributes and expands composite nodes.
 *
 * Structures and structure arrays are expanded by browsing their child nodes,
 * scalar arrays are expanded directly from the array value, and scalars keep
 * their value. This matches how the address-space tree presents CODESYS nodes.
 */
void StructuredNodeReader::handleAttributes(ReadNode *item, QOpcUa::NodeAttributes attributes)
{
    // Process the classification exactly once, after the attributes needed to
    // classify the node (Value, DataType, ValueRank) have all been received.
    if (item->classified || !item->node)
        return;

    item->received |= attributes;
    constexpr QOpcUa::NodeAttributes required = QOpcUa::NodeAttribute::Value
        | QOpcUa::NodeAttribute::DataType | QOpcUa::NodeAttribute::ValueRank;
    if ((item->received & required) != required)
        return;

    item->classified = true;

    QOpcUaNode *node = item->node;
    item->value = node->attribute(QOpcUa::NodeAttribute::Value);
    item->dataTypeId = node->attribute(QOpcUa::NodeAttribute::DataType).toString();
    const QVariant valueRank = node->attribute(QOpcUa::NodeAttribute::ValueRank);
    item->valueRank = valueRank.isValid() ? valueRank.toInt() : -1;

    const bool isExtensionObject = item->value.canConvert<QOpcUaExtensionObject>();
    const bool isList = item->value.isValid()
        && item->value.typeId() != QMetaType::QString
        && item->value.canConvert<QVariantList>();
    const QVariantList list = isList ? item->value.toList() : QVariantList();
    const bool elementsAreStructs = !list.isEmpty()
        && list.first().canConvert<QOpcUaExtensionObject>();

    // Array: an explicit array ValueRank or a list-like value.
    if (item->valueRank >= 1 || (isList && !list.isEmpty())) {
        item->kind = OpcUaValueTreeNode::Kind::Array;
        item->typeName = structTypeName(item->dataTypeId);

        if (isList && !elementsAreStructs) {
            // Array of scalars: build element children directly from the value.
            int index = 0;
            for (const QVariant &element : list) {
                ReadNode *child = new ReadNode;
                child->parent = item;
                child->depth = item->depth + 1;
                child->arrayIndex = index++;
                child->kind = OpcUaValueTreeNode::Kind::Scalar;
                child->value = element;
                child->typeName = scalarTypeName(element);
                item->children.push_back(child);
            }
            if (item->typeName.isEmpty() && !item->children.isEmpty())
                item->typeName = item->children.first()->typeName;
            finalizeNode(item);
            return;
        }

        // Array of structures (or an array whose value is not inline): browse the
        // element nodes and resolve each one recursively.
        connect(node, &QOpcUaNode::browseFinished, this,
                [this, item](const QVector<QOpcUaReferenceDescription> &children,
                             QOpcUa::UaStatusCode) { handleBrowse(item, children); },
                Qt::SingleShotConnection);
        if (!node->browseChildren(QOpcUa::ReferenceTypeId::HierarchicalReferences,
                                  QOpcUa::NodeClass::Variable))
            finalizeNode(item);
        return;
    }

    // Structure: an extension object whose members are exposed as child nodes.
    if (isExtensionObject) {
        item->kind = OpcUaValueTreeNode::Kind::Struct;
        item->typeName = structTypeName(item->dataTypeId);
        connect(node, &QOpcUaNode::browseFinished, this,
                [this, item](const QVector<QOpcUaReferenceDescription> &children,
                             QOpcUa::UaStatusCode) { handleBrowse(item, children); },
                Qt::SingleShotConnection);
        if (!node->browseChildren(QOpcUa::ReferenceTypeId::HierarchicalReferences,
                                  QOpcUa::NodeClass::Variable))
            finalizeNode(item);
        return;
    }

    // Scalar leaf.
    item->kind = OpcUaValueTreeNode::Kind::Scalar;
    item->typeName = scalarTypeName(item->value);
    if (item->typeName.isEmpty())
        item->typeName = structTypeName(item->dataTypeId);
    finalizeNode(item);
}

/*!
 * \brief Creates child nodes for \a item from browse result \a children.
 *
 * Structure children keep their display name; array element children are named
 * by index and sorted numerically. Depth and node-count caps stop unbounded or
 * cyclic expansion.
 */
void StructuredNodeReader::handleBrowse(ReadNode *item,
                                        const QVector<QOpcUaReferenceDescription> &children)
{
    // browseFinished can be emitted more than once; handle only the first result.
    if (item->browsed)
        return;
    item->browsed = true;

    if (item->depth >= kMaxDepth) {
        finalizeNode(item);
        return;
    }

    QVector<ReadNode *> kids;
    kids.reserve(children.size());
    for (const QOpcUaReferenceDescription &reference : children) {
        if (m_totalNodes >= kMaxNodes)
            break;

        const QString childNodeId = reference.targetNodeId().nodeId();
        if (childNodeId.isEmpty())
            continue;

        ReadNode *child = new ReadNode;
        child->parent = item;
        child->depth = item->depth + 1;
        child->nodeId = childNodeId;

        const QString displayName = reference.displayName().text();
        const QString label = displayName.isEmpty() ? reference.browseName().name()
                                                    : displayName;
        if (item->kind == OpcUaValueTreeNode::Kind::Array)
            child->arrayIndex = parseArrayIndex(label);
        else
            child->name = label;

        kids.push_back(child);
    }

    if (item->kind == OpcUaValueTreeNode::Kind::Array) {
        std::sort(kids.begin(), kids.end(), [](const ReadNode *a, const ReadNode *b) {
            return a->arrayIndex < b->arrayIndex;
        });
    }

    item->children = kids;
    item->pendingChildren = kids.size();

    if (kids.isEmpty()) {
        finalizeNode(item);
        return;
    }

    for (ReadNode *child : kids)
        resolveNode(child);
}

/*!
 * \brief Marks \a item resolved and completes the parent when its last child.
 *
 * Releases the transient protocol node and, for the root, emits the result.
 */
void StructuredNodeReader::finalizeNode(ReadNode *item)
{
    // Finalize each node exactly once so a parent's pending-child count can never
    // be decremented twice and complete the tree prematurely.
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
 * \brief Converts the assembled tree to OpcUaValueTreeNode and emits finished().
 *
 * Guarded so the result is emitted exactly once (normal completion or timeout).
 */
void StructuredNodeReader::emitResult(bool success)
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_timeout)
        m_timeout->stop();

    const OpcUaValueTreeNode root = m_root ? toValueTree(m_root) : OpcUaValueTreeNode();
    qCDebug(lcOpcUaStruct) << "StructuredNodeReader finished for" << m_rootNodeId
                           << "success:" << success
                           << "nodes:" << m_totalNodes
                           << "rootChildren:" << root.children.size();
    emit finished(m_requestId, m_rootNodeId, root, success && m_root != nullptr);
    deleteLater();
}

/*!
 * \brief Recursively converts intermediate \a item to an OpcUaValueTreeNode.
 */
OpcUaValueTreeNode StructuredNodeReader::toValueTree(const ReadNode *item) const
{
    OpcUaValueTreeNode node;
    node.name = item->name;
    node.typeId = item->dataTypeId;
    node.valueRank = item->valueRank;
    node.kind = item->kind;
    node.typeName = item->typeName;

    if (item->kind == OpcUaValueTreeNode::Kind::Scalar) {
        node.scalarValue = item->value;
        node.scalarText = scalarText(item->value);
    } else {
        node.children.reserve(item->children.size());
        for (const ReadNode *child : item->children)
            node.children.push_back(toValueTree(child));
    }
    return node;
}

/*!
 * \brief Recursively frees \a item and its children.
 */
void StructuredNodeReader::deleteSubtree(ReadNode *item)
{
    if (!item)
        return;
    for (ReadNode *child : item->children)
        deleteSubtree(child);
    if (item->node)
        item->node->deleteLater();
    delete item;
}
