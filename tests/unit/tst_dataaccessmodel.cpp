#include <QSignalSpy>
#include <QtTest>

#include "core/opcuaaccesslevel.h"
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

    /*! Verifies that the model exposes one column per table column with a title. */
    void exposesTitledColumns();

    /*! Verifies that each column renders the expected display text. */
    void columnsRenderTheExpectedText();

    /*! Verifies that the status text is classified into a severity. */
    void statusTextIsClassified();

    /*! Verifies that the sampling interval is stored, shown, and persisted. */
    void samplingIntervalIsStoredAndShown();

    /*! Verifies that numeric columns expose a numeric sort value. */
    void numericColumnsSortNumerically();

    /*! Verifies that writability stays unknown until the server reports it. */
    void writabilityStartsUnknown();

    /*! Verifies that the access level decides whether a row can be written. */
    void accessLevelDecidesWritability();
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

/*!
 * \brief Verifies that the model exposes one column per table column with a title.
 */
void DataAccessModelTest::exposesTitledColumns()
{
    DataAccessModel model;

    QCOMPARE(model.columnCount(), int(DataAccessModel::ColumnCount));

    // Every declared column must carry a header title, and nothing beyond them.
    for (int column = 0; column < DataAccessModel::ColumnCount; ++column) {
        QVERIFY2(!model.columnTitle(column).isEmpty(),
                 qPrintable(QStringLiteral("column %1 has no title").arg(column)));
        QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString(),
                 model.columnTitle(column));
    }
    QVERIFY(model.columnTitle(DataAccessModel::ColumnCount).isEmpty());

    // Vertical headers are not used by the table.
    QVERIFY(!model.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());
}

/*!
 * \brief Verifies that each column renders the expected display text.
 */
void DataAccessModelTest::columnsRenderTheExpectedText()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    OpcUaValueUpdate update;
    update.nodeId = QStringLiteral("ns=1;s=A");
    update.value = QStringLiteral("42");
    update.dataType = QStringLiteral("DINT");
    update.sourceTimestamp = QStringLiteral("2026-09-05T10:00:00");
    update.serverTimestamp = QStringLiteral("2026-09-05T10:00:01");
    update.statusCode = QStringLiteral("Good");
    model.updateValue(update);

    const auto text = [&model](DataAccessModel::Column column) {
        return model.data(model.index(0, column), Qt::DisplayRole).toString();
    };

    QCOMPARE(text(DataAccessModel::RowNumberColumn), QStringLiteral("1"));
    QCOMPARE(text(DataAccessModel::DisplayNameColumn), QStringLiteral("ns=1;s=A"));
    QCOMPARE(text(DataAccessModel::ValueColumn), QStringLiteral("42"));
    QCOMPARE(text(DataAccessModel::DataTypeColumn), QStringLiteral("DINT"));
    QCOMPARE(text(DataAccessModel::StatusColumn), QStringLiteral("Good"));
    QCOMPARE(text(DataAccessModel::SourceTimestampColumn),
             QStringLiteral("2026-09-05T10:00:00"));
    QCOMPARE(text(DataAccessModel::ServerTimestampColumn),
             QStringLiteral("2026-09-05T10:00:01"));
    QCOMPARE(text(DataAccessModel::NodePathColumn), QStringLiteral("Objects/ns=1;s=A"));
    QCOMPARE(text(DataAccessModel::NodeIdColumn), QStringLiteral("ns=1;s=A"));
    QCOMPARE(text(DataAccessModel::ServerColumn), QStringLiteral("srv"));

    // Row-level roles describe the row and stay the same in every column.
    QCOMPARE(model.data(model.index(0, DataAccessModel::ServerColumn),
                        DataAccessModel::NodeIdRole).toString(),
             QStringLiteral("ns=1;s=A"));

    // An update also records its arrival, which the view uses to dim rows that
    // have not received anything yet.
    QVERIFY(model.data(model.index(0, 0), DataAccessModel::LastUpdateMsRole).toLongLong() > 0);

    model.clearValues();
    QCOMPARE(model.data(model.index(0, 0), DataAccessModel::LastUpdateMsRole).toLongLong(), 0);
}

/*!
 * \brief Verifies that the status text is classified into a severity.
 */
void DataAccessModelTest::statusTextIsClassified()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    const auto severityFor = [&model](const QString &statusCode) {
        OpcUaValueUpdate update;
        update.nodeId = QStringLiteral("ns=1;s=A");
        update.statusCode = statusCode;
        model.updateValue(update);
        return model.data(model.index(0, 0), DataAccessModel::StatusSeverityRole).toInt();
    };

    QCOMPARE(severityFor(QStringLiteral("Good")), int(DataAccessModel::StatusGood));
    QCOMPARE(severityFor(QStringLiteral("GoodClamped")), int(DataAccessModel::StatusGood));
    QCOMPARE(severityFor(QStringLiteral("UncertainLastUsableValue")),
             int(DataAccessModel::StatusUncertain));
    QCOMPARE(severityFor(QStringLiteral("BadNodeIdUnknown")), int(DataAccessModel::StatusBad));

    // No reported status is not an error; it means nothing has arrived yet.
    QCOMPARE(severityFor(QString()), int(DataAccessModel::StatusUnknown));
}

/*!
 * \brief Verifies that the sampling interval is stored, shown, and persisted.
 */
void DataAccessModelTest::samplingIntervalIsStoredAndShown()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    const QModelIndex intervalIndex = model.index(0, DataAccessModel::IntervalColumn);

    // An unset interval follows the service default and must not read as 0 ms.
    QCOMPARE(model.samplingIntervalAt(0), 0);
    QCOMPARE(model.data(intervalIndex, Qt::DisplayRole).toString(),
             QString::fromUtf8("\xE2\x80\x94"));

    QSignalSpy changedSpy(&model, &DataAccessModel::dataChanged);
    QVERIFY(model.setSamplingIntervalAt(0, 500));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.samplingIntervalAt(0), 500);
    QCOMPARE(model.data(intervalIndex, Qt::DisplayRole).toString(), QStringLiteral("500"));

    // Writing the same value again changes nothing, so no re-subscribe is asked for.
    QVERIFY(!model.setSamplingIntervalAt(0, 500));

    // A non-positive interval restores the default.
    QVERIFY(model.setSamplingIntervalAt(0, 0));
    QCOMPARE(model.samplingIntervalAt(0), 0);

    QVERIFY(model.setSamplingIntervalAt(0, 250));
    QCOMPARE(model.records().at(0).samplingIntervalMs, 250);

    // Out-of-range rows are ignored rather than crashing.
    QVERIFY(!model.setSamplingIntervalAt(7, 100));
    QCOMPARE(model.samplingIntervalAt(7), 0);
}

/*!
 * \brief Verifies that numeric columns expose a numeric sort value.
 */
void DataAccessModelTest::numericColumnsSortNumerically()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));
    model.setSamplingIntervalAt(0, 500);

    OpcUaValueUpdate update;
    update.nodeId = QStringLiteral("ns=1;s=A");
    update.value = QStringLiteral("9.5");
    model.updateValue(update);

    const QVariant valueSort =
        model.data(model.index(0, DataAccessModel::ValueColumn),
                   DataAccessModel::SortValueRole);
    QCOMPARE(valueSort.toDouble(), 9.5);
    QCOMPARE(valueSort.typeId(), int(QMetaType::Double));

    QCOMPARE(model.data(model.index(0, DataAccessModel::RowNumberColumn),
                        DataAccessModel::SortValueRole).toInt(), 1);
    QCOMPARE(model.data(model.index(0, DataAccessModel::IntervalColumn),
                        DataAccessModel::SortValueRole).toInt(), 500);

    // A value that is not a number keeps its text so the column still sorts.
    update.value = QStringLiteral("RUNNING");
    model.updateValue(update);
    QCOMPARE(model.data(model.index(0, DataAccessModel::ValueColumn),
                        DataAccessModel::SortValueRole).toString(),
             QStringLiteral("RUNNING"));
}

/*!
 * \brief Verifies that writability stays unknown until the server reports it.
 */
void DataAccessModelTest::writabilityStartsUnknown()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));

    // A row restored from a project has never been browsed, so nothing is known
    // about it yet. Claiming it is read-only would block a legitimate write.
    QCOMPARE(model.accessLevelAt(0), int(OpcUaAccessLevel::Unknown));
    QCOMPARE(model.writabilityAt(0), int(DataAccessModel::WritabilityUnknown));
    QCOMPARE(model.data(model.index(0, 0), DataAccessModel::WritabilityRole).toInt(),
             int(DataAccessModel::WritabilityUnknown));

    // Out-of-range rows answer without crashing.
    QCOMPARE(model.accessLevelAt(9), int(OpcUaAccessLevel::Unknown));
    QCOMPARE(model.writabilityAt(9), int(DataAccessModel::WritabilityUnknown));
}

/*!
 * \brief Verifies that the access level decides whether a row can be written.
 */
void DataAccessModelTest::accessLevelDecidesWritability()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=B")));

    QSignalSpy changedSpy(&model, &DataAccessModel::dataChanged);

    QVERIFY(model.setAccessLevelForNode(QStringLiteral("ns=1;s=A"),
                                        OpcUaAccessLevel::CurrentRead
                                            | OpcUaAccessLevel::CurrentWrite));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.writabilityAt(0), int(DataAccessModel::WritabilityWritable));

    // CurrentRead alone means the server will reject a write.
    QVERIFY(model.setAccessLevelForNode(QStringLiteral("ns=1;s=B"),
                                        OpcUaAccessLevel::CurrentRead));
    QCOMPARE(model.writabilityAt(1), int(DataAccessModel::WritabilityReadOnly));

    // Repeating the same access level changes nothing.
    QVERIFY(!model.setAccessLevelForNode(QStringLiteral("ns=1;s=B"),
                                         OpcUaAccessLevel::CurrentRead));

    // An unknown node id is ignored rather than applied to the wrong row.
    QVERIFY(!model.setAccessLevelForNode(QStringLiteral("ns=1;s=Missing"),
                                         OpcUaAccessLevel::CurrentWrite));
    QCOMPARE(model.writabilityAt(0), int(DataAccessModel::WritabilityWritable));

    // The editor also needs the type and the name of the row it edits.
    QCOMPARE(model.dataTypeAt(0), QStringLiteral("String"));
    QCOMPARE(model.displayNameAt(0), QStringLiteral("ns=1;s=A"));
    QVERIFY(model.dataTypeAt(9).isEmpty());
    QVERIFY(model.displayNameAt(9).isEmpty());
}

QTEST_MAIN(DataAccessModelTest)

#include "tst_dataaccessmodel.moc"
