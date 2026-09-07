#ifndef DATAVIEWFILTERMODEL_H
#define DATAVIEWFILTERMODEL_H

#include <QSortFilterProxyModel>
#include <QString>

/**
 * Sorting and quick-filter proxy in front of DataAccessModel.
 *
 * The Data Access View shows this proxy instead of the source model so the user
 * can sort by any column and narrow the table by free text without the source
 * model losing its stable, project-ordered row list. Row positions therefore
 * differ between the two models, and callers that hold a view row must map it
 * back with toSourceRow() before touching the OPC UA facade.
 *
 * For the same reason the proxy, not the source model, supplies the text of the
 * row-number column: it is the position in the visible table, which only the
 * view order defines. Sorting by that column still means the project order,
 * because the sort key keeps coming from the source model.
 */
class DataViewFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

    /** Case-insensitive substring matched against every column; empty shows all rows. */
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

    /** Column the table is sorted by, or -1 when the project order is kept. */
    Q_PROPERTY(int sortColumn READ sortColumn NOTIFY sortingChanged)

    /** Sort direction as a Qt::SortOrder value; meaningful only while sorted. */
    Q_PROPERTY(int sortOrder READ sortOrder NOTIFY sortingChanged)

public:
    /** Creates the proxy with case-insensitive sorting over the sort-value role. */
    explicit DataViewFilterModel(QObject *parent = nullptr);

    /** Returns the current quick-filter text. */
    QString filterText() const { return m_filterText; }

    /** Sets the quick-filter \a text and re-evaluates which rows are shown. */
    void setFilterText(const QString &text);

    /** Returns the sorted column, or -1 when unsorted. */
    int sortColumn() const { return m_sortColumn; }

    /** Returns the sort direction as a Qt::SortOrder value. */
    int sortOrder() const { return int(m_sortOrder); }

    /**
     * Sorts by \a column, toggling the direction when \a column is already the
     * sorted one and clearing the sorting when it is toggled past descending, so
     * a repeated header click cycles ascending, descending, project order.
     */
    Q_INVOKABLE void toggleSort(int column);

    /** Sorts by \a column in \a order, or restores the project order when \a column is -1. */
    Q_INVOKABLE void applySort(int column, int order);

    /** Returns the source row for the view row \a proxyRow, or -1 when invalid. */
    Q_INVOKABLE int toSourceRow(int proxyRow) const;

    /** Returns the view row for the source row \a sourceRow, or -1 when filtered out. */
    Q_INVOKABLE int fromSourceRow(int sourceRow) const;

    /**
     * Returns the data of \a index for \a role, numbering the row-number column
     * by view position instead of by source row.
     */
    QVariant data(const QModelIndex &index, int role) const override;

signals:
    /** Emitted when the quick-filter text changes. */
    void filterTextChanged();

    /** Emitted when the sorted column or direction changes. */
    void sortingChanged();

protected:
    /** Returns whether \a sourceRow matches the quick filter in any column. */
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

    /** Compares \a left and \a right by their typed sort value. */
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    /**
     * Re-emits dataChanged() for the whole row-number column, so rows that keep
     * their position still pick up the number the new order gives them.
     */
    void refreshRowNumbers();

    /** Current quick-filter text; empty accepts every row. */
    QString m_filterText;
    /** Sorted column, or -1 while the source order is kept. */
    int m_sortColumn {-1};
    /** Current sort direction. */
    Qt::SortOrder m_sortOrder {Qt::AscendingOrder};
};

#endif // DATAVIEWFILTERMODEL_H
