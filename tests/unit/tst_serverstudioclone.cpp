// Unit test for the Server Studio side of "Clone to Server Studio": a clone
// result shaped like a CODESYS server (vendor IEC type aliases, a user
// structure, an unresolved type) is turned into a project whose variable types
// follow the resolved OPC UA base types instead of a blanket Int32.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core/clonebrowser.h"
#include "core/diagnosticslevel.h"
#include "qmlapi/opcuamanager.h"
#include "qmlapi/serverstudio.h"
#include "serverproject/serverprojectserializer.h"
#include "serverproject/serverprojectvalidator.h"

using Resolution = ResolvedDataType::Resolution;

/*! Verifies the data types and values Server Studio assigns to cloned variables. */
class ServerStudioCloneTest : public QObject
{
    Q_OBJECT

private slots:
    /*!
        Vendor aliases keep their base type, structures and arrays of structures
        keep their structure type, and unsupported types are substituted and reported.
    */
    void mapsResolvedBaseTypes();

    /*! Truncated, timed-out and partly failed clones are reported as incomplete. */
    void reportsIncompleteClone_data();
    void reportsIncompleteClone();

    /*! A cancelled clone leaves the open project unchanged. */
    void cancelledCloneKeepsProject();

    /*!
        A variable without a readable DataType but with members (a CODESYS
        structure holding a function block) is a container, not a failed read,
        and is cloned as a structure whose source type is "not provided".
    */
    void clonesContainersWithoutDataType();

private:
    /*! Returns the last notification text in \a spy, or an empty string. */
    static QString lastMessage(const QSignalSpy &spy);

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
    // A user structure (base Structure) with its members as child nodes; the
    // type name comes from the CODESYS "|type|" id because no BrowseName was set.
    const QString buttonType = QStringLiteral("ns=4;s=|type|PLC.Application.ST_HMI_Button");
    nodes.append(variable(QStringLiteral("stButton"), hmi, buttonType,
                          QStringLiteral("Structure"), Resolution::Hierarchy, QVariant()));
    const QString button = nodes.last().nodeId;
    nodes.append(variable(QStringLiteral("xPressed"), button, QStringLiteral("ns=0;i=1"),
                          QStringLiteral("Boolean"), Resolution::BuiltIn, false));
    nodes.append(variable(QStringLiteral("strText"), button, QStringLiteral("ns=3;i=3013"),
                          QStringLiteral("String"), Resolution::Hierarchy,
                          QStringLiteral("HMI Btn")));
    // An array of the same structure type; its elements are child nodes.
    nodes.append(variable(QStringLiteral("astButtons"), hmi, buttonType,
                          QStringLiteral("Structure"), Resolution::Hierarchy,
                          QVariantList{QVariant(), QVariant()}, 1));
    nodes.last().arrayDimensions = {2};
    nodes.append(variable(QStringLiteral("astButtons[1]"), nodes.last().nodeId, buttonType,
                          QStringLiteral("Structure"), Resolution::Hierarchy, QVariant()));
    // A structure whose type could not be resolved: recognized by its members,
    // named by the structType member as a fallback.
    nodes.append(variable(QStringLiteral("stLegacy"), hmi, QStringLiteral("ns=9;i=77"),
                          QString(), Resolution::Unresolved, QVariant()));
    const QString legacy = nodes.last().nodeId;
    nodes.append(variable(QStringLiteral("structType"), legacy, QStringLiteral("ns=0;i=12"),
                          QStringLiteral("String"), Resolution::BuiltIn,
                          QStringLiteral("ST_Legacy")));
    // A one-element string array that Qt OPC UA delivers as a scalar.
    nodes.append(variable(QStringLiteral("ServerArray"), hmi, QStringLiteral("ns=0;i=12"),
                          QStringLiteral("String"), Resolution::BuiltIn,
                          QStringLiteral("urn:plc:server"), 1));
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
    CloneCompletion completion;
    completion.state = CloneCompletion::State::Complete;
    completion.nodeCount = int(nodes.size());
    emit manager.cloneSnapshotReady(0, nodes, {}, completion);

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

    // Structures keep their own type and have no value; members stay typed.
    const ServerProject::Node *buttonNode = find(loaded.data, QStringLiteral("stButton"));
    QVERIFY(buttonNode);
    QVERIFY(buttonNode->isStructure());
    QVERIFY(!buttonNode->initialValue.isValid());
    QVERIFY(!buttonNode->writable);
    const auto typeName = [&loaded](const QString &typeId) {
        for (const ServerProject::StructType &type : loaded.data.structTypes) {
            if (type.nodeId == typeId)
                return type.name;
        }
        return QString();
    };
    QCOMPARE(typeName(buttonNode->structTypeId), QStringLiteral("ST_HMI_Button"));
    QCOMPARE(buttonNode->source.originalDataTypeId, buttonType);
    QCOMPARE(find(loaded.data, QStringLiteral("strText"))->dataType, QStringLiteral("String"));
    QCOMPARE(find(loaded.data, QStringLiteral("xPressed"))->dataType, QStringLiteral("Boolean"));

    const ServerProject::Node *buttons = find(loaded.data, QStringLiteral("astButtons"));
    QVERIFY(buttons);
    QCOMPARE(buttons->structTypeId, buttonNode->structTypeId); // one type for both
    QCOMPARE(buttons->valueRank, 1);
    QCOMPARE(buttons->arrayDimensions, QList<quint32>{2});
    QCOMPARE(ServerProject::variableTypeText(*buttons, typeName(buttons->structTypeId)),
             QStringLiteral("ST_HMI_Button[2]"));
    QVERIFY(find(loaded.data, QStringLiteral("astButtons[1]"))->isStructure());

    const ServerProject::Node *legacyNode = find(loaded.data, QStringLiteral("stLegacy"));
    QVERIFY(legacyNode);
    QVERIFY(legacyNode->isStructure());
    QCOMPARE(typeName(legacyNode->structTypeId), QStringLiteral("ST_Legacy"));
    QCOMPARE(loaded.data.structTypes.size(), 2);

    const ServerProject::Node *serverArray = find(loaded.data, QStringLiteral("ServerArray"));
    QVERIFY(serverArray);
    QCOMPARE(serverArray->valueRank, 1);
    QCOMPARE(serverArray->initialValue.toList(), QVariantList{QStringLiteral("urn:plc:server")});

    // The cloned project is valid for the runtime.
    const auto validation = ServerProject::Validator::validate(loaded.data);
    QVERIFY2(validation.errors.isEmpty(),
             qPrintable(validation.errors.join(QStringLiteral("; "))));

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

    // Only sUnknown is a substitute (structures are exact now); the report says so.
    QVERIFY(!notificationSpy.isEmpty());
    bool reported = false;
    for (const QList<QVariant> &args : std::as_const(notificationSpy)) {
        if (args.at(1).toString().contains(QLatin1String("1 variable(s) use a substitute")))
            reported = true;
    }
    QVERIFY2(reported, "clone notification does not report the substituted variables");
}

QString ServerStudioCloneTest::lastMessage(const QSignalSpy &spy)
{
    return spy.isEmpty() ? QString() : spy.last().at(1).toString();
}

void ServerStudioCloneTest::reportsIncompleteClone_data()
{
    QTest::addColumn<int>("state");
    QTest::addColumn<bool>("nodeLimit");
    QTest::addColumn<int>("failedReads");
    QTest::addColumn<int>("limitedArrays");
    QTest::addColumn<QString>("expected");

    QTest::newRow("node limit") << int(CloneCompletion::State::Truncated) << true << 0 << 0
                                << QStringLiteral("the limit of 200000 nodes was reached");
    QTest::newRow("depth limit") << int(CloneCompletion::State::Truncated) << false << 0 << 0
                                 << QStringLiteral("at the depth limit of 64");
    QTest::newRow("timeout") << int(CloneCompletion::State::TimedOut) << false << 0 << 0
                             << QStringLiteral("did not answer in time and 7 node(s)");
    QTest::newRow("failed reads") << int(CloneCompletion::State::Complete) << false << 3 << 0
                                  << QStringLiteral("3 node(s) could not be read");
    QTest::newRow("array limit") << int(CloneCompletion::State::Complete) << false << 0 << 2
                                 << QStringLiteral("2 array(s) were limited to their first 10 "
                                                   "elements; 1980 element(s) were not cloned");
}

void ServerStudioCloneTest::reportsIncompleteClone()
{
    QFETCH(int, state);
    QFETCH(bool, nodeLimit);
    QFETCH(int, failedReads);
    QFETCH(int, limitedArrays);
    QFETCH(QString, expected);

    const QString hmi = QStringLiteral("ns=4;s=|var|PLC.Application.V_HMI");
    QList<CloneNode> nodes{variable(QStringLiteral("xActive"), hmi, QStringLiteral("ns=0;i=1"),
                                    QStringLiteral("Boolean"), Resolution::BuiltIn, true)};
    nodes.first().parentNodeId.clear();

    CloneCompletion completion;
    completion.state = static_cast<CloneCompletion::State>(state);
    completion.nodeCount = int(nodes.size());
    completion.nodeLimitReached = nodeLimit;
    completion.depthLimitedNodes = nodeLimit ? 0 : 2;
    completion.pendingRequests = 7;
    completion.failedReads = failedReads;
    completion.limitedArrays = limitedArrays;
    completion.arrayElementsSkipped = limitedArrays > 0 ? 1980 : 0;
    completion.options.maxArrayElements = 10;

    OpcUaManager manager;
    ServerStudio studio;
    studio.setOpcUaManager(&manager);
    QSignalSpy notificationSpy(&studio, &ServerStudio::notification);
    emit manager.cloneSnapshotReady(0, nodes, {}, completion);

    QVERIFY(studio.hasProject());
    const QString message = lastMessage(notificationSpy);
    QVERIFY2(message.contains(expected), qPrintable(message));
    QCOMPARE(notificationSpy.last().at(0).toInt(), int(Diagnostics::Warning));
}

void ServerStudioCloneTest::cancelledCloneKeepsProject()
{
    OpcUaManager manager;
    ServerStudio studio;
    studio.setOpcUaManager(&manager);
    studio.newProject(QStringLiteral("Kept"));
    QCOMPARE(studio.projectName(), QStringLiteral("Kept"));

    CloneNode folder;
    folder.nodeId = QStringLiteral("ns=1;s=A");
    folder.browseName = QStringLiteral("A");
    folder.nodeClass = 1;
    CloneCompletion completion;
    completion.state = CloneCompletion::State::Cancelled;
    QSignalSpy notificationSpy(&studio, &ServerStudio::notification);
    emit manager.cloneSnapshotReady(0, {folder}, {}, completion);

    QCOMPARE(studio.projectName(), QStringLiteral("Kept"));
    QVERIFY(lastMessage(notificationSpy).contains(QLatin1String("cancelled")));
    QVERIFY(!studio.cloneInProgress());
}

void ServerStudioCloneTest::clonesContainersWithoutDataType()
{
    CloneNode recipe = variable(QStringLiteral("stRecipe"), QStringLiteral("ns=4;s=V_HMI"),
                                QString(), QString(), Resolution::None, QVariant());
    recipe.attributeErrors = {QStringLiteral("DataType: BadNotSupported")};
    CloneNode key = variable(QStringLiteral("strKey_Set"), recipe.nodeId,
                             QStringLiteral("ns=0;i=12"), QStringLiteral("String"),
                             Resolution::BuiltIn, QStringLiteral("K1"));
    CloneNode fb;
    fb.nodeId = recipe.nodeId + QStringLiteral(".fbProduct");
    fb.parentNodeId = recipe.nodeId;
    fb.browseName = QStringLiteral("fbProduct");
    fb.nodeClass = 1;
    // A variable with the same error but no members stays a failed read, and
    // so does one that also failed for another attribute.
    CloneNode lonely = variable(QStringLiteral("xLonely"), QStringLiteral("ns=4;s=V_HMI"),
                                QString(), QString(), Resolution::None, QVariant());
    lonely.attributeErrors = {QStringLiteral("DataType: BadNotSupported")};
    CloneNode broken = variable(QStringLiteral("stBroken"), QStringLiteral("ns=4;s=V_HMI"),
                                QString(), QString(), Resolution::None, QVariant());
    broken.attributeErrors = {QStringLiteral("DataType: BadNotSupported"),
                              QStringLiteral("ValueRank: BadNotSupported")};
    CloneNode brokenChild = variable(QStringLiteral("x"), broken.nodeId,
                                     QStringLiteral("ns=0;i=1"), QStringLiteral("Boolean"),
                                     Resolution::BuiltIn, true);

    QList<CloneNode> nodes{recipe, key, fb, lonely, broken, brokenChild};
    QCOMPARE(CloneBrowser::markContainersWithoutDataType(nodes), 1);
    QVERIFY(nodes.at(0).dataTypeNotProvided);
    QVERIFY(!nodes.at(3).dataTypeNotProvided);
    QVERIFY(!nodes.at(4).dataTypeNotProvided);

    OpcUaManager manager;
    ServerStudio studio;
    studio.setOpcUaManager(&manager);
    CloneCompletion completion;
    completion.state = CloneCompletion::State::Complete;
    completion.nodeCount = int(nodes.size());
    emit manager.cloneSnapshotReady(0, {recipe, key, fb}, {}, completion);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("containers.uaserver"));
    QVERIFY(studio.saveProjectAs(path));
    const auto loaded = ServerProject::Serializer::load(path);
    QVERIFY2(loaded.ok, qPrintable(loaded.errorString));
    const ServerProject::Node *recipeNode = find(loaded.data, QStringLiteral("stRecipe"));
    QVERIFY(recipeNode);
    QVERIFY(recipeNode->isStructure());
    QVERIFY(find(loaded.data, QStringLiteral("fbProduct")));

    studio.setSelectedNodeId(recipeNode->nodeId);
    QVERIFY(studio.selectedNode().value(QStringLiteral("sourceDataType")).toString()
                .contains(QLatin1String("not provided by the server")));
}

QTEST_GUILESS_MAIN(ServerStudioCloneTest)

#include "tst_serverstudioclone.moc"
