#include <QSignalSpy>
#include <QtTest>

#include "models/opcuamodel.h"

namespace {

/*! \internal Captures one lazy browse request emitted by OpcUaModel. */
struct FetchRequest
{
    /*! \internal Node id requested from the model. */
    QString parentNodeId;

    /*! \internal Request id used to correlate a later service result. */
    quint64 requestId {0};
};

/*! \internal Removes and returns the oldest fetch request from \a spy. */
FetchRequest takeFetchRequest(QSignalSpy &spy)
{
    const QList<QVariant> arguments = spy.takeFirst();
    return {arguments.at(0).toString(), arguments.at(1).toULongLong()};
}

} // namespace

/*! Verifies OpcUaModel lazy browsing and snapshot application behavior. */
class OpcUaModelTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a disconnected model exposes no rows and cannot fetch. */
    void initialStateIsEmpty();

    /*! Verifies that activating a connection requests the RootFolder browse. */
    void connectionRequestsRootFolderBrowse();

    /*! Verifies that a successful root snapshot creates model rows. */
    void successfulSnapshotPopulatesRows();

    /*! Verifies that an empty successful snapshot marks a node as fetched. */
    void successfulEmptySnapshotMarksNodeAsLeaf();

    /*! Verifies that duplicate node ids are resolved by the original requested index. */
    void browseResultAppliesToRequestedDuplicateNode();

    /*! Verifies that a failed browse keeps rows and allows retry. */
    void browseFailurePreservesRowsAndAllowsRetry();

    /*! Verifies that disconnecting clears the snapshot tree. */
    void disconnectClearsTree();
};

void OpcUaModelTest::initialStateIsEmpty()
{
    OpcUaModel model;

    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.canFetchMore(QModelIndex()));
}

void OpcUaModelTest::connectionRequestsRootFolderBrowse()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);

    model.setConnectionActive(true);

    QTRY_COMPARE(fetchSpy.count(), 1);
    const FetchRequest request = takeFetchRequest(fetchSpy);
    QCOMPARE(request.parentNodeId, QStringLiteral("ns=0;i=84"));
    QVERIFY(request.requestId > 0);
}

void OpcUaModelTest::successfulSnapshotPopulatesRows()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);
    QTRY_COMPARE(fetchSpy.count(), 1);
    const FetchRequest rootRequest = takeFetchRequest(fetchSpy);

    QList<OpcUaNodeData> children;
    children.push_back({QStringLiteral("ns=2;s=Machine"),
                        QStringLiteral("Machine"),
                        QStringLiteral("Machine"),
                        int(QOpcUa::NodeClass::Object),
                        true});
    children.push_back({QStringLiteral("ns=2;s=Temperature"),
                        QStringLiteral("Temperature"),
                        QStringLiteral("Temperature"),
                        int(QOpcUa::NodeClass::Variable),
                        false});

    model.applyChildrenSnapshot(rootRequest.parentNodeId, rootRequest.requestId, children, true);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), OpcUaModel::NodeIdRole).toString(),
             QStringLiteral("ns=2;s=Machine"));
    QCOMPARE(model.data(model.index(1, 0), OpcUaModel::NodeClassNameRole).toString(),
             QStringLiteral("Variable"));
}

void OpcUaModelTest::successfulEmptySnapshotMarksNodeAsLeaf()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);
    QTRY_COMPARE(fetchSpy.count(), 1);
    const FetchRequest rootRequest = takeFetchRequest(fetchSpy);

    QList<OpcUaNodeData> children;
    children.push_back({QStringLiteral("ns=2;s=Machine"),
                        QStringLiteral("Machine"),
                        QStringLiteral("Machine"),
                        int(QOpcUa::NodeClass::Object),
                        true});
    model.applyChildrenSnapshot(rootRequest.parentNodeId, rootRequest.requestId, children, true);

    const QModelIndex machineIndex = model.index(0, 0);
    QVERIFY(model.hasChildren(machineIndex));
    model.fetchMore(machineIndex);
    QCOMPARE(fetchSpy.count(), 1);
    const FetchRequest machineRequest = takeFetchRequest(fetchSpy);

    model.applyChildrenSnapshot(machineRequest.parentNodeId, machineRequest.requestId, {}, true);

    QVERIFY(!model.hasChildren(machineIndex));
    QVERIFY(!model.canFetchMore(machineIndex));
}

void OpcUaModelTest::browseResultAppliesToRequestedDuplicateNode()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);
    QTRY_COMPARE(fetchSpy.count(), 1);
    const FetchRequest rootRequest = takeFetchRequest(fetchSpy);

    QList<OpcUaNodeData> duplicateChildren;
    duplicateChildren.push_back({QStringLiteral("ns=2;s=Shared"),
                                 QStringLiteral("SharedA"),
                                 QStringLiteral("Shared A"),
                                 int(QOpcUa::NodeClass::Object),
                                 true});
    duplicateChildren.push_back({QStringLiteral("ns=2;s=Shared"),
                                 QStringLiteral("SharedB"),
                                 QStringLiteral("Shared B"),
                                 int(QOpcUa::NodeClass::Object),
                                 true});
    model.applyChildrenSnapshot(rootRequest.parentNodeId,
                                rootRequest.requestId,
                                duplicateChildren,
                                true);

    const QModelIndex firstSharedIndex = model.index(0, 0);
    const QModelIndex secondSharedIndex = model.index(1, 0);
    model.fetchMore(secondSharedIndex);
    QCOMPARE(fetchSpy.count(), 1);
    const FetchRequest secondRequest = takeFetchRequest(fetchSpy);

    QList<OpcUaNodeData> secondChildren;
    secondChildren.push_back({QStringLiteral("ns=2;s=Shared.Child"),
                              QStringLiteral("Child"),
                              QStringLiteral("Child"),
                              int(QOpcUa::NodeClass::Variable),
                              false});
    model.applyChildrenSnapshot(secondRequest.parentNodeId,
                                secondRequest.requestId,
                                secondChildren,
                                true);

    QCOMPARE(model.rowCount(firstSharedIndex), 0);
    QCOMPARE(model.rowCount(secondSharedIndex), 1);
    QCOMPARE(model.data(model.index(0, 0, secondSharedIndex), OpcUaModel::DisplayNameRole).toString(),
             QStringLiteral("Child"));
}

void OpcUaModelTest::browseFailurePreservesRowsAndAllowsRetry()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);
    QTRY_COMPARE(fetchSpy.count(), 1);
    const FetchRequest rootRequest = takeFetchRequest(fetchSpy);

    QList<OpcUaNodeData> children;
    children.push_back({QStringLiteral("ns=2;s=Machine"),
                        QStringLiteral("Machine"),
                        QStringLiteral("Machine"),
                        int(QOpcUa::NodeClass::Object),
                        true});
    model.applyChildrenSnapshot(rootRequest.parentNodeId, rootRequest.requestId, children, true);
    QCOMPARE(model.rowCount(), 1);

    const QModelIndex machineIndex = model.index(0, 0);
    model.fetchMore(machineIndex);
    QCOMPARE(fetchSpy.count(), 1);
    const FetchRequest machineRequest = takeFetchRequest(fetchSpy);

    model.applyChildrenSnapshot(machineRequest.parentNodeId, machineRequest.requestId, {}, false);

    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.canFetchMore(machineIndex));
    model.fetchMore(machineIndex);
    QCOMPARE(fetchSpy.count(), 1);
}

void OpcUaModelTest::disconnectClearsTree()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);
    QTRY_COMPARE(fetchSpy.count(), 1);
    const FetchRequest rootRequest = takeFetchRequest(fetchSpy);
    model.applyChildrenSnapshot(
        rootRequest.parentNodeId,
        rootRequest.requestId,
        {{QStringLiteral("ns=2;s=Machine"),
          QStringLiteral("Machine"),
          QStringLiteral("Machine"),
          int(QOpcUa::NodeClass::Object),
          true}},
        true);
    QCOMPARE(model.rowCount(), 1);

    model.setConnectionActive(false);

    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.canFetchMore(QModelIndex()));
}

QTEST_GUILESS_MAIN(OpcUaModelTest)

#include "tst_opcuamodel.moc"
