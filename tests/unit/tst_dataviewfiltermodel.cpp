#include <QSignalSpy>
#include <QtTest>

#include "models/dataaccessmodel.h"
#include "models/dataviewfiltermodel.h"

/*!
 * \internal
 * \brief Builds a monitored-node record for tests.
 */
static MonitoredNodeRecord makeRecord(const QString &nodeId, const QString &displayName)
{
    MonitoredNodeRecord record;
    record.server = QStringLiteral("srv");
    record.nodeId = nodeId;
    record.nodePath = QStringLiteral("Objects/") + displayName;
    record.displayName = displayName;
    record.dataType = QStringLiteral("DINT");
    return record;
}

/*!
 * \internal
 * \brief Applies a value \a value to the row identified by \a nodeId in \a model.
 */
static void setValue(DataAccessModel &model, const QString &nodeId, const QString &value)
{
    OpcUaValueUpdate update;
    update.nodeId = nodeId;
    update.value = value;
    update.statusCode = QStringLiteral("Good");
    model.updateValue(update);
}

/*! Verifies DataViewFilterModel sorting, filtering, and row mapping. */
class DataViewFilterModelTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that an unsorted, unfiltered proxy mirrors the source order. */
    void passesThroughWithoutSortOrFilter();

    /*! Verifies that the quick filter matches text in any column. */
    void filterMatchesAnyColumn();

    /*! Verifies that a value column of numbers sorts numerically, not as text. */
    void numericValuesSortNumerically();

    /*! Verifies that numbers and text in one column stay grouped. */
    void mixedValuesGroupNumbersFirst();

    /*! Verifies that repeated header clicks cycle ascending, descending, unsorted. */
    void toggleSortCyclesThroughThreeStates();

    /*! Verifies that view rows map back to the source rows they came from. */
    void rowsMapBackToTheSourceModel();
};

/*!
 * \brief Verifies that an unsorted, unfiltered proxy mirrors the source order.
 */
void DataViewFilterModelTest::passesThroughWithoutSortOrFilter()
{
    DataAccessModel source;
    source.addRow(makeRecord(QStringLiteral("ns=1;s=B"), QStringLiteral("Beta")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("Alpha")));

    DataViewFilterModel proxy;
    proxy.setSourceModel(&source);

    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(proxy.columnCount(), int(DataAccessModel::ColumnCount));
    QCOMPARE(proxy.sortColumn(), -1);

    // The project order is kept until the user sorts.
    QCOMPARE(proxy.data(proxy.index(0, DataAccessModel::DisplayNameColumn),
                        Qt::DisplayRole).toString(),
             QStringLiteral("Beta"));
}

/*!
 * \brief Verifies that the quick filter matches text in any column.
 */
void DataViewFilterModelTest::filterMatchesAnyColumn()
{
    DataAccessModel source;
    source.addRow(makeRecord(QStringLiteral("ns=1;s=Temp"), QStringLiteral("Temperature")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=Pres"), QStringLiteral("Pressure")));
    setValue(source, QStringLiteral("ns=1;s=Pres"), QStringLiteral("1013"));

    DataViewFilterModel proxy;
    proxy.setSourceModel(&source);

    QSignalSpy filterSpy(&proxy, &DataViewFilterModel::filterTextChanged);

    // Matching on the display name.
    proxy.setFilterText(QStringLiteral("temp"));
    QCOMPARE(filterSpy.count(), 1);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, DataAccessModel::DisplayNameColumn),
                        Qt::DisplayRole).toString(),
             QStringLiteral("Temperature"));

    // Matching on a value, which lives in a different column.
    proxy.setFilterText(QStringLiteral("1013"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, DataAccessModel::DisplayNameColumn),
                        Qt::DisplayRole).toString(),
             QStringLiteral("Pressure"));

    // Matching on the node id.
    proxy.setFilterText(QStringLiteral("ns=1;s=Temp"));
    QCOMPARE(proxy.rowCount(), 1);

    proxy.setFilterText(QStringLiteral("nothing here"));
    QCOMPARE(proxy.rowCount(), 0);

    proxy.setFilterText(QString());
    QCOMPARE(proxy.rowCount(), 2);
}

/*!
 * \brief Verifies that a value column of numbers sorts numerically, not as text.
 */
void DataViewFilterModelTest::numericValuesSortNumerically()
{
    DataAccessModel source;
    source.addRow(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("A")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=B"), QStringLiteral("B")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=C"), QStringLiteral("C")));
    setValue(source, QStringLiteral("ns=1;s=A"), QStringLiteral("10"));
    setValue(source, QStringLiteral("ns=1;s=B"), QStringLiteral("9"));
    setValue(source, QStringLiteral("ns=1;s=C"), QStringLiteral("100"));

    DataViewFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.applySort(DataAccessModel::ValueColumn, Qt::AscendingOrder);

    const auto valueAt = [&proxy](int row) {
        return proxy.data(proxy.index(row, DataAccessModel::ValueColumn),
                          Qt::DisplayRole).toString();
    };

    // Sorted as text this would be 10, 100, 9.
    QCOMPARE(valueAt(0), QStringLiteral("9"));
    QCOMPARE(valueAt(1), QStringLiteral("10"));
    QCOMPARE(valueAt(2), QStringLiteral("100"));

    proxy.applySort(DataAccessModel::ValueColumn, Qt::DescendingOrder);
    QCOMPARE(valueAt(0), QStringLiteral("100"));
    QCOMPARE(valueAt(2), QStringLiteral("9"));
}

/*!
 * \brief Verifies that numbers and text in one column stay grouped.
 */
void DataViewFilterModelTest::mixedValuesGroupNumbersFirst()
{
    DataAccessModel source;
    source.addRow(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("A")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=B"), QStringLiteral("B")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=C"), QStringLiteral("C")));
    setValue(source, QStringLiteral("ns=1;s=A"), QStringLiteral("RUNNING"));
    setValue(source, QStringLiteral("ns=1;s=B"), QStringLiteral("7"));
    setValue(source, QStringLiteral("ns=1;s=C"), QStringLiteral("IDLE"));

    DataViewFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.applySort(DataAccessModel::ValueColumn, Qt::AscendingOrder);

    const auto valueAt = [&proxy](int row) {
        return proxy.data(proxy.index(row, DataAccessModel::ValueColumn),
                          Qt::DisplayRole).toString();
    };

    // The number sorts before the text, and the text sorts among itself.
    QCOMPARE(valueAt(0), QStringLiteral("7"));
    QCOMPARE(valueAt(1), QStringLiteral("IDLE"));
    QCOMPARE(valueAt(2), QStringLiteral("RUNNING"));
}

/*!
 * \brief Verifies that repeated header clicks cycle ascending, descending, unsorted.
 */
void DataViewFilterModelTest::toggleSortCyclesThroughThreeStates()
{
    DataAccessModel source;
    source.addRow(makeRecord(QStringLiteral("ns=1;s=B"), QStringLiteral("Beta")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("Alpha")));

    DataViewFilterModel proxy;
    proxy.setSourceModel(&source);

    QSignalSpy sortSpy(&proxy, &DataViewFilterModel::sortingChanged);

    const auto firstName = [&proxy]() {
        return proxy.data(proxy.index(0, DataAccessModel::DisplayNameColumn),
                          Qt::DisplayRole).toString();
    };

    proxy.toggleSort(DataAccessModel::DisplayNameColumn);
    QCOMPARE(proxy.sortColumn(), int(DataAccessModel::DisplayNameColumn));
    QCOMPARE(proxy.sortOrder(), int(Qt::AscendingOrder));
    QCOMPARE(firstName(), QStringLiteral("Alpha"));

    proxy.toggleSort(DataAccessModel::DisplayNameColumn);
    QCOMPARE(proxy.sortOrder(), int(Qt::DescendingOrder));
    QCOMPARE(firstName(), QStringLiteral("Beta"));

    // A third click clears the sorting and restores the project order.
    proxy.toggleSort(DataAccessModel::DisplayNameColumn);
    QCOMPARE(proxy.sortColumn(), -1);
    QCOMPARE(firstName(), QStringLiteral("Beta"));

    QCOMPARE(sortSpy.count(), 3);

    // Sorting a different column starts over at ascending.
    proxy.toggleSort(DataAccessModel::DisplayNameColumn);
    proxy.toggleSort(DataAccessModel::NodeIdColumn);
    QCOMPARE(proxy.sortColumn(), int(DataAccessModel::NodeIdColumn));
    QCOMPARE(proxy.sortOrder(), int(Qt::AscendingOrder));

    // An out-of-range column is treated as "no sorting" rather than accepted.
    proxy.applySort(DataAccessModel::ColumnCount + 5, Qt::AscendingOrder);
    QCOMPARE(proxy.sortColumn(), -1);
}

/*!
 * \brief Verifies that view rows map back to the source rows they came from.
 */
void DataViewFilterModelTest::rowsMapBackToTheSourceModel()
{
    DataAccessModel source;
    source.addRow(makeRecord(QStringLiteral("ns=1;s=B"), QStringLiteral("Beta")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("Alpha")));
    source.addRow(makeRecord(QStringLiteral("ns=1;s=C"), QStringLiteral("Gamma")));

    DataViewFilterModel proxy;
    proxy.setSourceModel(&source);
    proxy.applySort(DataAccessModel::DisplayNameColumn, Qt::AscendingOrder);

    // Alpha is the first view row but the second source row.
    QCOMPARE(proxy.toSourceRow(0), 1);
    QCOMPARE(source.nodeIdAt(proxy.toSourceRow(0)), QStringLiteral("ns=1;s=A"));
    QCOMPARE(proxy.fromSourceRow(1), 0);

    QCOMPARE(proxy.toSourceRow(-1), -1);
    QCOMPARE(proxy.toSourceRow(99), -1);
    QCOMPARE(proxy.fromSourceRow(99), -1);

    // A filtered-out row has no view row at all.
    proxy.setFilterText(QStringLiteral("Alpha"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.fromSourceRow(0), -1);
    QCOMPARE(proxy.toSourceRow(0), 1);
}

QTEST_MAIN(DataViewFilterModelTest)

#include "tst_dataviewfiltermodel.moc"
