#ifndef OPCUAMODEL_H
#define OPCUAMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QPersistentModelIndex>
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
        FetchStateRole
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

    /** Applies the current connection state. */
    void setConnectionActive(bool active);

    /** Applies a child snapshot for \a parentNodeId. */
    void applyChildrenSnapshot(const QString &parentNodeId,
                               quint64 requestId,
                               const QList<OpcUaNodeData> &children,
                               bool success = true);

    /** Clears the current tree. */
    void clear();

    /** Returns the node id at \a index. */
    Q_INVOKABLE QString nodeIdAt(const QModelIndex &index) const;

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

    /** Requests lazy children for the given node id. */
    void fetchChildrenRequested(const QString &parentNodeId, quint64 requestId);

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

private:
    /** Invisible root item for the connected-session snapshot tree. */
    std::unique_ptr<TreeItem> mRootItem;
    /** Outstanding browse requests keyed by service request id. */
    QHash<quint64, PendingFetch> m_pendingFetchRequests;
    /** Monotonic request id used to correlate browse results. */
    quint64 m_nextFetchRequestId {0};
    /** Local auto-monitoring flag exposed to QML. */
    bool m_autoMonitor {false};
    /** Whether the model should expose the current snapshot tree. */
    bool m_connectionActive {false};
};

#endif // OPCUAMODEL_H
