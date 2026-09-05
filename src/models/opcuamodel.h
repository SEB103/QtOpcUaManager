#ifndef OPCUAMODEL_H
#define OPCUAMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QPersistentModelIndex>
#include <QSet>
#include <QStringList>
#include <memory>
#include "core/opcuanodedata.h"
#include "treeitem.h"

/**
 * GUI-thread snapshot tree model for QML TreeView.
 *
 * This model no longer talks directly to QOpcUaClient. Instead it stores a
 * GUI-thread copy of the service browse tree and emits requests whenever QML
 * asks to expand a node whose children have not been fetched yet.
 */
class OpcUaModel : public QAbstractItemModel
{
    Q_OBJECT

    /** Whether newly discovered monitorable nodes should be treated as auto-monitored. */
    Q_PROPERTY(bool autoMonitor READ autoMonitor WRITE setAutoMonitor NOTIFY autoMonitorChanged)

    /** Display-name search query; empty disables search highlighting. */
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)

    /** Number of already-loaded nodes matching the current search query. */
    Q_PROPERTY(int searchMatchCount READ searchMatchCount NOTIFY searchMatchesChanged)

public:
    /** Custom roles exposed to QML TreeView delegates. */
    enum Role {
        /** OPC UA node id string. */
        NodeIdRole = Qt::UserRole + 1,
        /** OPC UA browse name. */
        BrowseNameRole,
        /** Localized display name. */
        DisplayNameRole,
        /** Numeric QOpcUa::NodeClass value. */
        NodeClassRole,
        /** Text form of the OPC UA node class. */
        NodeClassNameRole,
        /** Current value text, if available. */
        ValueRole,
        /** Current data type text, if available. */
        DataTypeRole,
        /** Description text, if available. */
        DescriptionRole,
        /** UI icon name for the node class. */
        IconNameRole,
        /** Whether the node can be monitored. */
        CanMonitorRole,
        /** Whether monitoring is enabled in the GUI snapshot. */
        MonitoringEnabledRole,
        /** Lazy-fetch state as a TreeItem::FetchState integer. */
        FetchStateRole,
        /** Whether the node matches the current search query. */
        SearchMatchRole
    };
    Q_ENUM(Role)

    /** Creates the model. */
    explicit OpcUaModel(QObject *parent = nullptr);

    /** Destroys the model. */
    ~OpcUaModel() override;

    /** Returns whether automatic monitoring is enabled. */
    bool autoMonitor() const { return m_autoMonitor; }

    /** Sets automatic monitoring. */
    void setAutoMonitor(bool enabled);

    /** Returns the current display-name search query. */
    QString searchQuery() const { return m_searchQuery; }

    /**
     * Sets the display-name search query and recomputes the match list.
     *
     * A query containing \c * or \c ? is matched as a wildcard pattern against
     * the whole display name; any other query is matched as a case-insensitive
     * substring. Only nodes already materialized in the snapshot tree are
     * searched, so a collapsed branch has to be expanded to become reachable.
     */
    void setSearchQuery(const QString &query);

    /** Returns the number of already-loaded nodes matching the search query. */
    int searchMatchCount() const { return int(m_searchMatches.size()); }

    /**
     * Returns the column-0 index of search match \a position, or an invalid
     * index when \a position is out of range. Matches are ordered depth-first,
     * so stepping through them walks the tree from top to bottom.
     */
    Q_INVOKABLE QModelIndex searchMatchAt(int position) const;

    /** Applies the current connection state. */
    void setConnectionActive(bool active);

    /**
     * Sets the node the model is rooted at and re-seeds the tree when connected.
     *
     * The main address-space model keeps the default server RootFolder, while a
     * focus model can be rooted at an arbitrary node so only that subtree is
     * browsed. The root node id is remembered and applied the next time the
     * connection becomes active.
     */
    void setRootNode(const QString &nodeId, const QString &displayName);

    /** Resets the model root back to the server RootFolder. */
    void clearRootNode();

    /**
     * Sets the node ids that are currently monitored so browsed nodes restore
     * their monitoring checkbox. The set is applied to items created afterwards
     * and re-applied to already materialized items so an open tree updates too.
     */
    void setMonitoredNodeIds(const QSet<QString> &nodeIds);

    /** Applies a child snapshot for \a parentNodeId. */
    void applyChildrenSnapshot(const QString &parentNodeId,
                               quint64 requestId,
                               const QList<OpcUaNodeData> &children,
                               bool success = true);

    /** Clears the current tree. */
    void clear();

    /** Returns the node id at \a index. */
    Q_INVOKABLE QString nodeIdAt(const QModelIndex &index) const;

    /**
     * Returns the column-0 index of the first already-materialized node whose id
     * equals \a nodeId, or an invalid index when it is absent or not yet loaded.
     * The search does not trigger lazy fetching.
     */
    Q_INVOKABLE QModelIndex indexForNodeId(const QString &nodeId) const;

    /**
     * Starts asynchronously materializing the lazy tree along \a displayPath (the
     * ordered display names from the top level down to the target) so a collapsed
     * branch is expanded on demand. \a targetNodeId is used to match the final
     * node. When the node is reached, revealPathReady() is emitted with its index;
     * an invalid index is emitted when the path cannot be resolved. Any previous
     * reveal request is cancelled.
     */
    Q_INVOKABLE void requestRevealPath(const QStringList &displayPath,
                                       const QString &targetNodeId);

    /** Returns whether monitoring is enabled at \a index. */
    Q_INVOKABLE bool monitoringEnabledAt(const QModelIndex &index) const;

    /** Sets monitoring at \a index. */
    Q_INVOKABLE void setMonitoringEnabledAt(const QModelIndex &index, bool enabled);

    /** Returns data for \a index and \a role. */
    QVariant data(const QModelIndex &index, int role) const override;
    /** Returns horizontal or vertical header data. */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    /** Returns the model index for \a row, \a column, and \a parent. */
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    /** Returns the parent index for \a index. */
    QModelIndex parent(const QModelIndex &index) const override;
    /** Returns the number of rows below \a parent. */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns the number of columns below \a parent. */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns role names exposed to QML. */
    QHash<int, QByteArray> roleNames() const override;
    /** Returns whether \a parent has or may have children. */
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns whether more children can be fetched for \a parent. */
    bool canFetchMore(const QModelIndex &parent) const override;
    /** Emits a lazy browse request for \a parent when fetching is possible. */
    void fetchMore(const QModelIndex &parent) override;

signals:
    /** Emitted when automatic monitoring changes. */
    void autoMonitorChanged();

    /** Emitted when the display-name search query changes. */
    void searchQueryChanged();

    /** Emitted when the set of search matches changes. */
    void searchMatchesChanged();

    /** Requests lazy children for the given node id. */
    void fetchChildrenRequested(const QString &parentNodeId, quint64 requestId);

    /**
     * Emitted when a requestRevealPath() run finishes. \a index is the target
     * node's index, or an invalid index when the path could not be resolved.
     */
    void revealPathReady(const QModelIndex &index);

private:
    /** Tracks an outstanding lazy browse request until service results arrive. */
    struct PendingFetch
    {
        /** Node id requested from the worker service. */
        QString parentNodeId;
        /** Persistent parent index used to apply the result to duplicate node ids. */
        QPersistentModelIndex parentIndex;
        /** Whether the request targets the invisible root item. */
        bool root {false};
    };

    /** Returns the item for \a index. */
    TreeItem *itemFromIndex(const QModelIndex &index) const;

    /** Returns the QModelIndex for \a item. */
    QModelIndex indexForItem(TreeItem *item, int column) const;

    /** Advances the in-progress reveal, fetching or descending one level. */
    void advanceReveal();
    /** Ends the in-progress reveal and emits revealPathReady() with \a index. */
    void finishReveal(const QModelIndex &index);
    /** Resumes a waiting reveal when the snapshot for \a parentNodeId arrives. */
    void maybeResumeReveal(const QString &parentNodeId, bool success);

    /**
     * Re-evaluates the search query against every loaded node, refreshes the
     * per-item match flag, and repaints the rows whose match state changed.
     */
    void rebuildSearchMatches();

private:
    /** Invisible root item for the connected-session snapshot tree. */
    std::unique_ptr<TreeItem> mRootItem;
    /** Node id the tree is rooted at; the server RootFolder by default. */
    QString m_rootNodeId {QStringLiteral("ns=0;i=84")};
    /** Display name used for the invisible root browse point. */
    QString m_rootDisplayName {QStringLiteral("RootFolder")};
    /** Outstanding browse requests keyed by service request id. */
    QHash<quint64, PendingFetch> m_pendingFetchRequests;
    /** Monotonic request id used to correlate browse results. */
    quint64 m_nextFetchRequestId {0};
    /** Local auto-monitoring flag exposed to QML. */
    bool m_autoMonitor {false};
    /** Whether the model should expose the current snapshot tree. */
    bool m_connectionActive {false};
    /** Node ids known to be monitored, used to restore the checkbox on browse. */
    QSet<QString> m_monitoredNodeIds;

    /** Current display-name search query; empty disables search highlighting. */
    QString m_searchQuery;
    /** Depth-first ordered indexes of the loaded nodes matching m_searchQuery. */
    QList<QPersistentModelIndex> m_searchMatches;

    /** Whether a requestRevealPath() run is currently in progress. */
    bool m_revealActive {false};
    /** Ordered display names from the top level down to the reveal target. */
    QStringList m_revealPath;
    /** Node id used to match the final segment of the reveal path. */
    QString m_revealTargetNodeId;
    /** Index into m_revealPath of the segment currently being resolved. */
    int m_revealDepth {0};
    /** Parent whose children are matched at the current reveal step; invalid = root. */
    QPersistentModelIndex m_revealParentIndex;
    /** Node id of the parent whose pending browse the reveal is waiting for. */
    QString m_revealWaitParentNodeId;
};

#endif // OPCUAMODEL_H
