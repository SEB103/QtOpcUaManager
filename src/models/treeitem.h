#ifndef TREEITEM_H
#define TREEITEM_H

#include <QString>
#include <QVariant>
#include <memory>
#include <vector>
#include <qopcuatype.h>

#include "core/opcuanodedata.h"

class OpcUaModel;

/**
 * GUI-thread tree node used by OpcUaModel.
 *
 * The item stores only immutable snapshot data received from OpcUaService and
 * never owns live backend objects such as QOpcUaNode. Lazy loading is modeled
 * locally through FetchState and the hasChildren hint received from
 * the service.
 */
class TreeItem
{
public:
    /** Lazy child-loading state for a GUI-thread tree node. */
    enum class FetchState {
        /** Children were not requested yet. */
        NotFetched = 0,
        /** A browse request is currently pending. */
        Fetching,
        /** Children were fetched successfully or the node is known to be a leaf. */
        Fetched,
        /** The last browse request failed and may be retried. */
        Error
    };

    /** Creates the invisible root item. */
    explicit TreeItem(OpcUaModel *model);

    /** Creates a visible child item from snapshot data. */
    TreeItem(const OpcUaNodeData &data, OpcUaModel *model, TreeItem *parent);

    /** Destroys the item and all children. */
    ~TreeItem();

    /** Returns the child at \a row. */
    TreeItem *child(int row) const;

    /** Returns the row index of \a child. */
    int childIndex(const TreeItem *child) const;

    /** Returns the number of children. */
    int childCount() const;

    /** Returns the number of columns. */
    int columnCount() const { return 4; }

    /** Returns the row index in the parent. */
    int row() const;

    /** Returns the parent item. */
    TreeItem *parentItem() const { return m_parentItem; }

    /** Returns the column display data. */
    QVariant columnData(int column) const;

    /** Returns the node id. */
    QString nodeId() const { return m_nodeId; }

    /** Returns the browse name. */
    QString browseName() const { return m_browseName; }

    /** Returns the display name. */
    QString displayName() const { return m_displayName; }

    /** Returns the node class value. */
    int nodeClass() const { return m_nodeClass; }

    /** Returns the node class name. */
    QString nodeClassName() const;

    /** Returns the current value text. */
    QString valueString() const { return m_valueString; }

    /** Returns the current data type text. */
    QString dataTypeString() const { return m_dataTypeString; }

    /** Returns the description text. */
    QString description() const { return m_description; }

    /** Returns the icon name. */
    QString iconName() const;

    /** Returns whether the node supports monitoring. */
    bool supportsMonitoring() const;

    /** Returns whether monitoring is enabled in the GUI snapshot. */
    bool monitoringEnabled() const { return m_monitoringEnabled; }

    /** Sets the GUI snapshot monitoring state. */
    void setMonitoringEnabled(bool active) { m_monitoringEnabled = active; }

    /** Returns whether the service reported that children may exist. */
    bool mayHaveChildren() const { return m_hasChildren; }

    /** Returns whether the node can request more children. */
    bool canFetchMore() const;

    /** Returns the local fetch state. */
    FetchState fetchState() const { return m_fetchState; }

    /** Marks the node as currently fetching. */
    void markFetching();

    /** Applies a new fetch state. */
    void setFetchState(FetchState state);

    /** Replaces all current children with \a children. */
    void replaceChildren(std::vector<std::unique_ptr<TreeItem>> children);

private:
    /** Non-owning model pointer used for model-owned TreeItem operations. */
    OpcUaModel *m_model {nullptr};
    /** Non-owning parent item pointer; children are owned by \c m_children. */
    TreeItem *m_parentItem {nullptr};

    /** Owned child items in row order. */
    std::vector<std::unique_ptr<TreeItem>> m_children;

    /** OPC UA node id string used for follow-up browse requests. */
    QString m_nodeId;
    /** Browse name reported by the server. */
    QString m_browseName;
    /** Display name shown by the model. */
    QString m_displayName;
    /** Integer value of QOpcUa::NodeClass. */
    int m_nodeClass {0};
    /** Current value text shown by the browser. */
    QString m_valueString;
    /** Current data type text shown by the browser. */
    QString m_dataTypeString;
    /** Description text shown by the browser. */
    QString m_description;
    /** Whether the service reported that the node may have children. */
    bool m_hasChildren {false};
    /** Local GUI snapshot of monitoring state. */
    bool m_monitoringEnabled {false};

    /** Current lazy-fetch state. */
    FetchState m_fetchState {FetchState::NotFetched};
};

#endif // TREEITEM_H
