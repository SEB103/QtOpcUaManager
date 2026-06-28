#include <QMetaEnum>

#include "treeitem.h"
#include "opcuamodel.h"

/*!
 * \brief Creates the invisible root item.
 * The root item is a pure GUI-thread container. It represents the logical
 * RootFolder browse point and is never exposed directly to QML.
 */
TreeItem::TreeItem(OpcUaModel *model)
    : m_model(model)
    , m_nodeId(QStringLiteral("ns=0;i=84"))
    , m_browseName(QStringLiteral("RootFolder"))
    , m_displayName(QStringLiteral("RootFolder"))
    , m_hasChildren(true)
{
}

/*!
 * \brief Creates a visible child item from snapshot data.
 * The item copies immutable browse metadata received from OpcUaService.
 * It does not allocate QOpcUaNode or any other live backend object.
 */
TreeItem::TreeItem(const OpcUaNodeData &data, OpcUaModel *model, TreeItem *parent)
    : m_model(model)
    , m_parentItem(parent)
    , m_nodeId(data.nodeId)
    , m_browseName(data.browseName)
    , m_displayName(data.displayName)
    , m_nodeClass(data.nodeClass)
    , m_hasChildren(data.hasChildren)
{
}

/*!
 * \brief Destroys the item and all children.
 */
TreeItem::~TreeItem() = default;

/*!
 * \brief Returns the child at \a row.
 */
TreeItem *TreeItem::child(int row) const
{
    if (row < 0 || row >= int(m_children.size()))
        return nullptr;

    return m_children.at(size_t(row)).get();
}

/*!
 * \brief Returns the row index of \a child.
 */
int TreeItem::childIndex(const TreeItem *child) const
{
    if (!child)
        return -1;

    for (int i = 0, size = int(m_children.size()); i < size; ++i) {
        if (m_children.at(size_t(i)).get() == child)
            return i;
    }

    return -1;
}

/*!
 * \brief Returns the number of children.
 */
int TreeItem::childCount() const
{
    return int(m_children.size());
}

/*!
 * \brief Returns the row index in the parent.
 */
int TreeItem::row() const
{
    if (!m_parentItem)
        return 0;

    return m_parentItem->childIndex(this);
}

/*!
 * \brief Returns the column display data.
 * The GUI model keeps the same basic browser layout as before:
 * column 0 = name, column 1 = value, column 2 = type/class, column 3 = node id.
 * Value and type strings stay empty in this snapshot-only version until a
 * dedicated value adapter is added.
 */
QVariant TreeItem::columnData(int column) const
{
    switch (column) {
    case 0:
        return !m_displayName.isEmpty() ? m_displayName : m_browseName;
    case 1:
        return m_valueString;
    case 2:
        return nodeClassName();
    case 3:
        return m_nodeId;
    default:
        return {};
    }
}

/*!
 * \brief Returns the node class name.
 */
QString TreeItem::nodeClassName() const
{
    const QMetaEnum metaEnum = QMetaEnum::fromType<QOpcUa::NodeClass>();
    const char *name = metaEnum.valueToKey(m_nodeClass);
    return name ? QString::fromLatin1(name) : QStringLiteral("Undefined");
}

/*!
 * \brief Returns the icon name.
 */
QString TreeItem::iconName() const
{
    switch (QOpcUa::NodeClass(m_nodeClass)) {
    case QOpcUa::NodeClass::Object: return QStringLiteral("object");
    case QOpcUa::NodeClass::Variable: return QStringLiteral("variable");
    case QOpcUa::NodeClass::Method: return QStringLiteral("method");
    case QOpcUa::NodeClass::ObjectType: return QStringLiteral("objectType");
    case QOpcUa::NodeClass::VariableType: return QStringLiteral("variableType");
    case QOpcUa::NodeClass::DataType: return QStringLiteral("dataType");
    case QOpcUa::NodeClass::ReferenceType: return QStringLiteral("referenceType");
    case QOpcUa::NodeClass::View: return QStringLiteral("view");
    default: return QStringLiteral("node");
    }
}

/*!
 * \brief Returns whether the node supports monitoring.
 * In the snapshot-based model monitoring is currently only advertised for
 * variable nodes. Real subscription logic remains service-owned and can be
 * wired later through explicit service commands.
 */
bool TreeItem::supportsMonitoring() const
{
    return QOpcUa::NodeClass(m_nodeClass) == QOpcUa::NodeClass::Variable;
}

/*!
 * \brief Returns whether the node can request more children.
 */
bool TreeItem::canFetchMore() const
{
    return m_hasChildren
        && (m_fetchState == FetchState::NotFetched || m_fetchState == FetchState::Error);
}

/*!
 * \brief Marks the node as currently fetching.
 */
void TreeItem::markFetching()
{
    m_fetchState = FetchState::Fetching;
}

/*!
 * \brief Applies a new fetch state.
 */
void TreeItem::setFetchState(FetchState state)
{
    m_fetchState = state;
}

/*!
 * \brief Replaces all current children with \a children.
 * Ownership stays entirely inside the GUI-thread model. The service only
 * provides the raw snapshot list from which these child items were built.
 */
void TreeItem::replaceChildren(std::vector<std::unique_ptr<TreeItem>> children)
{
    m_children = std::move(children);

    if (!m_hasChildren)
        m_fetchState = FetchState::Fetched;
    else if (m_children.empty())
        m_fetchState = FetchState::Fetched;
}
