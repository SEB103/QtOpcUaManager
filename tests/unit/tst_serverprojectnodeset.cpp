// Unit test for NodeSet2 export/import round-trip at the model level.

#include <QTemporaryDir>
#include <QtTest>

#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectnodeset.h"
#include "serverproject/serverprojectvalidator.h"

using namespace ServerProject;

/*! Verifies the address space survives an export/import round-trip. */
class ServerProjectNodeSetTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Exported NodeSet2 re-imports to an equivalent, valid address space. */
    void roundTrip();

private:
    static ProjectData makeProject();
};

ProjectData ServerProjectNodeSetTest::makeProject()
{
    ProjectData data;
    data.displayName = QStringLiteral("NodeSet");
    data.namespaces.append({QStringLiteral("urn:opcuamanager:nodeset")});

    EnumType color;
    color.name = QStringLiteral("Color");
    color.nodeId = QStringLiteral("ns=1;s=Enum.Color");
    color.entries = {{0, QStringLiteral("Red")}, {1, QStringLiteral("Green")}};
    data.enumTypes.append(color);

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=Plant");
    folder.browseName = QStringLiteral("Plant");
    folder.displayName = QStringLiteral("Plant");
    data.nodes.append(folder);

    Node scalar;
    scalar.kind = NodeKind::Variable;
    scalar.nodeId = QStringLiteral("ns=1;s=Plant.Count");
    scalar.parentNodeId = folder.nodeId;
    scalar.browseName = QStringLiteral("Count");
    scalar.dataType = QStringLiteral("Int32");
    scalar.valueRank = -1;
    scalar.initialValue = 42;
    data.nodes.append(scalar);

    Node array;
    array.kind = NodeKind::Variable;
    array.nodeId = QStringLiteral("ns=1;s=Plant.Levels");
    array.parentNodeId = folder.nodeId;
    array.browseName = QStringLiteral("Levels");
    array.dataType = QStringLiteral("Double");
    array.valueRank = 1;
    array.initialValue = QVariantList{1.5, 2.5};
    data.nodes.append(array);

    Node status;
    status.kind = NodeKind::Variable;
    status.nodeId = QStringLiteral("ns=1;s=Plant.Status");
    status.parentNodeId = folder.nodeId;
    status.browseName = QStringLiteral("Status");
    status.enumTypeId = color.nodeId;
    status.initialValue = 1;
    data.nodes.append(status);

    return data;
}

void ServerProjectNodeSetTest::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("model.xml"));

    const ProjectData original = makeProject();
    const NodeSet::Result exported = NodeSet::exportToFile(path, original);
    QVERIFY2(exported.ok, qPrintable(exported.errorString));

    const NodeSet::ImportResult imported = NodeSet::importFromFile(path);
    QVERIFY2(imported.ok, qPrintable(imported.errorString));

    // Namespaces and enum types survive.
    QCOMPARE(imported.namespaces.size(), 1);
    QCOMPARE(imported.namespaces.first().uri, QStringLiteral("urn:opcuamanager:nodeset"));
    QCOMPARE(imported.enumTypes.size(), 1);
    QCOMPARE(imported.enumTypes.first().name, QStringLiteral("Color"));
    QCOMPARE(imported.enumTypes.first().entries.size(), 2);

    // Index the imported nodes by id.
    QHash<QString, Node> byId;
    for (const Node &node : imported.nodes)
        byId.insert(node.nodeId, node);
    QCOMPARE(byId.size(), 4);

    const Node folder = byId.value(QStringLiteral("ns=1;s=Plant"));
    QCOMPARE(folder.kind, NodeKind::Folder);
    QVERIFY(folder.parentNodeId.isEmpty());

    const Node scalar = byId.value(QStringLiteral("ns=1;s=Plant.Count"));
    QCOMPARE(scalar.kind, NodeKind::Variable);
    QCOMPARE(scalar.parentNodeId, QStringLiteral("ns=1;s=Plant"));
    QCOMPARE(scalar.dataType, QStringLiteral("Int32"));
    QCOMPARE(scalar.initialValue.toInt(), 42);

    const Node array = byId.value(QStringLiteral("ns=1;s=Plant.Levels"));
    QCOMPARE(array.valueRank, 1);
    QCOMPARE(array.dataType, QStringLiteral("Double"));
    QCOMPARE(array.initialValue.toList().size(), 2);
    QCOMPARE(array.initialValue.toList().at(1).toDouble(), 2.5);

    const Node status = byId.value(QStringLiteral("ns=1;s=Plant.Status"));
    QCOMPARE(status.enumTypeId, QStringLiteral("ns=1;s=Enum.Color"));

    // The imported address space is valid and therefore runnable.
    ProjectData rebuilt;
    rebuilt.namespaces = imported.namespaces;
    rebuilt.enumTypes = imported.enumTypes;
    rebuilt.nodes = imported.nodes;
    const Validator::Result validation = Validator::validate(rebuilt);
    QVERIFY2(validation.ok, qPrintable(validation.errors.join(QLatin1String("; "))));
}

QTEST_GUILESS_MAIN(ServerProjectNodeSetTest)

#include "tst_serverprojectnodeset.moc"
