#include <QTemporaryDir>
#include <QtTest>

#include "project/projectserializer.h"

/*!
 * \internal
 * \brief Builds a fully populated ProjectData for round-trip tests.
 */
static ProjectData makeProject()
{
    ProjectData data;
    data.displayName = QStringLiteral("Machine A");
    data.connection.discoveryUrl = QStringLiteral("opc.tcp://192.168.0.10:4840");
    data.connection.backend = QStringLiteral("open62541");
    data.connection.server = QStringLiteral("MachineA Server");
    data.connection.endpoint = QStringLiteral("opc.tcp://192.168.0.10:4840 (None)");
    data.connection.authMode = 1;
    data.connection.userName = QStringLiteral("operator");
    data.connection.endpointUrlRewriteEnabled = true;
    data.focusNode.nodeId = QStringLiteral("ns=2;s=Machine");
    data.focusNode.path = QStringLiteral("Objects/Machine");
    data.focusNode.displayName = QStringLiteral("Machine");
    data.settings.valueFormat = 1;

    MonitoredNodeRecord record;
    record.server = QStringLiteral("opc.tcp://192.168.0.10:4840");
    record.nodeId = QStringLiteral("ns=2;s=Temp");
    record.nodePath = QStringLiteral("Objects/Machine/Temp");
    record.displayName = QStringLiteral("Temp");
    record.dataType = QStringLiteral("Double");
    data.monitoredNodes.append(record);
    return data;
}

/*! Verifies ProjectSerializer round-trip and validation behavior. */
class ProjectSerializerTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that saving then loading preserves every project field. */
    void roundTripPreservesData();

    /*! Verifies that loading a missing file reports FileNotFound. */
    void loadMissingFileReportsError();

    /*! Verifies that loading malformed JSON reports InvalidJson. */
    void loadInvalidJsonReportsError();

    /*! Verifies that a file without formatVersion reports InvalidSchema. */
    void loadMissingVersionReportsSchemaError();

    /*! Verifies that a future format version reports UnsupportedVersion. */
    void loadFutureVersionReportsError();

    /*! Verifies that sampling intervals and the table layout survive a round trip. */
    void roundTripPreservesDataViewSettings();

    /*! Verifies that a version 1 project still loads with defaults for new fields. */
    void loadsVersionOneProjectWithDefaults();
};

/*!
 * \brief Verifies that saving then loading preserves every project field.
 */
void ProjectSerializerTest::roundTripPreservesData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("machine.uaproj"));

    const ProjectData original = makeProject();
    QString error;
    QVERIFY2(ProjectSerializer::save(path, original, &error), qPrintable(error));

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY2(result.ok, qPrintable(result.errorString));

    const ProjectData &loaded = result.data;
    QCOMPARE(loaded.formatVersion, kProjectFormatVersion);
    QCOMPARE(loaded.displayName, original.displayName);
    QCOMPARE(loaded.connection.discoveryUrl, original.connection.discoveryUrl);
    QCOMPARE(loaded.connection.backend, original.connection.backend);
    QCOMPARE(loaded.connection.server, original.connection.server);
    QCOMPARE(loaded.connection.endpoint, original.connection.endpoint);
    QCOMPARE(loaded.connection.authMode, original.connection.authMode);
    QCOMPARE(loaded.connection.userName, original.connection.userName);
    QCOMPARE(loaded.connection.endpointUrlRewriteEnabled,
             original.connection.endpointUrlRewriteEnabled);
    QCOMPARE(loaded.focusNode.nodeId, original.focusNode.nodeId);
    QCOMPARE(loaded.focusNode.path, original.focusNode.path);
    QCOMPARE(loaded.focusNode.displayName, original.focusNode.displayName);
    QCOMPARE(loaded.settings.valueFormat, original.settings.valueFormat);
    QCOMPARE(loaded.monitoredNodes.size(), 1);
    QCOMPARE(loaded.monitoredNodes.at(0).nodeId, original.monitoredNodes.at(0).nodeId);
    QCOMPARE(loaded.monitoredNodes.at(0).displayName, original.monitoredNodes.at(0).displayName);
    QCOMPARE(loaded.monitoredNodes.at(0).dataType, original.monitoredNodes.at(0).dataType);
}

/*!
 * \brief Verifies that loading a missing file reports FileNotFound.
 */
void ProjectSerializerTest::loadMissingFileReportsError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const ProjectSerializer::LoadResult result =
        ProjectSerializer::load(dir.filePath(QStringLiteral("nope.uaproj")));
    QVERIFY(!result.ok);
    QCOMPARE(result.error, ProjectSerializer::Error::FileNotFound);
}

/*!
 * \brief Verifies that loading malformed JSON reports InvalidJson.
 */
void ProjectSerializerTest::loadInvalidJsonReportsError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.uaproj"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("{ this is not json ");
    file.close();

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, ProjectSerializer::Error::InvalidJson);
}

/*!
 * \brief Verifies that a file without formatVersion reports InvalidSchema.
 */
void ProjectSerializerTest::loadMissingVersionReportsSchemaError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("noversion.uaproj"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({"displayName":"X"})");
    file.close();

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, ProjectSerializer::Error::InvalidSchema);
}

/*!
 * \brief Verifies that a future format version reports UnsupportedVersion.
 */
void ProjectSerializerTest::loadFutureVersionReportsError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.uaproj"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QStringLiteral(R"({"formatVersion":%1,"displayName":"X"})")
                   .arg(kProjectFormatVersion + 1)
                   .toUtf8());
    file.close();

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, ProjectSerializer::Error::UnsupportedVersion);
}

/*!
 * \brief Verifies that sampling intervals and the table layout survive a round trip.
 */
void ProjectSerializerTest::roundTripPreservesDataViewSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("layout.uaproj"));

    ProjectData saved = makeProject();
    saved.monitoredNodes[0].samplingIntervalMs = 750;
    saved.settings.dataView = QVariantMap {
        {QStringLiteral("widths"), QVariantList {40, -1, 120}},
        {QStringLiteral("visible"), QVariantList {true, false, true}},
        {QStringLiteral("sortColumn"), 2},
        {QStringLiteral("sortOrder"), 1}
    };

    QVERIFY(ProjectSerializer::save(path, saved, nullptr));

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(result.ok);
    QCOMPARE(result.data.formatVersion, kProjectFormatVersion);
    QCOMPARE(result.data.monitoredNodes.size(), 1);
    QCOMPARE(result.data.monitoredNodes.at(0).samplingIntervalMs, 750);

    const QVariantMap dataView = result.data.settings.dataView;
    QCOMPARE(dataView.value(QStringLiteral("sortColumn")).toInt(), 2);
    QCOMPARE(dataView.value(QStringLiteral("sortOrder")).toInt(), 1);
    QCOMPARE(dataView.value(QStringLiteral("widths")).toList(), QVariantList({40, -1, 120}));
    QCOMPARE(dataView.value(QStringLiteral("visible")).toList(),
             QVariantList({true, false, true}));
}

/*!
 * \brief Verifies that a version 1 project still loads with defaults for new fields.
 */
void ProjectSerializerTest::loadsVersionOneProjectWithDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.uaproj"));

    // A project written before the Data Access View gained per-node intervals and
    // a persisted column layout must keep opening without a migration step.
    const QString legacy = QStringLiteral(R"({
        "formatVersion": 1,
        "displayName": "Legacy",
        "connection": { "discoveryUrl": "opc.tcp://127.0.0.1:4840" },
        "focusNode": { "nodeId": "" },
        "monitoredNodes": [
            { "server": "srv", "nodeId": "ns=1;s=A", "nodePath": "Objects/A",
              "displayName": "A", "dataType": "DINT" }
        ],
        "settings": { "valueFormat": 1 }
    })");

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(legacy.toUtf8());
    file.close();

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(result.ok);
    QCOMPARE(result.data.formatVersion, 1);
    QCOMPARE(result.data.displayName, QStringLiteral("Legacy"));
    QCOMPARE(result.data.settings.valueFormat, 1);
    QCOMPARE(result.data.monitoredNodes.size(), 1);

    // The interval falls back to "use the service default".
    QCOMPARE(result.data.monitoredNodes.at(0).samplingIntervalMs, 0);
    QVERIFY(result.data.settings.dataView.isEmpty());
}

QTEST_MAIN(ProjectSerializerTest)

#include "tst_projectserializer.moc"
