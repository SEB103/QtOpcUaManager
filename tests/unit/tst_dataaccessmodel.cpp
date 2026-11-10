#include <QSignalSpy>
#include <QtTest>

#include "core/opcuaaccesslevel.h"
#include "models/dataaccessmodel.h"
#include "models/valuehighlight.h"

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

/*!
 * \internal
 * \brief Applies a value of \a kind with \a text and \a elements to node \a nodeId.
 */
static void applyValue(DataAccessModel &model, const QString &nodeId, const QString &text,
                       OpcUaValueKind kind, const QStringList &elements = {})
{
    OpcUaValueUpdate update;
    update.nodeId = nodeId;
    update.value = text;
    update.valueKind = kind;
    update.arrayElements = elements;
    model.updateValue(update);
}

/*!
 * \internal
 * \brief Returns the value markup of \a row in \a model.
 */
static QString markupAt(const DataAccessModel &model, int row)
{
    return model.data(model.index(row, DataAccessModel::ValueColumn),
                      DataAccessModel::ValueMarkupRole).toString();
}

/*!
 * \internal
 * \brief Returns \a text wrapped in a font tag of the \a token color.
 */
static QString fontRun(const QString &text, ValueHighlightToken token, bool dark)
{
    return QStringLiteral("<font color=\"%1\">%2</font>")
        .arg(valueHighlightColorName(token, dark), text);
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

    /*! Verifies currentValueForNode() returns the live value or empty. */
    void currentValueForNodeReturnsMonitoredValue();

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

    /*! Verifies that the value markup colors scalars by kind in both palettes. */
    void valueMarkupColorsScalarsByKind();

    /*! Verifies that the value markup escapes text and keeps its whitespace. */
    void valueMarkupEscapesText();

    /*! Verifies that the value markup colors array elements and separators. */
    void valueMarkupColorsArrayElements();

    /*! Verifies that switching the palette refreshes only the value markup. */
    void darkThemeRefreshesValueMarkup();

    /*! Verifies that colliding display names are shown as structured names. */
    void displayColumnShowsStructuredNames();

    /*! Verifies that adding a colliding row renames the existing row. */
    void addingACollisionRenamesTheExistingRow();

    /*! Verifies that removing a colliding row restores the plain name. */
    void removingACollisionRestoresThePlainName();

    /*! Verifies that replacing all rows computes structured names. */
    void setRecordsComputesNames();

    /*! Verifies that a bulk add of many rows sharing one name stays fast. */
    void bulkAddOfCollidingRowsStaysFast();
};

/*!
 * \internal
 * \brief Builds a record for \a path whose display name is the last path segment.
 */
static MonitoredNodeRecord namedRecord(const QString &path)
{
    MonitoredNodeRecord record = makeRecord(QStringLiteral("ns=1;s=") + path);
    record.nodePath = path;
    record.displayName = path.section(QLatin1Char('/'), -1);
    return record;
}

/*!
 * \internal
 * \brief Returns the text the name column shows for \a row of \a model.
 */
static QString nameCell(const DataAccessModel &model, int row)
{
    return model.data(model.index(row, DataAccessModel::DisplayNameColumn), Qt::DisplayRole)
        .toString();
}

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
 * \brief Verifies currentValueForNode() returns the live value or empty.
 *
 * This is the seam Clone Server uses to carry a client's current value into a
 * cloned variable, so it must return the live text for a monitored node and an
 * empty string when the node is unknown or has no value yet.
 */
void DataAccessModelTest::currentValueForNodeReturnsMonitoredValue()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=B")));

    OpcUaValueUpdate update;
    update.nodeId = QStringLiteral("ns=1;s=A");
    update.value = QStringLiteral("42");
    model.updateValue(update);

    QCOMPARE(model.currentValueForNode(QStringLiteral("ns=1;s=A")), QStringLiteral("42"));
    // A monitored node that has not received a value yet returns empty.
    QVERIFY(model.currentValueForNode(QStringLiteral("ns=1;s=B")).isEmpty());
    // An unmonitored node id returns empty.
    QVERIFY(model.currentValueForNode(QStringLiteral("ns=1;s=Z")).isEmpty());
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

/*!
 * \brief Verifies that the value markup colors scalars by kind in both palettes.
 *
 * The colors are the ones the structured Value panel uses for JSON: numbers in
 * the number color, booleans as keywords, everything else as strings. A value
 * of the Empty kind carries no color so the view's base color applies.
 */
void DataAccessModelTest::valueMarkupColorsScalarsByKind()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=N")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=B")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=S")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=E")));

    // A row without a value has no markup at all.
    QVERIFY(markupAt(model, 0).isEmpty());

    applyValue(model, QStringLiteral("ns=1;s=N"), QStringLiteral("42"), OpcUaValueKind::Number);
    applyValue(model, QStringLiteral("ns=1;s=B"), QStringLiteral("true"),
               OpcUaValueKind::Boolean);
    applyValue(model, QStringLiteral("ns=1;s=S"), QStringLiteral("RUNNING"),
               OpcUaValueKind::Text);
    applyValue(model, QStringLiteral("ns=1;s=E"), QStringLiteral("raw"), OpcUaValueKind::Empty);

    QVERIFY(model.darkTheme());
    QCOMPARE(markupAt(model, 0), fontRun(QStringLiteral("42"), ValueHighlightToken::Number, true));
    QCOMPARE(markupAt(model, 1),
             fontRun(QStringLiteral("true"), ValueHighlightToken::Keyword, true));
    QCOMPARE(markupAt(model, 2),
             fontRun(QStringLiteral("RUNNING"), ValueHighlightToken::String, true));
    QCOMPARE(markupAt(model, 3), QStringLiteral("raw"));

    model.setDarkTheme(false);
    QCOMPARE(markupAt(model, 0), QStringLiteral("<font color=\"#098658\">42</font>"));

    // The plain display text is untouched by the markup.
    QCOMPARE(model.data(model.index(0, DataAccessModel::ValueColumn), Qt::DisplayRole)
                 .toString(),
             QStringLiteral("42"));

    // Clearing the values clears the markup as well.
    model.clearValues();
    QVERIFY(markupAt(model, 0).isEmpty());
}

/*!
 * \brief Verifies that the value markup escapes text and keeps its whitespace.
 *
 * A string value is arbitrary server data, so it must not be interpreted as
 * markup, and StyledText would otherwise collapse its spaces.
 */
void DataAccessModelTest::valueMarkupEscapesText()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=S")));

    applyValue(model, QStringLiteral("ns=1;s=S"), QStringLiteral("<b>a&b</b>  \"x\"\ny"),
               OpcUaValueKind::Text);

    QCOMPARE(markupAt(model, 0),
             fontRun(QStringLiteral("&lt;b&gt;a&amp;b&lt;/b&gt;&nbsp;&nbsp;&quot;x&quot;<br>y"),
                     ValueHighlightToken::String, true));
}

/*!
 * \brief Verifies that the value markup colors array elements and separators.
 *
 * Each element is colored by the array's kind and the separators use the
 * punctuation color, as in the JSON panel. Long arrays are cut after
 * valueHighlightMaxArrayElements elements and end in an ellipsis.
 */
void DataAccessModelTest::valueMarkupColorsArrayElements()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=L")));

    applyValue(model, QStringLiteral("ns=1;s=A"), QStringLiteral("1, 2"), OpcUaValueKind::Number,
               {QStringLiteral("1"), QStringLiteral("2")});

    const QString separator =
        fontRun(QStringLiteral(",&nbsp;"), ValueHighlightToken::Punctuation, true);
    QCOMPARE(markupAt(model, 0),
             fontRun(QStringLiteral("1"), ValueHighlightToken::Number, true) + separator
                 + fontRun(QStringLiteral("2"), ValueHighlightToken::Number, true));

    QStringList elements;
    for (int i = 0; i < valueHighlightMaxArrayElements + 5; ++i)
        elements.append(QString::number(i));
    applyValue(model, QStringLiteral("ns=1;s=L"), elements.join(QStringLiteral(", ")),
               OpcUaValueKind::Number, elements);

    const QString markup = markupAt(model, 1);
    const QString numberColor = valueHighlightColorName(ValueHighlightToken::Number, true);
    QCOMPARE(markup.count(numberColor), valueHighlightMaxArrayElements);
    QVERIFY(markup.endsWith(
        fontRun(QStringLiteral(",&nbsp;…"), ValueHighlightToken::Punctuation, true)));
}

/*!
 * \brief Verifies that switching the palette refreshes only the value markup.
 */
void DataAccessModelTest::darkThemeRefreshesValueMarkup()
{
    DataAccessModel model;
    model.addRow(makeRecord(QStringLiteral("ns=1;s=A")));
    model.addRow(makeRecord(QStringLiteral("ns=1;s=B")));

    QSignalSpy themeSpy(&model, &DataAccessModel::darkThemeChanged);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    // Setting the current palette again changes nothing.
    model.setDarkTheme(true);
    QCOMPARE(themeSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);

    model.setDarkTheme(false);
    QCOMPARE(themeSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);

    const QList<QVariant> arguments = changedSpy.takeFirst();
    const QModelIndex topLeft = arguments.at(0).value<QModelIndex>();
    const QModelIndex bottomRight = arguments.at(1).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 0);
    QCOMPARE(bottomRight.row(), 1);
    QCOMPARE(topLeft.column(), int(DataAccessModel::ValueColumn));
    QCOMPARE(bottomRight.column(), int(DataAccessModel::ValueColumn));
    QCOMPARE(arguments.at(2).value<QList<int>>(), QList<int>{DataAccessModel::ValueMarkupRole});

    // The QML delegate reads the role by name.
    QCOMPARE(model.roleNames().value(DataAccessModel::ValueMarkupRole),
             QByteArray("valueMarkup"));
}

/*!
 * \brief Verifies that colliding display names are shown as structured names.
 *
 * The name column and the prefix/leaf roles carry the structured name, while
 * displayNameAt() keeps the server's own DisplayName.
 */
void DataAccessModelTest::displayColumnShowsStructuredNames()
{
    DataAccessModel model;
    model.addRow(namedRecord(QStringLiteral("Objects/s1/value")));
    model.addRow(namedRecord(QStringLiteral("Objects/s2/value")));

    QCOMPARE(nameCell(model, 0), QStringLiteral("s1.value"));
    QCOMPARE(model.structuredNameAt(1), QStringLiteral("s2.value"));
    QCOMPARE(model.displayNameAt(0), QStringLiteral("value"));

    const QModelIndex cell = model.index(0, DataAccessModel::DisplayNameColumn);
    QCOMPARE(model.data(cell, DataAccessModel::NamePrefixRole).toString(), QStringLiteral("s1."));
    QCOMPARE(model.data(cell, DataAccessModel::NameLeafRole).toString(), QStringLiteral("value"));
    QCOMPARE(model.data(cell, DataAccessModel::NameSuffixRole).toString(), QString());
    QCOMPARE(model.roleNames().value(DataAccessModel::NamePrefixRole), QByteArray("namePrefix"));
}

/*!
 * \brief Verifies that adding a colliding row renames the existing row.
 */
void DataAccessModelTest::addingACollisionRenamesTheExistingRow()
{
    DataAccessModel model;
    model.addRow(namedRecord(QStringLiteral("Objects/s1/value")));
    QCOMPARE(nameCell(model, 0), QStringLiteral("value"));

    QSignalSpy spy(&model, &DataAccessModel::dataChanged);
    model.addRow(namedRecord(QStringLiteral("Objects/s2/value")));

    QCOMPARE(nameCell(model, 0), QStringLiteral("s1.value"));

    bool nameRefreshed = false;
    for (const QList<QVariant> &arguments : std::as_const(spy)) {
        const QModelIndex topLeft = arguments.at(0).toModelIndex();
        const QModelIndex bottomRight = arguments.at(1).toModelIndex();
        if (topLeft.row() <= 0 && bottomRight.row() >= 0
            && topLeft.column() <= DataAccessModel::DisplayNameColumn
            && bottomRight.column() >= DataAccessModel::DisplayNameColumn) {
            nameRefreshed = true;
        }
    }
    QVERIFY(nameRefreshed);
}

/*!
 * \brief Verifies that removing a colliding row restores the plain name.
 */
void DataAccessModelTest::removingACollisionRestoresThePlainName()
{
    DataAccessModel model;
    model.addRow(namedRecord(QStringLiteral("Objects/s1/value")));
    model.addRow(namedRecord(QStringLiteral("Objects/s2/value")));

    model.removeAt(1);

    QCOMPARE(nameCell(model, 0), QStringLiteral("value"));
}

/*!
 * \brief Verifies that replacing all rows computes structured names.
 */
void DataAccessModelTest::setRecordsComputesNames()
{
    DataAccessModel model;
    model.setRecords({namedRecord(QStringLiteral("Objects/s1/value")),
                      namedRecord(QStringLiteral("Objects/s2/value"))});

    QCOMPARE(nameCell(model, 0), QStringLiteral("s1.value"));
    QCOMPARE(nameCell(model, 1), QStringLiteral("s2.value"));
}

/*!
 * \brief Verifies that a bulk add of many rows sharing one name stays fast.
 *
 * Ticking an array of structures adds one row per element member, all named
 * alike; names are recomputed after every add, so that recompute must stay
 * linear in the number of colliding rows. The bound is generous for a Debug
 * build; comparing the rows pairwise on every add takes over ten times longer.
 */
void DataAccessModelTest::bulkAddOfCollidingRowsStaysFast()
{
    DataAccessModel model;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 1000; ++i) {
        model.addRow(namedRecord(QStringLiteral("Application/V_HMI/astItems[%1]/stRefOffset/value")
                                     .arg(i)));
    }
    const qint64 elapsedMs = timer.elapsed();

    QCOMPARE(nameCell(model, 999), QStringLiteral("[999].stRefOffset.value"));
    QVERIFY2(elapsedMs < 3000, qPrintable(QStringLiteral("took %1 ms").arg(elapsedMs)));
}

QTEST_MAIN(DataAccessModelTest)

#include "tst_dataaccessmodel.moc"
