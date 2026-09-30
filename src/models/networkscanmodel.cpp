#include "networkscanmodel.h"

#include <QHostAddress>

#include <algorithm>

namespace {

/*!
 * \internal
 * \brief Returns the key that orders rows by numeric IPv4 address, then port.
 */
quint64 sortKey(const NetworkScanResult &result)
{
    return (quint64(QHostAddress(result.address).toIPv4Address()) << 16) | result.port;
}

} // namespace

/*!
 * \brief Creates an empty model owned by \a parent.
 */
NetworkScanModel::NetworkScanModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

/*!
 * \brief Returns the number of result rows; \a parent must be invalid for a list model.
 */
int NetworkScanModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

/*!
 * \brief Returns the value of \a role for the row at \a index.
 */
QVariant NetworkScanModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const NetworkScanResult &row = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case UrlRole:
        return row.url();
    case AddressRole:
        return row.address;
    case PortRole:
        return int(row.port);
    case ApplicationNameRole:
        return row.applicationName;
    case ApplicationUriRole:
        return row.applicationUri;
    case ProductUriRole:
        return row.productUri;
    case SecuritySummaryRole:
        return row.securitySummary;
    case AuthSummaryRole:
        return row.authSummary;
    case ResponseMsRole:
        return row.responseMs;
    case StatusRole:
        return int(row.status);
    case ErrorTextRole:
        return row.errorText;
    default:
        return {};
    }
}

/*!
 * \brief Returns the QML role names.
 */
QHash<int, QByteArray> NetworkScanModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {PortRole, "port"},
        {UrlRole, "url"},
        {ApplicationNameRole, "applicationName"},
        {ApplicationUriRole, "applicationUri"},
        {ProductUriRole, "productUri"},
        {SecuritySummaryRole, "securitySummary"},
        {AuthSummaryRole, "authSummary"},
        {ResponseMsRole, "responseMs"},
        {StatusRole, "status"},
        {ErrorTextRole, "errorText"},
    };
}

/*!
 * \brief Returns the number of result rows.
 */
int NetworkScanModel::count() const
{
    return int(m_rows.size());
}

/*!
 * \brief Removes all rows.
 */
void NetworkScanModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

/*!
 * \brief Inserts \a result in order, or updates the row with the same address and port.
 *
 * The scanner reports a row first as an open port and later with its OPC UA
 * details, so an update must keep the row's position and emit dataChanged().
 */
void NetworkScanModel::upsert(const NetworkScanResult &result)
{
    const quint64 key = sortKey(result);
    const auto it = std::lower_bound(m_rows.begin(), m_rows.end(), key,
                                     [](const NetworkScanResult &row, quint64 value) {
                                         return sortKey(row) < value;
                                     });
    const int row = int(std::distance(m_rows.begin(), it));
    if (it != m_rows.end() && sortKey(*it) == key) {
        *it = result;
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
        return;
    }

    beginInsertRows(QModelIndex(), row, row);
    m_rows.insert(row, result);
    endInsertRows();
    emit countChanged();
}

/*!
 * \brief Returns the result at \a row, or a default result when \a row is out of range.
 */
NetworkScanResult NetworkScanModel::resultAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : NetworkScanResult();
}
