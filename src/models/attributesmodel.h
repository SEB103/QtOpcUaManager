#ifndef ATTRIBUTESMODEL_H
#define ATTRIBUTESMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>

#include "core/opcuavaluedata.h"

/**
 * List model backing the Attributes panel.
 *
 * Each row is one attribute name/value pair for the node currently selected in
 * the address-space tree. The model is filled from an OpcUaAttributeData snapshot
 * read on demand by the OPC UA service.
 */
class AttributesModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /** Custom roles exposed to QML delegates. */
    enum Role {
        /** Attribute name text. */
        AttributeRole = Qt::UserRole + 1,
        /** Attribute value text. */
        ValueRole
    };
    Q_ENUM(Role)

    /** Creates an empty model. */
    explicit AttributesModel(QObject *parent = nullptr);

    /** Replaces all rows with the attributes described by \a data. */
    void setAttributes(const OpcUaAttributeData &data);

    /** Removes all rows. */
    void clear();

    /** Returns data for \a index and \a role. */
    QVariant data(const QModelIndex &index, int role) const override;
    /** Returns the number of rows below \a parent. */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns role names exposed to QML. */
    QHash<int, QByteArray> roleNames() const override;

private:
    /** One attribute name/value pair. */
    struct Entry
    {
        /** Attribute name text. */
        QString name;
        /** Attribute value text. */
        QString value;
    };

    /** Owned attribute rows in display order. */
    QList<Entry> m_entries;
};

#endif // ATTRIBUTESMODEL_H
