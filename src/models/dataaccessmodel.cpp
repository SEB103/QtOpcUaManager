#include <QDateTime>

#include "dataaccessmodel.h"

namespace {

/*!
 * \internal
 * \brief Classifies the OPC UA status text \a statusCode into a severity.
 *
 * QOpcUa::statusToString() produces names such as "Good", "UncertainLastUsableValue",
 * or "BadNodeIdUnknown", so the leading word carries the quality. An empty text
 * means no value has been reported yet and stays Unknown rather than being
 * treated as an error.
 */
DataAccessModel::StatusSeverity severityForStatus(const QString &statusCode)
{
    if (statusCode.isEmpty())
        return DataAccessModel::StatusUnknown;
    if (statusCode.startsWith(QLatin1String("Good"), Qt::CaseInsensitive))
        return DataAccessModel::StatusGood;
    if (statusCode.startsWith(QLatin1String("Uncertain"), Qt::CaseInsensitive))
        return DataAccessModel::StatusUncertain;
    if (statusCode.startsWith(QLatin1String("Bad"), Qt::CaseInsensitive))
        return DataAccessModel::StatusBad;
    return DataAccessModel::StatusUnknown;
}

} // namespace

/*!
 * \brief Constructs an empty Data Access View model.
 */
DataAccessModel::DataAccessModel(QObject *parent)
    : QAbstractTableModel(parent)
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
        m_rows.push_back(Row{record, {}, {}, {}, {}, 0, OpcUaAccessLevel::Unknown});
    endResetModel();
}

/*!
 * \brief Returns the persistent identity and metadata of every row, in display order.
 *
 * Live value, timestamp, and status fields are excluded; only the persistable
 * MonitoredNodeRecord of each row is returned, for saving into a project file.
 */
QList<MonitoredNodeRecord> DataAccessModel::records() const
{
    QList<MonitoredNodeRecord> result;
    result.reserve(m_rows.size());
    for (const Row &row : m_rows)
        result.append(row.record);
    return result;
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
    m_rows.push_back(Row{record, {}, {}, {}, {}, 0, OpcUaAccessLevel::Unknown});
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

    // Row numbers shift, so refresh the whole first column for the remaining rows.
    if (!m_rows.isEmpty()) {
        const QModelIndex top = index(0, RowNumberColumn);
        const QModelIndex bottom = index(m_rows.size() - 1, RowNumberColumn);
        emit dataChanged(top, bottom, {Qt::DisplayRole, RowNumberRole, SortValueRole});
    }
}

/*!
 * \brief Applies live value \a update to the matching row.
 *
 * The row is located by node id; unknown node ids are ignored so that stale
 * updates after a row was removed do not resurrect it. The arrival time is
 * recorded so the view can tell a row that has received a value from one that
 * is still waiting for its first update.
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
    target.lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
    if (!update.dataType.isEmpty())
        target.record.dataType = update.dataType;

    // Every column of the row can show changed data, so refresh the whole row.
    const QModelIndex left = index(row, 0);
    const QModelIndex right = index(row, ColumnCount - 1);
    emit dataChanged(left, right,
                     {Qt::DisplayRole, ValueRole, DataTypeRole, SourceTimestampRole,
                      ServerTimestampRole, StatusCodeRole, StatusSeverityRole,
                      LastUpdateMsRole, SortValueRole});
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
        row.lastUpdateMs = 0;
    }

    const QModelIndex left = index(0, 0);
    const QModelIndex right = index(m_rows.size() - 1, ColumnCount - 1);
    emit dataChanged(left, right,
                     {Qt::DisplayRole, ValueRole, SourceTimestampRole, ServerTimestampRole,
                      StatusCodeRole, StatusSeverityRole, LastUpdateMsRole, SortValueRole});
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
 * \brief Returns the data-type text at \a row.
 */
QString DataAccessModel::dataTypeAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).record.dataType;
}

/*!
 * rief Returns the display name at  row.
 */
QString DataAccessModel::displayNameAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).record.displayName;
}

/*!
 * \brief Returns the AccessLevel at \a row.
 */
int DataAccessModel::accessLevelAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return OpcUaAccessLevel::Unknown;
    return m_rows.at(row).accessLevel;
}

/*!
 * \brief Returns the Writability of \a row.
 */
int DataAccessModel::writabilityAt(int row) const
{
    const int accessLevel = accessLevelAt(row);
    if (!OpcUaAccessLevel::isKnown(accessLevel))
        return WritabilityUnknown;
    return OpcUaAccessLevel::allowsWrite(accessLevel) ? WritabilityWritable
                                                      : WritabilityReadOnly;
}

/*!
 * \brief Records the AccessLevel \a accessLevel for the row holding \a nodeId.
 * \return \c true when a row was updated.
 */
bool DataAccessModel::setAccessLevelForNode(const QString &nodeId, int accessLevel)
{
    const int row = indexForNodeId(nodeId);
    if (row < 0 || m_rows.at(row).accessLevel == accessLevel)
        return false;

    m_rows[row].accessLevel = accessLevel;

    const QModelIndex left = index(row, 0);
    const QModelIndex right = index(row, ColumnCount - 1);
    emit dataChanged(left, right, {AccessLevelRole, WritabilityRole});
    return true;
}

/*!
 * \brief Returns the browse path at \a row.
 */
QString DataAccessModel::nodePathAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    return m_rows.at(row).record.nodePath;
}

/*!
 * \brief Returns the sampling interval at \a row in milliseconds.
 */
int DataAccessModel::samplingIntervalAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return 0;
    return m_rows.at(row).record.samplingIntervalMs;
}

/*!
 * \brief Sets the sampling interval at \a row to \a intervalMs milliseconds.
 * \return \c true when the stored interval changed.
 */
bool DataAccessModel::setSamplingIntervalAt(int row, int intervalMs)
{
    if (row < 0 || row >= m_rows.size())
        return false;

    const int sanitized = intervalMs > 0 ? intervalMs : 0;
    if (m_rows.at(row).record.samplingIntervalMs == sanitized)
        return false;

    m_rows[row].record.samplingIntervalMs = sanitized;

    const QModelIndex changed = index(row, IntervalColumn);
    emit dataChanged(changed, changed,
                     {Qt::DisplayRole, SamplingIntervalRole, SortValueRole});
    return true;
}

/*!
 * \brief Returns the translated header title of \a column.
 */
QString DataAccessModel::columnTitle(int column) const
{
    switch (column) {
    case RowNumberColumn: return tr("#");
    case DisplayNameColumn: return tr("Display Name");
    case ValueColumn: return tr("Value");
    case DataTypeColumn: return tr("Data Type");
    case StatusColumn: return tr("Status");
    case IntervalColumn: return tr("Interval, ms");
    case SourceTimestampColumn: return tr("Source Timestamp");
    case ServerTimestampColumn: return tr("Server Timestamp");
    case NodePathColumn: return tr("Node Path");
    case NodeIdColumn: return tr("Node Id");
    case ServerColumn: return tr("Server");
    default: return {};
    }
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
 * \brief Returns the display text of \a column for \a row.
 * \param rowNumber The one-based position of the row in the table.
 */
QString DataAccessModel::cellText(const Row &row, int column, int rowNumber) const
{
    switch (column) {
    case RowNumberColumn: return QString::number(rowNumber);
    case DisplayNameColumn: return row.record.displayName;
    case ValueColumn: return row.value;
    case DataTypeColumn: return row.record.dataType;
    case StatusColumn: return row.statusCode;
    case IntervalColumn:
        // An unset interval follows the service default, which the user cannot
        // read off a number, so show a dash instead of a misleading zero.
        return row.record.samplingIntervalMs > 0
                   ? QString::number(row.record.samplingIntervalMs)
                   : QStringLiteral("—");
    case SourceTimestampColumn: return row.sourceTimestamp;
    case ServerTimestampColumn: return row.serverTimestamp;
    case NodePathColumn: return row.record.nodePath;
    case NodeIdColumn: return row.record.nodeId;
    case ServerColumn: return row.record.server;
    default: return {};
    }
}

/*!
 * \brief Returns a typed sort key for \a column of \a row.
 * \param rowNumber The one-based position of the row in the table.
 */
QVariant DataAccessModel::sortValue(const Row &row, int column, int rowNumber) const
{
    switch (column) {
    case RowNumberColumn:
        return rowNumber;
    case IntervalColumn:
        return row.record.samplingIntervalMs;
    case ValueColumn: {
        // Numeric PLC values must sort numerically; anything else falls back to
        // its text so mixed or non-numeric columns still sort predictably.
        bool ok = false;
        const double numeric = row.value.toDouble(&ok);
        return ok ? QVariant(numeric) : QVariant(row.value);
    }
    default:
        return cellText(row, column, rowNumber);
    }
}

/*!
 * \brief Returns model data for \a index and \a role.
 */
QVariant DataAccessModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    const int rowNumber = index.row() + 1;

    switch (role) {
    case Qt::DisplayRole: return cellText(row, index.column(), rowNumber);
    case SortValueRole: return sortValue(row, index.column(), rowNumber);
    case RowNumberRole: return rowNumber;
    case ServerRole: return row.record.server;
    case NodeIdRole: return row.record.nodeId;
    case NodePathRole: return row.record.nodePath;
    case DisplayNameRole: return row.record.displayName;
    case ValueRole: return row.value;
    case DataTypeRole: return row.record.dataType;
    case SourceTimestampRole: return row.sourceTimestamp;
    case ServerTimestampRole: return row.serverTimestamp;
    case StatusCodeRole: return row.statusCode;
    case StatusSeverityRole: return int(severityForStatus(row.statusCode));
    case SamplingIntervalRole: return row.record.samplingIntervalMs;
    case LastUpdateMsRole: return row.lastUpdateMs;
    case AccessLevelRole: return row.accessLevel;
    case WritabilityRole: return writabilityAt(index.row());
    default: return {};
    }
}

/*!
 * \brief Returns the horizontal header title for \a section.
 */
QVariant DataAccessModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    return columnTitle(section);
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
 * \brief Returns the number of columns below \a parent.
 */
int DataAccessModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return int(ColumnCount);
}

/*!
 * \brief Returns the role names exposed to QML.
 */
QHash<int, QByteArray> DataAccessModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},
        {RowNumberRole, "rowNumber"},
        {ServerRole, "server"},
        {NodeIdRole, "nodeId"},
        {NodePathRole, "nodePath"},
        {DisplayNameRole, "displayName"},
        {ValueRole, "value"},
        {DataTypeRole, "dataType"},
        {SourceTimestampRole, "sourceTimestamp"},
        {ServerTimestampRole, "serverTimestamp"},
        {StatusCodeRole, "statusCode"},
        {StatusSeverityRole, "statusSeverity"},
        {SamplingIntervalRole, "samplingInterval"},
        {LastUpdateMsRole, "lastUpdateMs"},
        {SortValueRole, "sortValue"},
        {AccessLevelRole, "accessLevel"},
        {WritabilityRole, "writability"}
    };
}
