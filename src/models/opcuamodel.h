#ifndef OPCUAMODEL_H
#define OPCUAMODEL_H

#include <QAbstractItemModel>
#include <memory>
#include "core/opcuanodedata.h"
#include "treeitem.h"

/*!
 * \class OpcUaModel
 *
 * GUI-thread snapshot tree model for QML TreeView.
 *
 * \details
 * This model no longer talks directly to QOpcUaClient. Instead it stores a
 * GUI-thread copy of the service browse tree and emits requests whenever QML
 * asks to expand a node whose children have not been fetched yet.
 */
class OpcUaModel : public QAbstractItemModel
{
    Q_OBJECT
    Q_PROPERTY(bool autoMonitor READ autoMonitor WRITE setAutoMonitor NOTIFY autoMonitorChanged)

public:
    enum Role {
        NodeIdRole = Qt::UserRole + 1,
        BrowseNameRole,
        DisplayNameRole,
        NodeClassRole,
        NodeClassNameRole,
        ValueRole,
        DataTypeRole,
        DescriptionRole,
        IconNameRole,
        CanMonitorRole,
        MonitoringEnabledRole,
        FetchStateRole
    };
    Q_ENUM(Role)

    /*! Creates the model. */
    explicit OpcUaModel(QObject *parent = nullptr);

    /*! Destroys the model. */
    ~OpcUaModel() override;

    /*! Returns whether automatic monitoring is enabled. */
    bool autoMonitor() const { return m_autoMonitor; }

    /*! Sets automatic monitoring. */
    void setAutoMonitor(bool enabled);

    /*! Applies the current connection state. */
    void setConnectionActive(bool active);

    /*! Applies a child snapshot for \a parentNodeId. */
    void applyChildrenSnapshot(const QString &parentNodeId, const QList<OpcUaNodeData> &children);

    /*! Clears the current tree. */
    void clear();

    /*! Returns the node id at \a index. */
    Q_INVOKABLE QString nodeIdAt(const QModelIndex &index) const;

    /*! Returns whether monitoring is enabled at \a index. */
    Q_INVOKABLE bool monitoringEnabledAt(const QModelIndex &index) const;

    /*! Sets monitoring at \a index. */
    Q_INVOKABLE void setMonitoringEnabledAt(const QModelIndex &index, bool enabled);

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

signals:
    /*! Emitted when automatic monitoring changes. */
    void autoMonitorChanged();

    /*! Requests lazy children for the given node id. */
    void fetchChildrenRequested(const QString &parentNodeId);

private:
    /*! Returns the item for \a index. */
    TreeItem *itemFromIndex(const QModelIndex &index) const;

    /*! Returns the QModelIndex for \a item. */
    QModelIndex indexForItem(TreeItem *item, int column) const;

    /*! Finds an item by node id. */
    TreeItem *findItemByNodeId(const QString &nodeId, TreeItem *start) const;

private:
    std::unique_ptr<TreeItem> mRootItem;
    bool m_autoMonitor {false};
    bool m_connectionActive {false};
};

#endif // OPCUAMODEL_H
