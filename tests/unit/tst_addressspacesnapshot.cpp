// Unit test for the address-space snapshot: the typed JSON value encoding and
// the snapshot document (sections, node order, value outcomes, completion).

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPoint>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include <QOpcUaExtensionObject>
#include <QOpcUaLocalizedText>
#include <QOpcUaMultiDimensionalArray>
#include <QOpcUaQualifiedName>
#include <QtOpcUa/qopcuatype.h>

#include <cmath>
#include <limits>

#include "core/addressspacesnapshot.h"
#include "core/snapshotvaluecodec.h"

using Resolution = ResolvedDataType::Resolution;

/*! Verifies SnapshotValueCodec and AddressSpaceSnapshot. */
class AddressSpaceSnapshotTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Numbers, Booleans and strings keep their JSON types; 64-bit integers keep every digit. */
    void encodesScalars();

    /*! DateTime, ByteString, Guid and the Qt OPC UA value types get their documented forms. */
    void encodesOpcUaTypes();

    /*! Lists, multi-dimensional arrays and restored one-element arrays keep their shape. */
    void encodesArrays();

    /*! The reported variant type is the OPC UA built-in type of the value or its elements. */
    void namesVariantTypes();

    /*! The document has every section, sorted nodes and explicit value outcomes. */
    void buildsDocument();

    /*! Without values no variable carries a value member. */
    void omitsValuesWhenNotIncluded();

    /*! write() produces the same document that toJson() returns. */
    void writesFile();

private:
    /*! Returns a variable \a name under \a parent of DataType \a typeId with \a base. */
    static CloneNode variable(const QString &name, const QString &parent, const QString &typeId,
                              const QString &typeName, const QString &base,
                              Resolution resolution);

    /*! Returns the node object with \a nodeId in \a nodes, or an empty object. */
    static QJsonObject findNode(const QJsonArray &nodes, const QString &nodeId);

    /*! Returns a snapshot input with a small mixed address space. */
    static SnapshotInput sampleInput();
};

CloneNode AddressSpaceSnapshotTest::variable(const QString &name, const QString &parent,
                                             const QString &typeId, const QString &typeName,
                                             const QString &base, Resolution resolution)
{
    CloneNode node;
    node.nodeId = QStringLiteral("ns=2;s=") + name;
    node.parentNodeId = parent;
    node.browseName = name;
    node.browseNameNamespace = 2;
    node.displayName = name;
    node.nodeClass = 2;
    node.isVariable = true;
    node.referenceTypeId = QStringLiteral("ns=0;i=47");
    node.typeDefinitionId = QStringLiteral("ns=0;i=63");
    node.dataTypeId = typeId;
    node.resolvedDataType.typeId = typeId;
    node.resolvedDataType.typeName = typeName;
    node.resolvedDataType.builtInBase = base;
    node.resolvedDataType.resolution = resolution;
    node.accessLevel = 3;
    node.userAccessLevel = 1;
    node.attributesRead = true;
    return node;
}

QJsonObject AddressSpaceSnapshotTest::findNode(const QJsonArray &nodes, const QString &nodeId)
{
    for (const QJsonValue &node : nodes) {
        if (node.toObject().value(QStringLiteral("nodeId")).toString() == nodeId)
            return node.toObject();
    }
    return {};
}

SnapshotInput AddressSpaceSnapshotTest::sampleInput()
{
    SnapshotInput input;
    input.application = QStringLiteral("OpcUaManager");
    input.applicationVersion = QStringLiteral("1.5.0");
    input.created = QDateTime(QDate(2026, 11, 26), QTime(10, 0), QTimeZone::UTC);
    input.scope.kind = QStringLiteral("Objects");
    input.scope.rootNodeId = QStringLiteral("ns=0;i=85");
    input.scope.rootBrowsePath = QStringLiteral("/Objects");
    input.namespaceUris = {QStringLiteral("http://opcfoundation.org/UA/"),
                           QStringLiteral("urn:test:server")};

    // Captured in server order: Zeta before Plant, so sorting is visible.
    CloneNode zeta;
    zeta.nodeId = QStringLiteral("ns=2;s=Zeta");
    zeta.browseName = QStringLiteral("Zeta");
    zeta.nodeClass = 1;
    zeta.browseError = QStringLiteral("BadNodeIdUnknown");
    input.nodes.append(zeta);

    CloneNode plant;
    plant.nodeId = QStringLiteral("ns=2;s=Plant");
    plant.browseName = QStringLiteral("Plant");
    plant.displayName = QStringLiteral("Plant");
    plant.nodeClass = 1;
    plant.referenceTypeId = QStringLiteral("ns=0;i=35");
    plant.typeDefinitionId = QStringLiteral("ns=0;i=61");
    plant.isFolder = true;
    input.nodes.append(plant);

    CloneNode label = variable(QStringLiteral("Label"), plant.nodeId, QStringLiteral("ns=2;i=5000"),
                               QStringLiteral("VendorString"), QStringLiteral("String"),
                               Resolution::Hierarchy);
    label.resolvedDataType.superTypeIds = {QStringLiteral("ns=0;i=12")};
    label.valueRead = true;
    label.value = QStringLiteral("abc");
    label.sourceTimestamp = QDateTime(QDate(2026, 11, 26), QTime(9, 0, 0, 125), QTimeZone::UTC);
    input.nodes.append(label);

    CloneNode button = variable(QStringLiteral("Button"), plant.nodeId,
                                QStringLiteral("ns=2;i=6000"), QStringLiteral("ST_Button"),
                                QStringLiteral("Structure"), Resolution::Hierarchy);
    button.valueSkipped = true;
    input.nodes.append(button);

    CloneNode locked = variable(QStringLiteral("Locked"), plant.nodeId, QStringLiteral("ns=0;i=6"),
                                QStringLiteral("Int32"), QStringLiteral("Int32"),
                                Resolution::BuiltIn);
    locked.valueRead = true;
    locked.valueStatus = quint32(QOpcUa::UaStatusCode::BadNotReadable);
    locked.attributeErrors = {QStringLiteral("Value: BadNotReadable")};
    input.nodes.append(locked);

    CloneNode pending = variable(QStringLiteral("Pending"), plant.nodeId,
                                 QStringLiteral("ns=9;i=1"), QString(), QString(),
                                 Resolution::Unresolved);
    pending.attributeErrors = {QStringLiteral("Value: not read, DataType lookup timed out")};
    input.nodes.append(pending);

    CloneNode matrix = variable(QStringLiteral("Matrix"), plant.nodeId, QStringLiteral("ns=0;i=6"),
                                QStringLiteral("Int32"), QStringLiteral("Int32"),
                                Resolution::BuiltIn);
    matrix.valueRank = 2;
    matrix.arrayDimensions = {2, 3};
    matrix.valueRead = true;
    matrix.value = QVariant::fromValue(
        QOpcUaMultiDimensionalArray(QVariantList{1, 2, 3, 4, 5, 6}, {2, 3}));
    input.nodes.append(matrix);

    input.completion.state = CloneCompletion::State::Truncated;
    input.completion.nodeCount = int(input.nodes.size());
    input.completion.nodeLimitReached = true;
    input.completion.failedReads = 2;
    input.completion.failedBrowses = 1;
    input.completion.elapsedMs = 4321;
    input.completion.options.maxNodes = 50000;
    input.completion.server.endpointUrl = QStringLiteral("opc.tcp://plc:4840");
    input.completion.server.securityMode = QStringLiteral("None");
    input.completion.server.productName = QStringLiteral("Test PLC");
    input.completion.server.buildDate = QDateTime(QDate(2024, 1, 5), QTime(1, 0), QTimeZone::UTC);
    return input;
}

void AddressSpaceSnapshotTest::encodesScalars()
{
    using SnapshotValueCodec::encode;
    QCOMPARE(encode(QVariant()), QJsonValue(QJsonValue::Null));
    QCOMPARE(encode(true), QJsonValue(true));
    QCOMPARE(encode(QVariant::fromValue<qint16>(-5)), QJsonValue(-5));
    QCOMPARE(encode(QVariant::fromValue<quint32>(4294967295u)), QJsonValue(4294967295.0));
    // 2^53 + 1 cannot be held by a double; the string keeps every digit.
    QCOMPARE(encode(QVariant::fromValue<qint64>(9007199254740993LL)),
             QJsonValue(QStringLiteral("9007199254740993")));
    QCOMPARE(encode(QVariant::fromValue<quint64>(std::numeric_limits<quint64>::max())),
             QJsonValue(QStringLiteral("18446744073709551615")));
    QCOMPARE(encode(QVariant::fromValue<float>(1.5f)), QJsonValue(1.5));
    QCOMPARE(encode(std::nan("")), QJsonValue(QStringLiteral("NaN")));
    QCOMPARE(encode(-std::numeric_limits<double>::infinity()),
             QJsonValue(QStringLiteral("-Infinity")));
    QCOMPARE(encode(QStringLiteral("text")), QJsonValue(QStringLiteral("text")));
}

void AddressSpaceSnapshotTest::encodesOpcUaTypes()
{
    using SnapshotValueCodec::encode;
    const QDateTime local(QDate(2026, 11, 26), QTime(10, 30, 0, 5), QTimeZone::fromSecondsAheadOfUtc(3600));
    QCOMPARE(encode(local), QJsonValue(QStringLiteral("2026-11-26T09:30:00.005Z")));
    QCOMPARE(encode(QDateTime()), QJsonValue(QJsonValue::Null));
    QCOMPARE(encode(QByteArray("\x01\x02\xff", 3)), QJsonValue(QStringLiteral("AQL/")));
    const QUuid uuid(QStringLiteral("{12345678-1234-5678-9abc-def012345678}"));
    QCOMPARE(encode(uuid), QJsonValue(QStringLiteral("12345678-1234-5678-9abc-def012345678")));

    const QJsonObject text =
        encode(QVariant::fromValue(QOpcUaLocalizedText(QStringLiteral("de-DE"), QStringLiteral("Hallo"))))
            .toObject();
    QCOMPARE(text.value(QStringLiteral("locale")).toString(), QStringLiteral("de-DE"));
    QCOMPARE(text.value(QStringLiteral("text")).toString(), QStringLiteral("Hallo"));

    const QJsonObject name =
        encode(QVariant::fromValue(QOpcUaQualifiedName(4, QStringLiteral("strText")))).toObject();
    QCOMPARE(name.value(QStringLiteral("namespaceIndex")).toInt(), 4);
    QCOMPARE(name.value(QStringLiteral("name")).toString(), QStringLiteral("strText"));

    QOpcUaExtensionObject object;
    object.setBinaryEncodedBody(QByteArray("\x01\x00", 2), QStringLiteral("ns=4;i=99"));
    const QJsonObject extension = encode(QVariant::fromValue(object)).toObject();
    QCOMPARE(extension.value(QStringLiteral("encodingTypeId")).toString(),
             QStringLiteral("ns=4;i=99"));
    QCOMPARE(extension.value(QStringLiteral("encoding")).toString(), QStringLiteral("ByteString"));
    QCOMPARE(extension.value(QStringLiteral("body")).toString(), QStringLiteral("AQA="));

    QCOMPARE(encode(QVariant::fromValue(QOpcUa::UaStatusCode::BadNotReadable)),
             QJsonValue(QStringLiteral("BadNotReadable")));

    const QJsonObject unsupported = encode(QPoint(1, 2)).toObject();
    QCOMPARE(unsupported.value(QStringLiteral("unsupported")).toString(), QStringLiteral("QPoint"));
}

void AddressSpaceSnapshotTest::encodesArrays()
{
    using SnapshotValueCodec::encode;
    using SnapshotValueCodec::encodeForValueRank;

    const QJsonArray list =
        encode(QVariantList{QVariant::fromValue<qint64>(1), QVariant::fromValue<qint64>(2)}).toArray();
    QCOMPARE(list, (QJsonArray{QStringLiteral("1"), QStringLiteral("2")}));

    const QJsonObject matrix = encode(QVariant::fromValue(
        QOpcUaMultiDimensionalArray(QVariantList{1, 2, 3, 4, 5, 6}, {2, 3}))).toObject();
    QCOMPARE(matrix.value(QStringLiteral("dimensions")).toArray(), (QJsonArray{2, 3}));
    QCOMPARE(matrix.value(QStringLiteral("elements")).toArray().size(), 6);

    // A one-element array delivered as a scalar is an array again for ValueRank 1.
    QCOMPARE(encodeForValueRank(QStringLiteral("urn:a"), 1),
             QJsonValue(QJsonArray{QStringLiteral("urn:a")}));
    QCOMPARE(encodeForValueRank(QStringLiteral("urn:a"), -1), QJsonValue(QStringLiteral("urn:a")));
    QCOMPARE(encodeForValueRank(QVariantList{1, 2}, 1), QJsonValue(QJsonArray{1, 2}));
    QCOMPARE(encodeForValueRank(QVariant(), 1), QJsonValue(QJsonValue::Null));
}

void AddressSpaceSnapshotTest::namesVariantTypes()
{
    using SnapshotValueCodec::variantTypeName;
    QCOMPARE(variantTypeName(QVariant::fromValue<quint16>(3)), QStringLiteral("UInt16"));
    QCOMPARE(variantTypeName(QVariantList{QVariant::fromValue<quint8>(1)}), QStringLiteral("Byte"));
    QCOMPARE(variantTypeName(QVariant::fromValue(
                 QOpcUaMultiDimensionalArray(QVariantList{1, 2}, {2}))),
             QStringLiteral("Int32"));
    QCOMPARE(variantTypeName(QVariant::fromValue(QOpcUaLocalizedText())),
             QStringLiteral("LocalizedText"));
    QCOMPARE(variantTypeName(QVariantList()), QString());
    QCOMPARE(variantTypeName(QVariant()), QString());
}

void AddressSpaceSnapshotTest::buildsDocument()
{
    const QJsonObject document = AddressSpaceSnapshot::toJson(sampleInput());

    QCOMPARE(document.value(QStringLiteral("format")).toString(),
             QStringLiteral("opcuamanager.addressspace-snapshot"));
    QCOMPARE(document.value(QStringLiteral("formatVersion")).toInt(), 1);
    QCOMPARE(document.value(QStringLiteral("created")).toString(),
             QStringLiteral("2026-11-26T10:00:00.000Z"));
    QCOMPARE(document.value(QStringLiteral("generator")).toObject().value(QStringLiteral("version")),
             QJsonValue(QStringLiteral("1.5.0")));

    const QJsonObject server = document.value(QStringLiteral("server")).toObject();
    QCOMPARE(server.value(QStringLiteral("productName")).toString(), QStringLiteral("Test PLC"));
    QCOMPARE(server.value(QStringLiteral("buildDate")).toString(),
             QStringLiteral("2024-01-05T01:00:00.000Z"));
    QVERIFY(!server.contains(QStringLiteral("productUri"))); // empty fields are left out

    const QJsonArray namespaces = document.value(QStringLiteral("namespaces")).toArray();
    QCOMPARE(namespaces.at(1).toObject().value(QStringLiteral("uri")).toString(),
             QStringLiteral("urn:test:server"));
    QCOMPARE(document.value(QStringLiteral("limits")).toObject().value(QStringLiteral("maxNodes")),
             QJsonValue(50000));

    const QJsonObject completion = document.value(QStringLiteral("completion")).toObject();
    QCOMPARE(completion.value(QStringLiteral("state")).toString(), QStringLiteral("Truncated"));
    const QJsonArray reasons = completion.value(QStringLiteral("reasons")).toArray();
    for (const char *reason : {"nodeLimit", "readErrors", "browseErrors", "unresolvedTypes"})
        QVERIFY2(reasons.contains(QLatin1String(reason)), reason);
    QCOMPARE(completion.value(QStringLiteral("unresolvedTypes")).toInt(), 1);

    // DataTypes: every used type once, sorted by id, with base and supertypes.
    const QJsonArray dataTypes = document.value(QStringLiteral("dataTypes")).toArray();
    QCOMPARE(dataTypes.size(), 4);
    QCOMPARE(dataTypes.at(0).toObject().value(QStringLiteral("nodeId")).toString(),
             QStringLiteral("ns=0;i=6"));
    const QJsonObject vendor = dataTypes.at(1).toObject();
    QCOMPARE(vendor.value(QStringLiteral("nodeId")).toString(), QStringLiteral("ns=2;i=5000"));
    QCOMPARE(vendor.value(QStringLiteral("builtInBase")).toString(), QStringLiteral("String"));
    QCOMPARE(vendor.value(QStringLiteral("superTypes")).toArray(),
             QJsonArray{QStringLiteral("ns=0;i=12")});
    QCOMPARE(vendor.value(QStringLiteral("resolution")).toString(), QStringLiteral("Hierarchy"));

    // Nodes are sorted by browse path, not by capture order.
    const QJsonArray nodes = document.value(QStringLiteral("nodes")).toArray();
    QCOMPARE(nodes.size(), 7);
    QCOMPARE(nodes.first().toObject().value(QStringLiteral("browsePath")).toString(),
             QStringLiteral("/Objects/Plant"));
    QCOMPARE(nodes.last().toObject().value(QStringLiteral("browsePath")).toString(),
             QStringLiteral("/Objects/Zeta"));

    const QJsonObject label = findNode(nodes, QStringLiteral("ns=2;s=Label"));
    QCOMPARE(label.value(QStringLiteral("browsePath")).toString(),
             QStringLiteral("/Objects/Plant/Label"));
    QCOMPARE(label.value(QStringLiteral("referenceTypeId")).toString(), QStringLiteral("ns=0;i=47"));
    const QJsonObject labelVariable = label.value(QStringLiteral("variable")).toObject();
    QCOMPARE(labelVariable.value(QStringLiteral("dataType")).toObject().value(QStringLiteral("name")),
             QJsonValue(QStringLiteral("VendorString")));
    QCOMPARE(labelVariable.value(QStringLiteral("accessLevel")).toArray(),
             (QJsonArray{QStringLiteral("CurrentRead"), QStringLiteral("CurrentWrite")}));
    const QJsonObject labelValue = labelVariable.value(QStringLiteral("value")).toObject();
    QCOMPARE(labelValue.value(QStringLiteral("status")).toString(), QStringLiteral("Good"));
    QCOMPARE(labelValue.value(QStringLiteral("variantType")).toString(), QStringLiteral("String"));
    QCOMPARE(labelValue.value(QStringLiteral("data")).toString(), QStringLiteral("abc"));
    QCOMPARE(labelValue.value(QStringLiteral("sourceTimestamp")).toString(),
             QStringLiteral("2026-11-26T09:00:00.125Z"));

    const auto valueOf = [&nodes](const char *name) {
        return findNode(nodes, QStringLiteral("ns=2;s=") + QLatin1String(name))
            .value(QStringLiteral("variable"))
            .toObject()
            .value(QStringLiteral("value"))
            .toObject();
    };
    QCOMPARE(valueOf("Button"), (QJsonObject{{QStringLiteral("skipped"), QStringLiteral("structure")}}));
    QCOMPARE(valueOf("Locked"), (QJsonObject{{QStringLiteral("status"), QStringLiteral("BadNotReadable")}}));
    const QJsonObject pending = valueOf("Pending");
    QCOMPARE(pending.value(QStringLiteral("read")), QJsonValue(false));
    QCOMPARE(pending.value(QStringLiteral("reason")).toString(),
             QStringLiteral("not read, DataType lookup timed out"));
    const QJsonObject matrixData = valueOf("Matrix").value(QStringLiteral("data")).toObject();
    QCOMPARE(matrixData.value(QStringLiteral("dimensions")).toArray(), (QJsonArray{2, 3}));

    // Diagnostics name every node with an attribute or browse error.
    const QJsonArray diagnostics = document.value(QStringLiteral("diagnostics")).toArray();
    QCOMPARE(diagnostics.size(), 3);
    QCOMPARE(diagnostics.last().toObject().value(QStringLiteral("errors")).toArray(),
             QJsonArray{QStringLiteral("Browse: BadNodeIdUnknown")});
}

void AddressSpaceSnapshotTest::omitsValuesWhenNotIncluded()
{
    SnapshotInput input = sampleInput();
    input.scope.includeValues = false;
    const QJsonObject document = AddressSpaceSnapshot::toJson(input);
    QVERIFY(!document.value(QStringLiteral("scope")).toObject()
                 .value(QStringLiteral("includeValues")).toBool());
    for (const QJsonValue &node : document.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject variable = node.toObject().value(QStringLiteral("variable")).toObject();
        QVERIFY(!variable.contains(QStringLiteral("value")));
    }
}

void AddressSpaceSnapshotTest::writesFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plc.uasnapshot.json"));
    const SnapshotInput input = sampleInput();
    const AddressSpaceSnapshot::WriteResult result = AddressSpaceSnapshot::write(path, input);
    QVERIFY2(result.ok, qPrintable(result.errorString));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QCOMPARE(qint64(data.size()), result.bytes);
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(data, &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QCOMPARE(parsed.object(), AddressSpaceSnapshot::toJson(input));

    const AddressSpaceSnapshot::WriteResult failed =
        AddressSpaceSnapshot::write(dir.filePath(QStringLiteral("missing/x.json")), input);
    QVERIFY(!failed.ok);
    QVERIFY(!failed.errorString.isEmpty());
}

QTEST_GUILESS_MAIN(AddressSpaceSnapshotTest)

#include "tst_addressspacesnapshot.moc"
