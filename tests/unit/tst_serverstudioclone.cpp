// Unit test for the Server Studio side of "Clone to Server Studio": a clone
// result shaped like a CODESYS server (vendor IEC type aliases, a user
// structure, an unresolved type) is turned into a project whose variable types
// follow the resolved OPC UA base types instead of a blanket Int32.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core/clonebrowser.h"
#include "qmlapi/opcuamanager.h"
#include "qmlapi/serverstudio.h"
#include "serverproject/serverprojectserializer.h"

using Resolution = ResolvedDataType::Resolution;

/*! Verifies the data types and values Server Studio assigns to cloned variables. */
class ServerStudioCloneTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Vendor aliases keep their base type; unsupported types are substituted and reported. */
    void mapsResolvedBaseTypes();

private:
    /*! Returns a cloned variable \a name under \a parent with \a typeId resolved to \a base. */
    static CloneNode variable(const QString &name, const QString &parent, const QString &typeId,
                              const QString &base, Resolution resolution, const QVariant &value,
                              int valueRank = -1);

    /*! Returns the node with browse name \a name in \a project, or nullptr. */
    static const ServerProject::Node *find(const ServerProject::ProjectData &project,
                                           const QString &name);
};

CloneNode ServerStudioCloneTest::variable(const QString &name, const QString &parent,
                                          const QString &typeId, const QString &base,
                                          Resolution resolution, const QVariant &value,
                                          int valueRank)
{
    CloneNode node;
    node.nodeId = parent + QLatin1Char('.') + name;
    node.parentNodeId = parent;
    node.browseName = name;
    node.displayName = name;
    node.nodeClass = 2; // Variable
    node.isVariable = true;
    node.dataTypeId = typeId;
    node.valueRank = valueRank;
    node.value = value;
    node.resolvedDataType.typeId = typeId;
    node.resolvedDataType.builtInBase = base;
    node.resolvedDataType.resolution = resolution;
    return node;
}

const ServerProject::Node *ServerStudioCloneTest::find(const ServerProject::ProjectData &project,
                                                       const QString &name)
{
    for (const ServerProject::Node &node : project.nodes) {
        if (node.browseName == name)
            return &node;
    }
    return nullptr;
}

void ServerStudioCloneTest::mapsResolvedBaseTypes()
{
    const QString hmi = QStringLiteral("ns=4;s=|var|PLC.Application.V_HMI");

    CloneNode folder;
    folder.nodeId = hmi;
    folder.browseName = QStringLiteral("V_HMI");
    folder.displayName = QStringLiteral("V_HMI");
    folder.nodeClass = 1; // Object

    QList<CloneNode> nodes{folder};
    // CODESYS STRING / UINT aliases in the vendor namespace.
    nodes.append(variable(QStringLiteral("sAppVersion"), hmi, QStringLiteral("ns=3;i=3013"),
                          QStringLiteral("String"), Resolution::Hierarchy,
                          QStringLiteral("1.2.3")));
    nodes.append(variable(QStringLiteral("uiButtonCount"), hmi, QStringLiteral("ns=3;i=3002"),
                          QStringLiteral("UInt16"), Resolution::Hierarchy,
                          QVariant::fromValue(quint16(4))));
    // A user structure whose value is an ExtensionObject without text.
    nodes.append(variable(QStringLiteral("stButton"), hmi,
                          QStringLiteral("ns=4;s=|type|PLC.Application.STButton"),
                          QStringLiteral("Structure"), Resolution::Hierarchy, QVariant()));
    // An unresolved type carrying a string value.
    nodes.append(variable(QStringLiteral("sUnknown"), hmi, QStringLiteral("ns=9;i=1"), QString(),
                          Resolution::Unresolved, QStringLiteral("abc")));
    // A standard Boolean that was not resolved by the browser (local fallback).
    nodes.append(variable(QStringLiteral("xActive"), hmi, QStringLiteral("ns=0;i=1"), QString(),
                          Resolution::None, true));
    // LocaleId[] (a String subtype) keeps its texts.
    nodes.append(variable(QStringLiteral("LocaleIdArray"), hmi, QStringLiteral("ns=0;i=295"),
                          QStringLiteral("String"), Resolution::KnownSubtype,
                          QVariantList{QStringLiteral("en-US"), QStringLiteral("de-DE")}, 1));

    OpcUaManager manager;
    ServerStudio studio;
    studio.setOpcUaManager(&manager);
    QSignalSpy notificationSpy(&studio, &ServerStudio::notification);
    emit manager.cloneSnapshotReady(0, nodes, {}, true, false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("clone.uaserver"));
    QVERIFY(studio.saveProjectAs(path));
    const auto loaded = ServerProject::Serializer::load(path);
    QVERIFY2(loaded.ok, qPrintable(loaded.errorString));

    const ServerProject::Node *version = find(loaded.data, QStringLiteral("sAppVersion"));
    QVERIFY(version);
    QCOMPARE(version->dataType, QStringLiteral("String"));
    QCOMPARE(version->initialValue.toString(), QStringLiteral("1.2.3"));

    const ServerProject::Node *count = find(loaded.data, QStringLiteral("uiButtonCount"));
    QVERIFY(count);
    QCOMPARE(count->dataType, QStringLiteral("UInt16"));
    QCOMPARE(count->initialValue.toInt(), 4);

    const ServerProject::Node *button = find(loaded.data, QStringLiteral("stButton"));
    QVERIFY(button);
    QCOMPARE(button->dataType, QStringLiteral("String"));

    const ServerProject::Node *unknown = find(loaded.data, QStringLiteral("sUnknown"));
    QVERIFY(unknown);
    QCOMPARE(unknown->dataType, QStringLiteral("String"));
    QCOMPARE(unknown->initialValue.toString(), QStringLiteral("abc"));

    const ServerProject::Node *active = find(loaded.data, QStringLiteral("xActive"));
    QVERIFY(active);
    QCOMPARE(active->dataType, QStringLiteral("Boolean"));
    QCOMPARE(active->initialValue.toBool(), true);

    const ServerProject::Node *locales = find(loaded.data, QStringLiteral("LocaleIdArray"));
    QVERIFY(locales);
    QCOMPARE(locales->dataType, QStringLiteral("String"));
    QCOMPARE(locales->valueRank, 1);
    QCOMPARE(locales->initialValue.toList().value(1).toString(), QStringLiteral("de-DE"));

    // stButton and sUnknown are substitutes; the report says so.
    QVERIFY(!notificationSpy.isEmpty());
    bool reported = false;
    for (const QList<QVariant> &args : std::as_const(notificationSpy)) {
        if (args.at(1).toString().contains(QLatin1String("2 variable(s) use a substitute")))
            reported = true;
    }
    QVERIFY2(reported, "clone notification does not report the substituted variables");
}

QTEST_GUILESS_MAIN(ServerStudioCloneTest)

#include "tst_serverstudioclone.moc"
