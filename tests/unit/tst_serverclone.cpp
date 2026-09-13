// Unit test for the address-space snapshot that backs Server Studio's
// "Clone to Server Studio": OpcUaModel::snapshotUnder walks the browsed client
// tree and returns the descendants of a node with their parent links, node
// classes and variable data types.

#include <QSignalSpy>
#include <QtTest>

#include "core/opcuanodedata.h"
#include "models/opcuamodel.h"

/*! Verifies snapshotUnder captures a browsed subtree faithfully. */
class ServerCloneTest : public QObject
{
    Q_OBJECT

private slots:
    /*! A populated Objects/Test subtree is returned pre-order with parents. */
    void snapshotUnderCapturesSubtree();

private:
    /*! Pops the next (parentNodeId, requestId) fetch request from \a spy. */
    static QPair<QString, quint64> takeFetch(QSignalSpy &spy);

    /*! Applies \a children as the fetched children of \a parentNodeId on \a model. */
    static void fetch(OpcUaModel &model, QSignalSpy &spy, const QString &parentNodeId,
                      const QList<OpcUaNodeData> &children);
};

QPair<QString, quint64> ServerCloneTest::takeFetch(QSignalSpy &spy)
{
    const QList<QVariant> args = spy.takeFirst();
    return {args.at(0).toString(), args.at(1).toULongLong()};
}

void ServerCloneTest::fetch(OpcUaModel &model, QSignalSpy &spy, const QString &parentNodeId,
                            const QList<OpcUaNodeData> &children)
{
    if (!parentNodeId.isEmpty() && parentNodeId != QLatin1String("ns=0;i=84")) {
        // Trigger a browse of the parent so the model issues a fetch request.
        const QModelIndex parentIndex = model.indexForNodeId(parentNodeId);
        QVERIFY(parentIndex.isValid());
        model.fetchMore(parentIndex);
    }
    QTRY_VERIFY(!spy.isEmpty());
    const auto request = takeFetch(spy);
    QCOMPARE(request.first, parentNodeId);
    model.applyChildrenSnapshot(request.first, request.second, children, true);
}

void ServerCloneTest::snapshotUnderCapturesSubtree()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);
    model.setConnectionActive(true);

    // Root (i=84) -> Objects (i=85).
    OpcUaNodeData objects;
    objects.nodeId = QStringLiteral("ns=0;i=85");
    objects.browseName = QStringLiteral("Objects");
    objects.displayName = QStringLiteral("Objects");
    objects.nodeClass = int(QOpcUa::NodeClass::Object);
    objects.hasChildren = true;
    fetch(model, fetchSpy, QStringLiteral("ns=0;i=84"), {objects});

    // Objects -> Test folder.
    OpcUaNodeData testFolder;
    testFolder.nodeId = QStringLiteral("ns=1;s=Test");
    testFolder.browseName = QStringLiteral("Test");
    testFolder.displayName = QStringLiteral("Test");
    testFolder.nodeClass = int(QOpcUa::NodeClass::Object);
    testFolder.typeDefinitionId = QStringLiteral("ns=0;i=61"); // FolderType
    testFolder.hasChildren = true;
    fetch(model, fetchSpy, QStringLiteral("ns=0;i=85"), {testFolder});

    // Test -> IntValue variable.
    OpcUaNodeData intValue;
    intValue.nodeId = QStringLiteral("ns=1;s=Test.IntValue");
    intValue.browseName = QStringLiteral("IntValue");
    intValue.displayName = QStringLiteral("IntValue");
    intValue.nodeClass = int(QOpcUa::NodeClass::Variable);
    intValue.dataTypeId = QStringLiteral("ns=0;i=6"); // Int32
    intValue.valueRank = -1;
    fetch(model, fetchSpy, QStringLiteral("ns=1;s=Test"), {intValue});

    const QList<OpcUaModel::SnapshotNode> snapshot =
        model.snapshotUnder(QStringLiteral("ns=0;i=85"));
    QCOMPARE(snapshot.size(), 2);

    // The Test folder is a direct child of Objects, so its parent is empty.
    const OpcUaModel::SnapshotNode &folder = snapshot.at(0);
    QCOMPARE(folder.nodeId, QStringLiteral("ns=1;s=Test"));
    QVERIFY(folder.parentNodeId.isEmpty());
    QVERIFY(folder.isFolder);
    QVERIFY(!folder.isVariable);

    // The variable carries its parent, data type id and variable flag.
    const OpcUaModel::SnapshotNode &variable = snapshot.at(1);
    QCOMPARE(variable.nodeId, QStringLiteral("ns=1;s=Test.IntValue"));
    QCOMPARE(variable.parentNodeId, QStringLiteral("ns=1;s=Test"));
    QVERIFY(variable.isVariable);
    QCOMPARE(variable.dataTypeId, QStringLiteral("ns=0;i=6"));
}

QTEST_GUILESS_MAIN(ServerCloneTest)

#include "tst_serverclone.moc"
