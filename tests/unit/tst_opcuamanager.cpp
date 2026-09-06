#include <QtTest>

#include "models/dataaccessmodel.h"
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

QTEST_MAIN(OpcUaManagerTest)

#include "tst_opcuamanager.moc"
