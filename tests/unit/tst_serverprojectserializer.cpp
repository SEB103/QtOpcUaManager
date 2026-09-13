// Unit tests for the .uaserver project serializer: round-trip fidelity and
// typed error reporting, mirroring tst_projectserializer for the client format.

#include <QTemporaryDir>
#include <QtTest>

#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectserializer.h"

using namespace ServerProject;

/*! Verifies save/load round-trips and error handling for .uaserver files. */
class ServerProjectSerializerTest : public QObject
{
    Q_OBJECT

private slots:
    /*! A saved project loads back with identical fields. */
    void roundTripPreservesData();

    /*! Loading a missing file reports FileNotFound. */
    void reportsFileNotFound();

    /*! Loading malformed JSON reports InvalidJson. */
    void reportsInvalidJson();

    /*! Loading a future version reports UnsupportedVersion. */
    void reportsUnsupportedVersion();

    /*! A legacy file without acceptAllClientCerts defaults it to true. */
    void legacyFileDefaultsAcceptAllClientCerts();

private:
    /*! Builds a representative project with a folder, scalar and array variable. */
    static ProjectData makeSampleProject();
};

ProjectData ServerProjectSerializerTest::makeSampleProject()
{
    ProjectData data;
    data.displayName = QStringLiteral("Demo Server");
    data.server.applicationName = QStringLiteral("Demo");
    data.server.endpoint.port = 48410;
    data.namespaces.append({QStringLiteral("urn:opcuamanager:demo")});

    data.security.allowAnonymous = false;
    data.security.allowNone = true;
    data.security.enableSecurity = true;
    data.security.acceptAllClientCerts = false;
    data.security.users.append({QStringLiteral("admin"), QStringLiteral("secret")});

    EnumType mode;
    mode.name = QStringLiteral("Mode");
    mode.nodeId = QStringLiteral("ns=1;s=Enum.Mode");
    mode.entries = {{0, QStringLiteral("Off")}, {1, QStringLiteral("On")}};
    data.enumTypes.append(mode);

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=Test");
    folder.browseName = QStringLiteral("Test");
    folder.displayName = QStringLiteral("Test");
    data.nodes.append(folder);

    Node scalar;
    scalar.kind = NodeKind::Variable;
    scalar.nodeId = QStringLiteral("ns=1;s=Test.IntValue");
    scalar.parentNodeId = folder.nodeId;
    scalar.browseName = QStringLiteral("IntValue");
    scalar.displayName = QStringLiteral("IntValue");
    scalar.dataType = QStringLiteral("Int32");
    scalar.valueRank = -1;
    scalar.writable = true;
    scalar.initialValue = 42;
    scalar.simulation.kind = SimulationKind::Sine;
    scalar.simulation.intervalMs = 250.0;
    scalar.simulation.min = -5.0;
    scalar.simulation.max = 5.0;
    scalar.simulation.periodMs = 8000.0;
    data.nodes.append(scalar);

    Node array;
    array.kind = NodeKind::Variable;
    array.nodeId = QStringLiteral("ns=1;s=Test.Doubles");
    array.parentNodeId = folder.nodeId;
    array.browseName = QStringLiteral("Doubles");
    array.displayName = QStringLiteral("Doubles");
    array.dataType = QStringLiteral("Double");
    array.valueRank = 1;
    array.writable = false;
    array.initialValue = QVariantList{1.5, 2.5, 3.5};
    data.nodes.append(array);

    return data;
}

void ServerProjectSerializerTest::roundTripPreservesData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("demo.uaserver"));

    const ProjectData original = makeSampleProject();
    const Serializer::SaveResult saved = Serializer::save(path, original);
    QVERIFY2(saved.ok, qPrintable(saved.errorString));

    const Serializer::LoadResult loaded = Serializer::load(path);
    QVERIFY2(loaded.ok, qPrintable(loaded.errorString));

    const ProjectData &copy = loaded.data;
    QCOMPARE(copy.formatVersion, kServerProjectFormatVersion);
    QCOMPARE(copy.displayName, original.displayName);
    QCOMPARE(copy.server.applicationName, original.server.applicationName);
    QCOMPARE(copy.server.endpoint.port, original.server.endpoint.port);
    QCOMPARE(copy.namespaces.size(), 1);
    QCOMPARE(copy.namespaces.first().uri, QStringLiteral("urn:opcuamanager:demo"));
    QCOMPARE(copy.nodes.size(), original.nodes.size());

    const Node &scalar = copy.nodes.at(1);
    QCOMPARE(scalar.nodeId, QStringLiteral("ns=1;s=Test.IntValue"));
    QCOMPARE(scalar.parentNodeId, QStringLiteral("ns=1;s=Test"));
    QCOMPARE(scalar.dataType, QStringLiteral("Int32"));
    QCOMPARE(scalar.valueRank, -1);
    QVERIFY(scalar.writable);
    QCOMPARE(scalar.initialValue.toInt(), 42);
    QCOMPARE(scalar.simulation.kind, SimulationKind::Sine);
    QCOMPARE(scalar.simulation.intervalMs, 250.0);
    QCOMPARE(scalar.simulation.periodMs, 8000.0);

    const Node &array = copy.nodes.at(2);
    QCOMPARE(array.valueRank, 1);
    QCOMPARE(array.initialValue.toList().size(), 3);
    QCOMPARE(array.initialValue.toList().at(2).toDouble(), 3.5);

    QCOMPARE(copy.security.allowAnonymous, false);
    QCOMPARE(copy.security.enableSecurity, true);
    QCOMPARE(copy.security.acceptAllClientCerts, false);
    QCOMPARE(copy.security.users.size(), 1);
    QCOMPARE(copy.security.users.first().username, QStringLiteral("admin"));
    QCOMPARE(copy.security.users.first().password, QStringLiteral("secret"));

    QCOMPARE(copy.enumTypes.size(), 1);
    QCOMPARE(copy.enumTypes.first().name, QStringLiteral("Mode"));
    QCOMPARE(copy.enumTypes.first().nodeId, QStringLiteral("ns=1;s=Enum.Mode"));
    QCOMPARE(copy.enumTypes.first().entries.size(), 2);
    QCOMPARE(copy.enumTypes.first().entries.at(1).value, 1);
    QCOMPARE(copy.enumTypes.first().entries.at(1).name, QStringLiteral("On"));
}

void ServerProjectSerializerTest::reportsFileNotFound()
{
    const Serializer::LoadResult result =
        Serializer::load(QStringLiteral("/no/such/file.uaserver"));
    QVERIFY(!result.ok);
    QCOMPARE(result.error, Serializer::Error::FileNotFound);
}

void ServerProjectSerializerTest::reportsInvalidJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.uaserver"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{ this is not json ");
    file.close();

    const Serializer::LoadResult result = Serializer::load(path);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, Serializer::Error::InvalidJson);
}

void ServerProjectSerializerTest::reportsUnsupportedVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.uaserver"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("{ \"formatVersion\": 9999, \"nodes\": [] }"));
    file.close();

    const Serializer::LoadResult result = Serializer::load(path);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, Serializer::Error::UnsupportedVersion);
}

void ServerProjectSerializerTest::legacyFileDefaultsAcceptAllClientCerts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.uaserver"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // A v1 file whose security block predates the acceptAllClientCerts field.
    file.write(QByteArrayLiteral(
        "{ \"formatVersion\": 1, \"displayName\": \"Legacy\", \"nodes\": [],"
        " \"security\": { \"allowAnonymous\": true, \"enableSecurity\": true } }"));
    file.close();

    const Serializer::LoadResult result = Serializer::load(path);
    QVERIFY2(result.ok, qPrintable(result.errorString));
    // The convenience default keeps older projects behaving as before.
    QCOMPARE(result.data.security.acceptAllClientCerts, true);
}

QTEST_GUILESS_MAIN(ServerProjectSerializerTest)

#include "tst_serverprojectserializer.moc"
