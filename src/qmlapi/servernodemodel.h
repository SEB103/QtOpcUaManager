#ifndef SERVERNODEMODEL_H
#define SERVERNODEMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QScopedPointer>
#include <QString>

#include "serverproject/serverprojectdata.h"

/**
 * Design-time tree model over a Server Studio project's address space.
 *
 * Presents the project's flat node list as the parent/child tree implied by the
 * node parent references, for a QML TreeView. It is a pure snapshot model: the
 * ServerStudio facade owns the authoritative node list and calls setNodes()
 * after every edit, which rebuilds the tree. The model never talks to a server.
 */
class ServerNodeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    /** Roles exposed to the QML delegate. */
    enum Roles {
        NodeIdRole = Qt::UserRole + 1,
        ParentNodeIdRole,
        KindRole,        /**< ServerProject::NodeKind as int. */
        KindNameRole,    /**< "Folder"/"Object"/"Variable". */
        BrowseNameRole,
        DisplayNameRole,
        DataTypeRole,
        ValueRankRole,
        WritableRole,
        IsVariableRole,
        IconNameRole
    };
    Q_ENUM(Roles)

    /** Creates an empty model. */
    explicit ServerNodeModel(QObject *parent = nullptr);
    ~ServerNodeModel() override;

    /** Replaces the model contents with \a nodes, rebuilding the tree. */
    void setNodes(const QList<ServerProject::Node> &nodes);

    // QAbstractItemModel interface.
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    /** One tree item wrapping a project node (or the invisible root). */
    struct Item {
        ServerProject::Node node;
        Item *parent = nullptr;
        QList<Item *> children;
    };

    /** Returns the item for \a index, or the root when the index is invalid. */
    Item *itemFor(const QModelIndex &index) const;

    /** Invisible root whose children are the top-level nodes. */
    QScopedPointer<Item> m_root;
};

#endif // SERVERNODEMODEL_H
