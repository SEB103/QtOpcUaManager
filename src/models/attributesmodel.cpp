#include "attributesmodel.h"

/*!
 * \brief Constructs an empty attributes model.
 */
AttributesModel::AttributesModel(QObject *parent)
    : QAbstractListModel(parent)
{}

/*!
 * \brief Replaces all rows with the attributes described by \a data.
 *
 * Only attributes with a non-empty value are shown, so unavailable attributes do
 * not clutter the panel.
 */
void AttributesModel::setAttributes(const OpcUaAttributeData &data)
{
    QList<Entry> entries;
    const auto append = [&entries](const QString &name, const QString &value) {
        if (!value.isEmpty())
            entries.push_back(Entry{name, value});
    };

    append(QStringLiteral("NodeId"), data.nodeId);
    append(QStringLiteral("NodeClass"), data.nodeClassName);
    append(QStringLiteral("BrowseName"), data.browseName);
    append(QStringLiteral("DisplayName"), data.displayName);
    append(QStringLiteral("Description"), data.description);
    append(QStringLiteral("Value"), data.value);
    append(QStringLiteral("DataType"), data.dataType);
    append(QStringLiteral("SourceTimestamp"), data.sourceTimestamp);
    append(QStringLiteral("ServerTimestamp"), data.serverTimestamp);
    append(QStringLiteral("StatusCode"), data.statusCode);

    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

/*!
 * \brief Removes all rows.
 */
void AttributesModel::clear()
{
    if (m_entries.isEmpty())
        return;

    beginResetModel();
    m_entries.clear();
    endResetModel();
}

/*!
 * \brief Returns model data for \a index and \a role.
 */
QVariant AttributesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case AttributeRole: return entry.name;
    case ValueRole: return entry.value;
    default: return {};
    }
}

/*!
 * \brief Returns the number of rows below \a parent.
 */
int AttributesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return int(m_entries.size());
}

/*!
 * \brief Returns the role names exposed to QML.
 */
QHash<int, QByteArray> AttributesModel::roleNames() const
{
    return {
        {AttributeRole, "attribute"},
        {ValueRole, "value"}
    };
}
