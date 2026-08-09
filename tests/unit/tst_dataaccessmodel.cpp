#include <QSignalSpy>
#include <QtTest>

#include "models/dataaccessmodel.h"

/*!
 * \internal
 * \brief Builds a monitored-node record for tests.
 */
static MonitoredNodeRecord makeRecord(const QString &nodeId)
{
    MonitoredNodeRecord record;
    record.server = QStringLiteral("srv");
    record.nodeId = nodeId;
    record.nodePath = QStringLiteral("Objects/") + nodeId;
    record.displayName = nodeId;
    record.dataType = QStringLiteral("String");
    return record;
}

/*! Verifies DataAccessModel row management and live value updates. */
class DataAccessModelTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that addRow() appends rows and rejects duplicates. */
    void addRowAppendsAndDeduplicates();

    /*! Verifies that removeAt() removes a row. */
    void removeAtRemovesRow();

    /*! Verifies that updateValue() fills the value fields of the matching row. */
    void updateValueUpdatesMatchingRow();

    /*! Verifies that clearValues() clears live fields but keeps rows. */
    void clearValuesKeepsRows();

    /*! Verifies that setRecords() replaces all rows. */
    void setRecordsReplacesRows();
};

/*!
 * \brief Verifies that addRow() appends rows and rejects duplicates.
 */
void DataAccessModelTest::addRowAppendsAndDeduplicates()
{
    DataAccessModel model;
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(model.addRow(makeRecord(QStringLiteral("ns=1;s=A"))));
    QCOMPARE(model.rowCount(), 1);

    // Adding the same (server, nodeId) again must be rejected.
    QVERIFY(!model.addRow(makeRecord(QStringLiteral("ns=1;s=A"))));
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.addRow(makeRecord(QStringLiteral("ns=1;s=B"))));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.nodeIdAt(1), QStringLiteral("ns=1;s=B"));
}

/*!
 * \brief Verifies that removeAt() removes a row.
 */
void DataAccessModelTest::removeAtRemovesRow()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=B")));

    model.removeAt(0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.nodeIdAt(0), QStringLiteral("ns=1;s=B"));
}

/*!
 * \brief Verifies that updateValue() fills the value fields of the matching row.
 */
void DataAccessModelTest::updateValueUpdatesMatchingRow()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    OpcUaValueUpdate update;
    update.nodeId = QStringLiteral("ns=1;s=A");
    update.value = QStringLiteral("42");
    update.dataType = QStringLiteral("Int32");
    update.statusCode = QStringLiteral("Good");
    model.updateValue(update);

    QCOMPARE(spy.size(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, DataAccessModel::ValueRole).toString(), QStringLiteral("42"));
    QCOMPARE(model.data(index, DataAccessModel::StatusCodeRole).toString(), QStringLiteral("Good"));

    // An update for an unknown node id must be ignored.
    OpcUaValueUpdate unknown;
    unknown.nodeId = QStringLiteral("ns=1;s=Z");
    unknown.value = QStringLiteral("999");
    model.updateValue(unknown);
    QCOMPARE(spy.size(), 1);
}

/*!
 * \brief Verifies that clearValues() clears live fields but keeps rows.
 */
void DataAccessModelTest::clearValuesKeepsRows()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    OpcUaValueUpdate update;
    update.nodeId = QStringLiteral("ns=1;s=A");
    update.value = QStringLiteral("42");
    model.updateValue(update);

    model.clearValues();
    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0, 0);
    QVERIFY(model.data(index, DataAccessModel::ValueRole).toString().isEmpty());
}

/*!
 * \brief Verifies that setRecords() replaces all rows.
 */
void DataAccessModelTest::setRecordsReplacesRows()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    const QList<MonitoredNodeRecord> records {
        makeRecord(QStringLiteral("ns=1;s=X")),
        makeRecord(QStringLiteral("ns=1;s=Y"))
    };
    model.setRecords(records);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.nodeIdAt(0), QStringLiteral("ns=1;s=X"));
    QCOMPARE(model.nodeIdAt(1), QStringLiteral("ns=1;s=Y"));
}

QTEST_MAIN(DataAccessModelTest)

#include "tst_dataaccessmodel.moc"
