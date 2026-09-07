#include "dataviewfiltermodel.h"

#include "dataaccessmodel.h"

namespace {

/*!
 * \internal
 * \brief Returns whether \a value holds a number rather than text.
 */
bool isNumeric(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return true;
    default:
        return false;
    }
}

} // namespace

/*!
 * \property DataViewFilterModel::filterText
 * \brief Case-insensitive substring matched against every column.
 */

/*!
 * \property DataViewFilterModel::sortColumn
 * \brief Column the table is sorted by, or -1 when the project order is kept.
 */

/*!
 * \property DataViewFilterModel::sortOrder
 * \brief Sort direction as a Qt::SortOrder value.
 */

/*!
 * \brief Creates the proxy over the source model's sort-value role.
 */
DataViewFilterModel::DataViewFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSortRole(DataAccessModel::SortValueRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);

    // Reordering or hiding rows changes the number of rows that did not move, and
    // a view repaints only what it is told changed.
    connect(this, &QAbstractItemModel::layoutChanged,
            this, &DataViewFilterModel::refreshRowNumbers);
    connect(this, &QAbstractItemModel::modelReset,
            this, &DataViewFilterModel::refreshRowNumbers);
    connect(this, &QAbstractItemModel::rowsInserted,
            this, &DataViewFilterModel::refreshRowNumbers);
    connect(this, &QAbstractItemModel::rowsRemoved,
            this, &DataViewFilterModel::refreshRowNumbers);
}

/*!
 * \brief Returns the data of \a index for \a role.
 *
 * The row-number column is answered here rather than by the source model: it
 * shows the position in the visible table, which the sorting and the quick
 * filter define and the source model cannot know. Every other role, the sort key
 * of that column included, still comes from the source model, so sorting by the
 * number means the project order.
 */
QVariant DataViewFilterModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole && index.isValid()
        && index.column() == int(DataAccessModel::RowNumberColumn)) {
        return QString::number(index.row() + 1);
    }

    return QSortFilterProxyModel::data(index, role);
}

/*!
 * \internal
 * \brief Re-emits dataChanged() for the whole row-number column.
 */
void DataViewFilterModel::refreshRowNumbers()
{
    const int rows = rowCount();
    if (rows <= 0)
        return;

    emit dataChanged(index(0, DataAccessModel::RowNumberColumn),
                     index(rows - 1, DataAccessModel::RowNumberColumn),
                     {Qt::DisplayRole});
}

/*!
 * \brief Sets the quick-filter \a text.
 */
void DataViewFilterModel::setFilterText(const QString &text)
{
    if (m_filterText == text)
        return;

    m_filterText = text;
    invalidateRowsFilter();
    emit filterTextChanged();
}

/*!
 * \brief Cycles the sorting of \a column between ascending, descending, and none.
 */
void DataViewFilterModel::toggleSort(int column)
{
    if (column != m_sortColumn) {
        applySort(column, Qt::AscendingOrder);
        return;
    }

    if (m_sortOrder == Qt::AscendingOrder)
        applySort(column, Qt::DescendingOrder);
    else
        applySort(-1, Qt::AscendingOrder);
}

/*!
 * \brief Sorts by \a column in \a order, or restores the source order.
 * \param column The column to sort by, or -1 to keep the source model's order.
 * \param order A Qt::SortOrder value applied when \a column is valid.
 */
void DataViewFilterModel::applySort(int column, int order)
{
    const int columnCount = sourceModel() ? sourceModel()->columnCount() : 0;
    const int effectiveColumn = (column >= 0 && column < columnCount) ? column : -1;
    const Qt::SortOrder effectiveOrder =
        order == int(Qt::DescendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;

    if (effectiveColumn == m_sortColumn && effectiveOrder == m_sortOrder)
        return;

    m_sortColumn = effectiveColumn;
    m_sortOrder = effectiveOrder;

    // Sorting by column -1 makes QSortFilterProxyModel fall back to the source
    // order, which is exactly the "unsorted" state the header cycles back to.
    sort(m_sortColumn, m_sortOrder);
    emit sortingChanged();
}

/*!
 * \brief Returns the source row for the view row \a proxyRow.
 */
int DataViewFilterModel::toSourceRow(int proxyRow) const
{
    const QModelIndex proxyIndex = index(proxyRow, 0);
    if (!proxyIndex.isValid())
        return -1;

    const QModelIndex sourceIndex = mapToSource(proxyIndex);
    return sourceIndex.isValid() ? sourceIndex.row() : -1;
}

/*!
 * \brief Returns the view row for the source row \a sourceRow.
 */
int DataViewFilterModel::fromSourceRow(int sourceRow) const
{
    if (!sourceModel())
        return -1;

    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0);
    if (!sourceIndex.isValid())
        return -1;

    const QModelIndex proxyIndex = mapFromSource(sourceIndex);
    return proxyIndex.isValid() ? proxyIndex.row() : -1;
}

/*!
 * \brief Returns whether \a sourceRow matches the quick filter in any column.
 */
bool DataViewFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_filterText.isEmpty())
        return true;

    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return true;

    const int columns = model->columnCount(sourceParent);
    for (int column = 0; column < columns; ++column) {
        const QModelIndex index = model->index(sourceRow, column, sourceParent);
        if (model->data(index, Qt::DisplayRole).toString().contains(m_filterText,
                                                                    Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

/*!
 * \brief Compares \a left and \a right by their typed sort value.
 *
 * Numbers are compared numerically so 9 sorts before 10, and text is compared
 * case-insensitively. In a column that mixes both — a value column holding
 * numbers on some rows and strings on others — numbers sort first so each kind
 * stays grouped instead of interleaving by string order.
 */
bool DataViewFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return false;

    const QVariant leftValue = model->data(left, DataAccessModel::SortValueRole);
    const QVariant rightValue = model->data(right, DataAccessModel::SortValueRole);

    const bool leftNumeric = isNumeric(leftValue);
    const bool rightNumeric = isNumeric(rightValue);

    if (leftNumeric && rightNumeric)
        return leftValue.toDouble() < rightValue.toDouble();
    if (leftNumeric != rightNumeric)
        return leftNumeric;

    return QString::compare(leftValue.toString(), rightValue.toString(),
                            Qt::CaseInsensitive)
           < 0;
}
