#include <QTimer>
#include "opcuamodel.h"
#include "treeitem.h"
using namespace Qt::Literals::StringLiterals;

/*!
 * \brief Creates the model.
 */
OpcUaModel::OpcUaModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

/*!
 * \brief Destroys the model.
 */
OpcUaModel::~OpcUaModel() = default;

/*!
 * \brief Sets automatic monitoring.
 */
void OpcUaModel::setAutoMonitor(bool enabled)
{
    if (m_autoMonitor == enabled)
        return;

    m_autoMonitor = enabled;
    emit autoMonitorChanged();
}

/*!
 * \brief Applies the current connection state.
 * \details
 * A live session creates an empty logical root item and triggers an initial
 * lazy browse request for the RootFolder. A disconnected session resets the
 * entire GUI-thread tree.
 */
void OpcUaModel::setConnectionActive(bool active)
{
    if (m_connectionActive == active)
        return;

    m_connectionActive = active;
    m_pendingFetchRequests.clear();

    beginResetModel();

    if (m_connectionActive)
        mRootItem = std::make_unique<TreeItem>(this);
    else
        mRootItem.reset();

    endResetModel();

    if (m_connectionActive && mRootItem) {
        QTimer::singleShot(0, this, [this]() {
            if (mRootItem && canFetchMore(QModelIndex()))
                fetchMore(QModelIndex());
        });
    }
}

/*!
 * \brief Applies a child snapshot for \a parentNodeId.
 * \details
 * The service emits immutable node lists. The model converts those lists into
 * GUI-thread TreeItem instances and replaces the current children of the
 * matching parent item.
 */
void OpcUaModel::applyChildrenSnapshot(const QString &parentNodeId,
                                       quint64 requestId,
                                       const QList<OpcUaNodeData> &children,
                                       bool success)
{
    if (!mRootItem)
        return;

    const auto pendingIt = m_pendingFetchRequests.find(requestId);
    if (pendingIt == m_pendingFetchRequests.end())
        return;

    const PendingFetch pending = pendingIt.value();
    m_pendingFetchRequests.erase(pendingIt);

    if (pending.parentNodeId != parentNodeId)
        return;

    QModelIndex parentIndex;
    TreeItem *parentItem = nullptr;
    if (pending.root) {
        parentItem = mRootItem.get();
    } else {
        if (!pending.parentIndex.isValid())
            return;
        parentIndex = QModelIndex(pending.parentIndex);
        parentItem = itemFromIndex(parentIndex);
    }

    if (!parentItem || parentItem->nodeId() != parentNodeId)
        return;

    if (!pending.root)
        parentIndex = indexForItem(parentItem, 0);

    if (!success) {
        parentItem->setFetchState(TreeItem::FetchState::Error);
        if (parentIndex.isValid()) {
            const QModelIndex left = index(parentIndex.row(), 0, parentIndex.parent());
            const QModelIndex right = index(parentIndex.row(), columnCount() - 1, parentIndex.parent());
            emit dataChanged(left, right, {FetchStateRole});
        }
        return;
    }

    const int oldCount = parentItem->childCount();
    if (oldCount > 0) {
        beginRemoveRows(parentIndex, 0, oldCount - 1);
        parentItem->replaceChildren({});
        endRemoveRows();
    }

    if (children.isEmpty()) {
        parentItem->setFetchState(TreeItem::FetchState::Fetched);
        return;
    }

    std::vector<std::unique_ptr<TreeItem>> newChildren;
    newChildren.reserve(size_t(children.size()));
    for (const auto &child : children)
        newChildren.push_back(std::make_unique<TreeItem>(child, this, parentItem));

    beginInsertRows(parentIndex, 0, int(children.size()) - 1);
    parentItem->replaceChildren(std::move(newChildren));
    parentItem->setFetchState(TreeItem::FetchState::Fetched);
    endInsertRows();
}

/*!
 * \brief Clears the current tree.
 */
void OpcUaModel::clear()
{
    setConnectionActive(false);
}

/*!
 * \brief Returns the node id at \a index.
 */
QString OpcUaModel::nodeIdAt(const QModelIndex &index) const
{
    const auto *item = itemFromIndex(index);
    return item ? item->nodeId() : QString();
}

/*!
 * \brief Returns whether monitoring is enabled at \a index.
 */
bool OpcUaModel::monitoringEnabledAt(const QModelIndex &index) const
{
    const auto *item = itemFromIndex(index);
    return item ? item->monitoringEnabled() : false;
}

/*!
 * \brief Sets monitoring at \a index.
 * \details
 * The snapshot model only mirrors the local monitoring flag for UI purposes.
 * Real subscription changes should be implemented as explicit service commands
 * if needed later.
 */
void OpcUaModel::setMonitoringEnabledAt(const QModelIndex &index, bool enabled)
{
    auto *item = itemFromIndex(index);
    if (!item)
        return;

    item->setMonitoringEnabled(enabled);

    const auto left = this->index(index.row(), 0, index.parent());
    const auto right = this->index(index.row(), columnCount() - 1, index.parent());
    emit dataChanged(left, right, {MonitoringEnabledRole});
}

/*!
 * \brief Returns model data for \a role.
 */
QVariant OpcUaModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !mRootItem)
        return {};

    auto *item = static_cast<TreeItem *>(index.internalPointer());
    if (!item)
        return {};

    switch (role) {
    case NodeIdRole: return item->nodeId();
    case BrowseNameRole: return item->browseName();
    case DisplayNameRole: return item->displayName();
    case NodeClassRole: return item->nodeClass();
    case NodeClassNameRole: return item->nodeClassName();
    case ValueRole: return item->valueString();
    case DataTypeRole: return item->dataTypeString();
    case DescriptionRole: return item->description();
    case IconNameRole: return item->iconName();
    case CanMonitorRole: return item->supportsMonitoring();
    case MonitoringEnabledRole: return item->monitoringEnabled();
    case FetchStateRole: return int(item->fetchState());
    default:
        break;
    }

    if (role == Qt::DisplayRole)
        return item->columnData(index.column());

    return {};
}

/*!
 * \brief Returns the header data.
 */
QVariant OpcUaModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Vertical)
        return section;

    switch (section) {
    case 0: return u"Name"_s;
    case 1: return u"Value"_s;
    case 2: return u"Type"_s;
    case 3: return u"NodeId"_s;
    default: return u"Column %1"_s.arg(section);
    }
}

/*!
 * \brief Returns the model index for \a row and \a column.
 */
QModelIndex OpcUaModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!mRootItem || row < 0 || column < 0)
        return {};

    if (!hasIndex(row, column, parent))
        return {};

    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : mRootItem.get();

    if (!parentItem)
        return {};

    TreeItem *child = parentItem->child(row);
    if (!child)
        return {};

    return createIndex(row, column, child);
}

/*!
 * \brief Returns the parent model index.
 */
QModelIndex OpcUaModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    auto *childItem = static_cast<TreeItem *>(index.internalPointer());
    if (!childItem)
        return {};

    TreeItem *parentItem = childItem->parentItem();
    if (!parentItem || parentItem == mRootItem.get())
        return {};

    return createIndex(parentItem->row(), 0, parentItem);
}

/*!
 * \brief Returns the row count.
 */
int OpcUaModel::rowCount(const QModelIndex &parent) const
{
    if (!mRootItem || !m_connectionActive)
        return 0;

    if (parent.column() > 0)
        return 0;

    TreeItem *parentItem = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : mRootItem.get();

    return parentItem ? parentItem->childCount() : 0;
}

/*!
 * \brief Returns the column count.
 */
int OpcUaModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 4;
}

/*!
 * \brief Returns QML role names.
 */
QHash<int, QByteArray> OpcUaModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Qt::DisplayRole] = "display";
    roles[Qt::DecorationRole] = "decoration";
    roles[NodeIdRole] = "nodeId";
    roles[BrowseNameRole] = "browseName";
    roles[DisplayNameRole] = "displayName";
    roles[NodeClassRole] = "nodeClass";
    roles[NodeClassNameRole] = "nodeClassName";
    roles[ValueRole] = "value";
    roles[DataTypeRole] = "dataType";
    roles[DescriptionRole] = "description";
    roles[IconNameRole] = "iconName";
    roles[CanMonitorRole] = "canMonitor";
    roles[MonitoringEnabledRole] = "monitoringEnabled";
    roles[FetchStateRole] = "fetchState";
    return roles;
}

/*!
 * \brief Returns whether \a parent has children.
 */
bool OpcUaModel::hasChildren(const QModelIndex &parent) const
{
    if (!mRootItem)
        return false;

    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : mRootItem.get();

    if (!item)
        return false;

    if (item->childCount() > 0)
        return true;

    if (item->fetchState() == TreeItem::FetchState::Fetched)
        return false;

    return item->mayHaveChildren();
}

/*!
 * \brief Returns whether more children can be fetched for \a parent.
 */
bool OpcUaModel::canFetchMore(const QModelIndex &parent) const
{
    if (!mRootItem || !m_connectionActive)
        return false;

    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : mRootItem.get();

    return item ? item->canFetchMore() : false;
}

/*!
 * \brief Requests more children for \a parent.
 * \details
 * The model does not browse directly. Instead it marks the GUI-thread node as
 * fetching and emits \see fetchChildrenRequested so that OpcUaManager can
 * forward the request to OpcUaService.
 */
void OpcUaModel::fetchMore(const QModelIndex &parent)
{
    if (!mRootItem || !m_connectionActive)
        return;

    TreeItem *item = parent.isValid()
        ? static_cast<TreeItem *>(parent.internalPointer())
        : mRootItem.get();

    if (!item || !item->canFetchMore())
        return;

    item->markFetching();

    const quint64 requestId = ++m_nextFetchRequestId;
    PendingFetch pending;
    pending.parentNodeId = item->nodeId();
    pending.parentIndex = QPersistentModelIndex(parent);
    pending.root = !parent.isValid();
    m_pendingFetchRequests.insert(requestId, pending);

    if (parent.isValid()) {
        const QModelIndex left = this->index(parent.row(), 0, parent.parent());
        const QModelIndex right = this->index(parent.row(), columnCount() - 1, parent.parent());
        emit dataChanged(left, right, {FetchStateRole});
    }

    emit fetchChildrenRequested(item->nodeId(), requestId);
}

/*!
 * \brief Returns the item for \a index.
 */
TreeItem *OpcUaModel::itemFromIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;

    return static_cast<TreeItem *>(index.internalPointer());
}

/*!
 * \brief Returns the QModelIndex for \a item.
 */
QModelIndex OpcUaModel::indexForItem(TreeItem *item, int column) const
{
    if (!item || item == mRootItem.get())
        return {};

    return createIndex(item->row(), column, item);
}
