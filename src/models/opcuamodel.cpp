#include <QTimer>
#include "opcuamodel.h"
#include "treeitem.h"
using namespace Qt::Literals::StringLiterals;

/*!
 * \property OpcUaModel::autoMonitor
 * \brief Whether newly discovered monitorable nodes should be treated as auto-monitored.
 */

/*!
 * \brief Creates the model.
 * \param parent Optional QObject parent.
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
 * \param enabled Whether automatic monitoring should be enabled.
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
 * \param active Whether the service has an active connection.
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
        mRootItem = std::make_unique<TreeItem>(this, m_rootNodeId, m_rootDisplayName);
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
 * \brief Sets the node the model is rooted at and re-seeds the tree when connected.
 * \param nodeId The OPC UA node id to browse from; the RootFolder when empty.
 * \param displayName The display name used for the invisible root browse point.
 *
 * The new root is remembered for the next activation. When a session is already
 * active the tree is reset and an initial lazy browse of the new root is queued,
 * so switching the focus node repopulates the segment immediately.
 */
void OpcUaModel::setRootNode(const QString &nodeId, const QString &displayName)
{
    const QString effectiveId = nodeId.isEmpty() ? QStringLiteral("ns=0;i=84") : nodeId;
    m_rootNodeId = effectiveId;
    m_rootDisplayName = displayName.isEmpty() ? effectiveId : displayName;

    if (!m_connectionActive)
        return;

    m_pendingFetchRequests.clear();
    m_revealActive = false;

    beginResetModel();
    mRootItem = std::make_unique<TreeItem>(this, m_rootNodeId, m_rootDisplayName);
    endResetModel();

    QTimer::singleShot(0, this, [this]() {
        if (mRootItem && canFetchMore(QModelIndex()))
            fetchMore(QModelIndex());
    });
}

/*!
 * \brief Resets the model root back to the server RootFolder.
 */
void OpcUaModel::clearRootNode()
{
    setRootNode(QStringLiteral("ns=0;i=84"), QStringLiteral("RootFolder"));
}

/*!
 * \brief Sets the node ids that are currently monitored.
 *
 * Nodes created after this call restore their monitoring checkbox from the set.
 * Already materialized items are updated in place so an open tree reflects the
 * change immediately, emitting dataChanged() for each item whose state flips.
 */
void OpcUaModel::setMonitoredNodeIds(const QSet<QString> &nodeIds)
{
    m_monitoredNodeIds = nodeIds;

    if (!mRootItem)
        return;

    std::vector<TreeItem *> stack;
    for (int i = 0; i < mRootItem->childCount(); ++i)
        stack.push_back(mRootItem->child(i));

    while (!stack.empty()) {
        TreeItem *item = stack.back();
        stack.pop_back();
        if (!item)
            continue;

        const bool monitored = m_monitoredNodeIds.contains(item->nodeId());
        if (item->monitoringEnabled() != monitored) {
            item->setMonitoringEnabled(monitored);
            const QModelIndex idx = indexForItem(item, 0);
            if (idx.isValid()) {
                const QModelIndex left = index(idx.row(), 0, idx.parent());
                const QModelIndex right = index(idx.row(), columnCount() - 1, idx.parent());
                emit dataChanged(left, right, {MonitoringEnabledRole});
            }
        }

        for (int i = 0; i < item->childCount(); ++i)
            stack.push_back(item->child(i));
    }
}

/*!
 * \brief Applies a child snapshot for \a parentNodeId.
 * \param requestId The request identifier emitted by fetchChildrenRequested().
 * \param children The immutable child node snapshots to apply.
 * \param success Whether the browse operation succeeded.
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
        maybeResumeReveal(parentNodeId, false);
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
        maybeResumeReveal(parentNodeId, true);
        return;
    }

    std::vector<std::unique_ptr<TreeItem>> newChildren;
    newChildren.reserve(size_t(children.size()));
    for (const auto &child : children) {
        auto item = std::make_unique<TreeItem>(child, this, parentItem);
        // Restore the monitoring checkbox for nodes already in the Data Access View.
        item->setMonitoringEnabled(m_monitoredNodeIds.contains(child.nodeId));
        newChildren.push_back(std::move(item));
    }

    beginInsertRows(parentIndex, 0, int(children.size()) - 1);
    parentItem->replaceChildren(std::move(newChildren));
    parentItem->setFetchState(TreeItem::FetchState::Fetched);
    endInsertRows();

    maybeResumeReveal(parentNodeId, true);
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
 * \brief Returns the column-0 index of the materialized node with id \a nodeId.
 *
 * Performs a depth-first search over the currently loaded snapshot tree only, so
 * it never triggers a lazy browse. Returns an invalid index when no loaded node
 * carries the id, which lets callers fall back gracefully.
 */
QModelIndex OpcUaModel::indexForNodeId(const QString &nodeId) const
{
    if (nodeId.isEmpty() || !mRootItem)
        return {};

    std::vector<TreeItem *> stack;
    for (int i = mRootItem->childCount() - 1; i >= 0; --i)
        stack.push_back(mRootItem->child(i));

    while (!stack.empty()) {
        TreeItem *item = stack.back();
        stack.pop_back();
        if (!item)
            continue;
        if (item->nodeId() == nodeId)
            return indexForItem(item, 0);
        for (int i = item->childCount() - 1; i >= 0; --i)
            stack.push_back(item->child(i));
    }

    return {};
}

/*!
 * \brief Starts materializing the lazy tree along \a displayPath to \a targetNodeId.
 */
void OpcUaModel::requestRevealPath(const QStringList &displayPath, const QString &targetNodeId)
{
    // Cancel any previous reveal before starting a new one.
    m_revealActive = false;

    if (!mRootItem || displayPath.isEmpty()) {
        emit revealPathReady({});
        return;
    }

    m_revealActive = true;
    m_revealPath = displayPath;
    m_revealTargetNodeId = targetNodeId;
    m_revealDepth = 0;
    m_revealParentIndex = QPersistentModelIndex();
    m_revealWaitParentNodeId.clear();

    advanceReveal();
}

/*!
 * \internal
 * \brief Advances the in-progress reveal, fetching or descending one level.
 *
 * Starting from the saved parent and depth, it descends through every already
 * materialized level synchronously. When a level's children are missing it
 * either waits for an in-flight browse or issues one and returns, to be resumed
 * by maybeResumeReveal() once the snapshot arrives. The target node is matched by
 * id for the final segment and by display name for intermediate segments.
 */
void OpcUaModel::advanceReveal()
{
    if (!m_revealActive)
        return;
    if (!mRootItem) {
        finishReveal({});
        return;
    }

    QModelIndex parent = m_revealParentIndex;
    while (true) {
        TreeItem *parentItem = parent.isValid() ? itemFromIndex(parent) : mRootItem.get();
        if (!parentItem) {
            finishReveal({});
            return;
        }

        const int depth = m_revealDepth;
        if (depth < 0 || depth >= m_revealPath.size()) {
            finishReveal({});
            return;
        }

        const QString wantName = m_revealPath.at(depth);
        const bool lastSegment = (depth == m_revealPath.size() - 1);

        TreeItem *match = nullptr;
        for (int i = 0; i < parentItem->childCount(); ++i) {
            TreeItem *candidate = parentItem->child(i);
            if (!candidate)
                continue;
            if (lastSegment && !m_revealTargetNodeId.isEmpty()
                && candidate->nodeId() == m_revealTargetNodeId) {
                match = candidate;
                break;
            }
            if (candidate->displayName() == wantName) {
                match = candidate;
                if (!lastSegment)
                    break;
            }
        }

        if (match) {
            const QModelIndex matchIndex = indexForItem(match, 0);
            if (lastSegment) {
                finishReveal(matchIndex);
                return;
            }
            // Descend to the matched child and resolve the next segment there.
            m_revealParentIndex = QPersistentModelIndex(matchIndex);
            m_revealDepth = depth + 1;
            parent = matchIndex;
            continue;
        }

        // The wanted child is not present yet at this level.
        if (parentItem->fetchState() == TreeItem::FetchState::Fetching) {
            m_revealWaitParentNodeId = parentItem->nodeId();
            return;
        }
        if (parentItem->canFetchMore()) {
            m_revealWaitParentNodeId = parentItem->nodeId();
            fetchMore(parent);
            return;
        }

        // Fully fetched and still absent: the address space no longer matches the
        // stored path, so give up without disturbing the rest of the selection.
        finishReveal({});
        return;
    }
}

/*!
 * \internal
 * \brief Resumes a waiting reveal when the snapshot for \a parentNodeId arrives.
 */
void OpcUaModel::maybeResumeReveal(const QString &parentNodeId, bool success)
{
    if (!m_revealActive || parentNodeId != m_revealWaitParentNodeId)
        return;

    m_revealWaitParentNodeId.clear();
    if (success)
        advanceReveal();
    else
        finishReveal({});
}

/*!
 * \internal
 * \brief Ends the in-progress reveal and emits revealPathReady() with \a index.
 */
void OpcUaModel::finishReveal(const QModelIndex &index)
{
    m_revealActive = false;
    m_revealPath.clear();
    m_revealTargetNodeId.clear();
    m_revealDepth = 0;
    m_revealParentIndex = QPersistentModelIndex();
    m_revealWaitParentNodeId.clear();

    emit revealPathReady(index);
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
 * \param enabled Whether monitoring should be marked enabled in the GUI snapshot.
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
 * \param index The model index whose data is requested.
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
    case DataTypeRole: return item->dataTypeName();
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
 * \param section The header section index.
 * \param orientation The requested header orientation.
 * \param role The data role requested for the header.
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
 * \param parent The parent model index.
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
 * \param index The child index whose parent is requested.
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
 * \param parent The parent index whose child row count is requested.
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
 * \param parent The parent index; ignored because the model has a fixed column count.
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
 * The model does not browse directly. Instead it marks the GUI-thread node as
 * fetching and emits fetchChildrenRequested so that OpcUaManager can
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
