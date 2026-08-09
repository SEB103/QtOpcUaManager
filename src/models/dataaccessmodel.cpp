#include "dataaccessmodel.h"

/*!
 * \brief Constructs an empty Data Access View model.
 */
DataAccessModel::DataAccessModel(QObject *parent)
    : QAbstractListModel(parent)
{}

/*!
 * \brief Replaces all rows with \a records.
 *
 * Live value fields are left empty; they are filled once the server reports a
 * value for each node.
 */
void DataAccessModel::setRecords(const QList<MonitoredNodeRecord> &records)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(records.size());
    for (const auto &record : records)
        m_rows.push_back(Row{record, {}, {}, {}, {}});
    endResetModel();
}

/*!
 * \brief Appends \a record when its (server, nodeId) pair is not present.
 * \return \c true when a new row was appended.
 */
bool DataAccessModel::addRow(const MonitoredNodeRecord &record)
{
    if (contains(record.server, record.nodeId))
        return false;

    const int row = m_rows.size();
    beginInsertRows(QModelIndex(), row, row);
    m_rows.push_back(Row{record, {}, {}, {}, {}});
    endInsertRows();
    return true;
}

/*!
 * \brief Removes the row at \a row when it is within range.
 */
void DataAccessModel::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();

    // Row numbers shift, so refresh the RowNumberRole for the remaining rows.
    if (!m_rows.isEmpty()) {
        const QModelIndex top = index(0, 0);
        const QModelIndex bottom = index(m_rows.size() - 1, 0);
        emit dataChanged(top, bottom, {RowNumberRole});
    }
}

/*!
 * \brief Applies live value \a update to the matching row.
 *
 * The row is located by node id; unknown node ids are ignored so that stale
 * updates after a row was removed do not resurrect it.
 */
void DataAccessModel::updateValue(const OpcUaValueUpdate &update)
{
    const int row = indexForNodeId(update.nodeId);
    if (row < 0)
        return;

    Row &target = m_rows[row];
    target.value = update.value;
    target.sourceTimestamp = update.sourceTimestamp;
    target.serverTimestamp = update.serverTimestamp;
    target.statusCode = update.statusCode;
    if (!update.dataType.isEmpty())
        target.record.dataType = update.dataType;

    const QModelIndex changed = index(row, 0);
    emit dataChanged(changed, changed,
                     {ValueRole, DataTypeRole, SourceTimestampRole, ServerTimestampRole,
                      StatusCodeRole});
}

/*!
 * \brief Clears live value, timestamp, and status text for every row.
 *
 * Called when the session disconnects so that the table no longer shows values
 * that are no longer being updated.
 */
void DataAccessModel::clearValues()
{
    if (m_rows.isEmpty())
        return;

    for (Row &row : m_rows) {
        row.value.clear();
        row.sourceTimestamp.clear();
        row.serverTimestamp.clear();
        row.statusCode.clear();
    }

    const QModelIndex top = index(0, 0);
    const QModelIndex bottom = index(m_rows.size() - 1, 0);
    emit dataChanged(top, bottom,
                     {ValueRole, SourceTimestampRole, ServerTimestampRole, StatusCodeRole});
}

/*!
 * \brief Returns whether a row with \a server and \a nodeId exists.
 */
bool DataAccessModel::contains(const QString &server, const QString &nodeId) const
{
    for (const Row &row : m_rows) {
        if (row.record.server == server && row.record.nodeId == nodeId)
            return true;
    }
    return false;
}

/*!
 * \brief Returns the node id at \a row.
 */
QString DataAccessModel::nodeIdAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).record.nodeId;
}

/*!
 * \brief Returns the server at \a row.
 */
QString DataAccessModel::serverAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).record.server;
}

/*!
 * \brief Returns the current value text at \a row.
 */
QString DataAccessModel::valueAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).value;
}

/*!
 * \brief Returns the row index for \a nodeId, or -1 when not present.
 */
int DataAccessModel::indexForNodeId(const QString &nodeId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).record.nodeId == nodeId)
            return i;
    }
    return -1;
}

/*!
 * \brief Returns model data for \a index and \a role.
 */
QVariant DataAccessModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case RowNumberRole: return index.row() + 1;
    case ServerRole: return row.record.server;
    case NodeIdRole: return row.record.nodeId;
    case NodePathRole: return row.record.nodePath;
    case DisplayNameRole: return row.record.displayName;
    case ValueRole: return row.value;
    case DataTypeRole: return row.record.dataType;
    case SourceTimestampRole: return row.sourceTimestamp;
    case ServerTimestampRole: return row.serverTimestamp;
    case StatusCodeRole: return row.statusCode;
    default: return {};
    }
}

/*!
 * \brief Returns the number of rows below \a parent.
 */
int DataAccessModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return int(m_rows.size());
}

/*!
 * \brief Returns the role names exposed to QML.
 */
QHash<int, QByteArray> DataAccessModel::roleNames() const
{
    return {
        {RowNumberRole, "rowNumber"},
        {ServerRole, "server"},
        {NodeIdRole, "nodeId"},
        {NodePathRole, "nodePath"},
        {DisplayNameRole, "displayName"},
        {ValueRole, "value"},
        {DataTypeRole, "dataType"},
        {SourceTimestampRole, "sourceTimestamp"},
        {ServerTimestampRole, "serverTimestamp"},
        {StatusCodeRole, "statusCode"}
    };
}
