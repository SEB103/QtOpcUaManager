#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core/opcuanodedata.h"
#include "models/dataaccessmodel.h"
#include "models/opcuamodel.h"
#include "models/trendmodel.h"
#include "project/projectdata.h"
#include "qmlapi/opcuamanager.h"

/*!
 * \internal
 * \brief Builds a project holding one monitored node.
 */
static ProjectData makeProjectWithNode(const QString &nodeId)
{
    MonitoredNodeRecord record;
    record.server = QStringLiteral("srv");
    record.nodeId = nodeId;
    record.nodePath = QStringLiteral("Objects/") + nodeId;
    record.displayName = nodeId;
    record.dataType = QStringLiteral("DINT");

    ProjectData data;
    data.monitoredNodes.append(record);
    return data;
}

/*!
 * \internal
 * \brief Builds a browsed variable node named \a name under a mock namespace.
 */
static OpcUaNodeData makeVariable(const QString &name)
{
    OpcUaNodeData node;
    node.nodeId = QStringLiteral("ns=2;s=") + name;
    node.browseName = name;
    node.displayName = name;
    node.nodeClass = int(QOpcUa::NodeClass::Variable);
    return node;
}

/*!
 * \internal
 * \brief Fills \a model with one object holding \a childCount variables.
 * \return The index of the object whose children were browsed.
 *
 * The tree model only materializes nodes a browse delivered, so a bulk-add test
 * has to walk the same lazy path the address space pane walks.
 */
static QModelIndex browseObjectWithVariables(OpcUaModel *model, int childCount)
{
    QSignalSpy fetchSpy(model, &OpcUaModel::fetchChildrenRequested);
    model->setConnectionActive(true);
    if (!fetchSpy.wait(1000) && fetchSpy.isEmpty())
        return {};

    const quint64 rootRequest = fetchSpy.takeFirst().at(1).toULongLong();

    OpcUaNodeData object;
    object.nodeId = QStringLiteral("ns=2;s=Machine");
    object.browseName = QStringLiteral("Machine");
    object.displayName = QStringLiteral("Machine");
    object.nodeClass = int(QOpcUa::NodeClass::Object);
    object.hasChildren = true;
    model->applyChildrenSnapshot(QStringLiteral("ns=0;i=84"), rootRequest, {object});

    const QModelIndex objectIndex = model->index(0, 0, QModelIndex());
    fetchSpy.clear();
    model->fetchMore(objectIndex);
    if (fetchSpy.isEmpty() && !fetchSpy.wait(1000))
        return {};

    const quint64 childRequest = fetchSpy.takeFirst().at(1).toULongLong();

    QList<OpcUaNodeData> children;
    children.reserve(childCount);
    for (int i = 0; i < childCount; ++i)
        children.append(makeVariable(QStringLiteral("Member%1").arg(i)));

    model->applyChildrenSnapshot(object.nodeId, childRequest, children);
    return objectIndex;
}

/*!
 * \internal
 * \brief Builds a value update for \a nodeId carrying \a value.
 */
static OpcUaValueUpdate makeUpdate(const QString &nodeId, const QString &value)
{
    OpcUaValueUpdate update;
    update.nodeId = nodeId;
    update.value = value;
    update.statusCode = QStringLiteral("Good");
    return update;
}

/*! Verifies how the OPC UA facade distributes incoming subscription updates. */
class OpcUaManagerTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that an update reaches both the table and the trend. */
    void updatesReachTheTableAndTheTrend();

    /*! Verifies that pausing the table does not stop the trend recording. */
    void pausingTheTableDoesNotStopTheTrend();

    /*! Verifies that values which are not numbers leave the trend untouched. */
    void nonNumericValuesAreNotPlotted();

    /*! Verifies that a bulk add refreshes the monitored-id sets only once. */
    void bulkAddRefreshesTheMonitoredSetsOnce();

    /*! Verifies that an exported CSV cell cannot become a spreadsheet formula. */
    void csvExportNeutralizesFormulaInjection();
};

/*!
 * \brief Verifies that an update reaches both the table and the trend.
 */
void OpcUaManagerTest::updatesReachTheTableAndTheTrend()
{
    OpcUaManager manager;
    const QString nodeId = QStringLiteral("ns=1;s=A");
    manager.applyProject(makeProjectWithNode(nodeId));

    QCOMPARE(manager.dataModel()->rowCount(), 1);

    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("42")));

    QCOMPARE(manager.dataModel()->valueAt(0), QStringLiteral("42"));
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 1);
}

/*!
 * \brief Verifies that pausing the table does not stop the trend recording.
 */
void OpcUaManagerTest::pausingTheTableDoesNotStopTheTrend()
{
    OpcUaManager manager;
    const QString nodeId = QStringLiteral("ns=1;s=A");
    manager.applyProject(makeProjectWithNode(nodeId));

    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("42")));
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 1);

    manager.setUpdatesPaused(true);
    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("43")));
    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("44")));

    // The table is frozen on purpose: that is what the pause is for.
    QCOMPARE(manager.dataModel()->valueAt(0), QStringLiteral("42"));

    // The trend must keep recording. Dropping these samples would leave a hole
    // that the curve spans with a straight line once the pause ends, showing
    // values the variable never had. The trend has its own pause for freezing
    // the axis, which keeps collecting.
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 3);

    manager.setUpdatesPaused(false);
    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("45")));
    QCOMPARE(manager.dataModel()->valueAt(0), QStringLiteral("45"));
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 4);
}

/*!
 * \brief Verifies that values which are not numbers leave the trend untouched.
 */
void OpcUaManagerTest::nonNumericValuesAreNotPlotted()
{
    OpcUaManager manager;
    const QString nodeId = QStringLiteral("ns=1;s=A");
    manager.applyProject(makeProjectWithNode(nodeId));

    // A string has no position on a value axis, so the node simply gets no
    // curve; the table still shows the text.
    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("RUNNING")));
    QCOMPARE(manager.dataModel()->valueAt(0), QStringLiteral("RUNNING"));
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 0);

    // Booleans are plotted, because they map onto 1 and 0.
    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("true")));
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 1);
    manager.applyMonitoredValue(makeUpdate(nodeId, QStringLiteral("false")));
    QCOMPARE(manager.trendModel()->sampleCountFor(nodeId), 2);
}

/*!
 * \brief Verifies that a bulk add refreshes the monitored-id sets only once.
 *
 * Refreshing the monitored-id sets rescans every Data View row and then walks
 * both tree models end to end, so doing it per child turns one bulk add into a
 * quadratic amount of work on the GUI thread. The refresh and the project-state
 * signal are emitted together at the end of the operation, which makes the
 * number of projectStateChanged() emissions the observable proof that the
 * refresh was hoisted out of the per-child loop.
 */
void OpcUaManagerTest::bulkAddRefreshesTheMonitoredSetsOnce()
{
    OpcUaManager manager;

    constexpr int childCount = 24;
    const QModelIndex objectIndex = browseObjectWithVariables(manager.treeModel(), childCount);
    QVERIFY(objectIndex.isValid());

    QSignalSpy projectSpy(&manager, &OpcUaManager::projectStateChanged);
    QCOMPARE(manager.monitorChildVariables(objectIndex), childCount);

    // Every child still reaches the table, and the whole batch counts as one
    // project change rather than one per child.
    QCOMPARE(manager.dataModel()->rowCount(), childCount);
    QCOMPARE(projectSpy.count(), 1);

    // A single checkbox keeps its own refresh, so its signal still arrives.
    const QModelIndex child = manager.treeModel()->index(0, 0, objectIndex);
    QVERIFY(child.isValid());
    projectSpy.clear();
    manager.setNodeMonitored(child, false);
    QCOMPARE(projectSpy.count(), 1);
    QCOMPARE(manager.dataModel()->rowCount(), childCount - 1);
}

/*!
 * \brief Verifies that an exported CSV cell cannot become a spreadsheet formula.
 *
 * The exported values come from whatever OPC UA server the user connected to, so
 * a String variable can carry text a spreadsheet would execute. Quoting is no
 * defense: a spreadsheet strips the quotes while parsing and then evaluates what
 * is left, so the decoded field itself must not open with a formula trigger.
 */
void OpcUaManagerTest::csvExportNeutralizesFormulaInjection()
{
    OpcUaManager manager;
    const QString nodeId = QStringLiteral("ns=1;s=Text");
    manager.applyProject(makeProjectWithNode(nodeId));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("export.csv"));
    const QList<int> columns {int(DataAccessModel::ValueColumn)};

    const QStringList payloads {
        QStringLiteral("=HYPERLINK(\"http://attacker.example\",\"Click\")"),
        QStringLiteral("+1+1"),
        QStringLiteral("-2+3"),
        QStringLiteral("@SUM(A1)")};

    for (const QString &payload : payloads) {
        manager.applyMonitoredValue(makeUpdate(nodeId, payload));
        QCOMPARE(manager.dataModel()->valueAt(0), payload);
        QVERIFY(manager.exportDataViewCsv(QUrl::fromLocalFile(path), {0}, columns));

        QString contents;
        {
            // QIODevice::Text would translate the CRLF endings the export writes
            // on purpose, so the file is read verbatim. The handle is closed again
            // before the next export replaces the file.
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            contents = QString::fromUtf8(file.readAll());
        }

        const QStringList lines = contents.split(QLatin1String("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);

        // Decode the field the way a spreadsheet does: drop the surrounding
        // quotes and undouble the escaped ones.
        QString field = lines.at(1);
        if (field.startsWith(QLatin1Char('"')) && field.endsWith(QLatin1Char('"'))) {
            field = field.mid(1, field.size() - 2);
            field.replace(QLatin1String("\"\""), QLatin1String("\""));
        }

        QVERIFY2(!field.startsWith(payload.at(0)),
                 qPrintable(QStringLiteral("field still opens with a formula trigger: ")
                            + field));

        // The value itself is preserved, only made inert.
        QVERIFY(field.contains(payload));
    }
}

QTEST_MAIN(OpcUaManagerTest)

#include "tst_opcuamanager.moc"
