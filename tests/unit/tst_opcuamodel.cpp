#include <QSignalSpy>
#include <QtTest>

#include "models/opcuamodel.h"

class OpcUaModelTest : public QObject
{
    Q_OBJECT

private slots:
    void initialStateIsEmpty();
    void connectionRequestsRootFolderBrowse();
    void successfulSnapshotPopulatesRows();
    void successfulEmptySnapshotMarksNodeAsLeaf();
    void browseFailurePreservesRowsAndAllowsRetry();
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
    QCOMPARE(fetchSpy.takeFirst().constFirst().toString(), QStringLiteral("ns=0;i=84"));
}

void OpcUaModelTest::successfulSnapshotPopulatesRows()
{
    OpcUaModel model;
    model.setConnectionActive(true);

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

    model.applyChildrenSnapshot(QStringLiteral("ns=0;i=84"), children, true);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), OpcUaModel::NodeIdRole).toString(),
             QStringLiteral("ns=2;s=Machine"));
    QCOMPARE(model.data(model.index(1, 0), OpcUaModel::NodeClassNameRole).toString(),
             QStringLiteral("Variable"));
}

void OpcUaModelTest::successfulEmptySnapshotMarksNodeAsLeaf()
{
    OpcUaModel model;
    model.setConnectionActive(true);

    QList<OpcUaNodeData> children;
    children.push_back({QStringLiteral("ns=2;s=Machine"),
                        QStringLiteral("Machine"),
                        QStringLiteral("Machine"),
                        int(QOpcUa::NodeClass::Object),
                        true});
    model.applyChildrenSnapshot(QStringLiteral("ns=0;i=84"), children, true);

    const QModelIndex machineIndex = model.index(0, 0);
    QVERIFY(model.hasChildren(machineIndex));

    model.applyChildrenSnapshot(QStringLiteral("ns=2;s=Machine"), {}, true);

    QVERIFY(!model.hasChildren(machineIndex));
    QVERIFY(!model.canFetchMore(machineIndex));
}

void OpcUaModelTest::browseFailurePreservesRowsAndAllowsRetry()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);
    QTRY_COMPARE(fetchSpy.count(), 1);
    fetchSpy.clear();

    QList<OpcUaNodeData> children;
    children.push_back({QStringLiteral("ns=2;s=Machine"),
                        QStringLiteral("Machine"),
                        QStringLiteral("Machine"),
                        int(QOpcUa::NodeClass::Object),
                        true});
    model.applyChildrenSnapshot(QStringLiteral("ns=0;i=84"), children, true);
    QCOMPARE(model.rowCount(), 1);

    const QModelIndex machineIndex = model.index(0, 0);
    model.fetchMore(machineIndex);
    QCOMPARE(fetchSpy.count(), 1);
    fetchSpy.clear();

    model.applyChildrenSnapshot(QStringLiteral("ns=2;s=Machine"), {}, false);

    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.canFetchMore(machineIndex));
    model.fetchMore(machineIndex);
    QCOMPARE(fetchSpy.count(), 1);
}

void OpcUaModelTest::disconnectClearsTree()
{
    OpcUaModel model;
    model.setConnectionActive(true);
    model.applyChildrenSnapshot(
        QStringLiteral("ns=0;i=84"),
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
