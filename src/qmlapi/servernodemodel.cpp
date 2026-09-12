#include "servernodemodel.h"

using ServerProject::Node;
using ServerProject::NodeKind;

/*!
 * \brief Creates an empty model with just the invisible root.
 */
ServerNodeModel::ServerNodeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_root(new Item)
{
}

ServerNodeModel::~ServerNodeModel() = default;

/*!
 * \brief Returns the item for \a index, or the root for an invalid index.
 */
ServerNodeModel::Item *ServerNodeModel::itemFor(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root.data();
    return static_cast<Item *>(index.internalPointer());
}

/*!
 * \brief Rebuilds the tree from \a nodes.
 *
 * All items are created first, then linked to their parents by node id, so the
 * node list may be in any order. A node whose parent is empty or missing is
 * placed at the top level; child order follows the list order.
 */
void ServerNodeModel::setNodes(const QList<Node> &nodes)
{
    beginResetModel();

    m_root.reset(new Item);

    QList<Item *> items;
    items.reserve(nodes.size());
    QHash<QString, Item *> byId;
    for (const Node &node : nodes) {
        auto *item = new Item;
        item->node = node;
        items.append(item);
        if (!node.nodeId.isEmpty())
            byId.insert(node.nodeId, item);
    }

    for (Item *item : items) {
        Item *parent = m_root.data();
        const QString parentId = item->node.parentNodeId;
        if (!parentId.isEmpty()) {
            Item *found = byId.value(parentId, nullptr);
            if (found && found != item)
                parent = found;
        }
        item->parent = parent;
        parent->children.append(item);
    }

    endResetModel();
}

/*!
 * \brief Returns the index for the child at (\a row, \a column) of \a parent.
 */
QModelIndex ServerNodeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    Item *parentItem = itemFor(parent);
    if (row < 0 || row >= parentItem->children.size())
        return {};
    return createIndex(row, column, parentItem->children.at(row));
}

/*!
 * \brief Returns the parent index of \a child.
 */
QModelIndex ServerNodeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    Item *item = itemFor(child);
    Item *parentItem = item ? item->parent : nullptr;
    if (!parentItem || parentItem == m_root.data())
        return {};

    Item *grandParent = parentItem->parent;
    const int row = grandParent ? grandParent->children.indexOf(parentItem) : 0;
    return createIndex(row, 0, parentItem);
}

/*!
 * \brief Returns the number of children under \a parent.
 */
int ServerNodeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    return itemFor(parent)->children.size();
}

/*!
 * \brief Returns the single column count.
 */
int ServerNodeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

/*!
 * \brief Returns the \a role datum for \a index.
 */
QVariant ServerNodeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const Node &node = itemFor(index)->node;

    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return node.displayName.isEmpty() ? node.browseName : node.displayName;
    case NodeIdRole:
        return node.nodeId;
    case ParentNodeIdRole:
        return node.parentNodeId;
    case KindRole:
        return int(node.kind);
    case KindNameRole:
        return ServerProject::nodeKindToString(node.kind);
    case BrowseNameRole:
        return node.browseName;
    case DataTypeRole:
        return node.dataType;
    case ValueRankRole:
        return node.valueRank;
    case WritableRole:
        return node.writable;
    case IsVariableRole:
        return node.isVariable();
    case IconNameRole:
        switch (node.kind) {
        case NodeKind::Folder:
            return QStringLiteral("folder");
        case NodeKind::Object:
            return QStringLiteral("object");
        case NodeKind::Variable:
            return QStringLiteral("variable");
        }
        return QStringLiteral("node");
    default:
        return {};
    }
}

/*!
 * \brief Maps role ids to the names used by the QML delegate.
 */
QHash<int, QByteArray> ServerNodeModel::roleNames() const
{
    return {
        {NodeIdRole, "nodeId"},
        {ParentNodeIdRole, "parentNodeId"},
        {KindRole, "kind"},
        {KindNameRole, "kindName"},
        {BrowseNameRole, "browseName"},
        {DisplayNameRole, "displayName"},
        {DataTypeRole, "dataType"},
        {ValueRankRole, "valueRank"},
        {WritableRole, "writable"},
        {IsVariableRole, "isVariable"},
        {IconNameRole, "iconName"},
    };
}
