// Unit test for DataType resolution used by "Clone to Server Studio": node id
// normalization, the namespace-0 tables, the supertype walk over a collected
// HasSubtype hierarchy, and the Server Studio type chosen for a clone.

#include <QDateTime>
#include <QtTest>

#include "core/datatyperesolver.h"
#include "serverproject/clonedatatype.h"

using Resolution = ResolvedDataType::Resolution;

/*! Verifies DataType resolution and the clone data-type choice without a server. */
class DataTypeResolverTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Short, explicit and URI forms of namespace-0 ids normalize to "ns=0;...". */
    void normalizesNodeIds();

    /*! Built-in types and well-known subtypes resolve without a hierarchy. */
    void resolvesNamespaceZeroLocally();

    /*! A CODESYS-style STRING alias resolves to String through its supertype. */
    void resolvesVendorStringAlias();

    /*! A vendor subtype of a well-known subtype resolves to that subtype's base. */
    void resolvesThroughKnownSubtype();

    /*! User structures and enumerations resolve to Structure and Enumeration. */
    void resolvesStructureAndEnumeration();

    /*! Missing entries, recorded failures and cycles are reported as unresolved. */
    void reportsUnresolvedTypes();

    /*! Supported base types are kept; others are substituted from the value. */
    void choosesCloneDataType();
};

void DataTypeResolverTest::normalizesNodeIds()
{
    QCOMPARE(DataTypeIds::normalize(QStringLiteral("i=12")), QStringLiteral("ns=0;i=12"));
    QCOMPARE(DataTypeIds::normalize(QStringLiteral(" ns=0;i=12 ")), QStringLiteral("ns=0;i=12"));
    QCOMPARE(DataTypeIds::normalize(QStringLiteral("nsu=http://opcfoundation.org/UA/;i=290")),
             QStringLiteral("ns=0;i=290"));
    QCOMPARE(DataTypeIds::normalize(QStringLiteral("ns=3;i=3013")), QStringLiteral("ns=3;i=3013"));
    QCOMPARE(DataTypeIds::normalize(QStringLiteral("nsu=urn:vendor;i=7")),
             QStringLiteral("nsu=urn:vendor;i=7"));
    QVERIFY(DataTypeIds::normalize(QString()).isEmpty());
}

void DataTypeResolverTest::resolvesNamespaceZeroLocally()
{
    const ResolvedDataType string = DataTypeIds::resolveLocally(QStringLiteral("i=12"));
    QCOMPARE(string.resolution, Resolution::BuiltIn);
    QCOMPARE(string.builtInBase, QStringLiteral("String"));
    QCOMPARE(string.typeName, QStringLiteral("String"));

    const ResolvedDataType dateTime = DataTypeIds::resolveLocally(QStringLiteral("ns=0;i=13"));
    QCOMPARE(dateTime.builtInBase, QStringLiteral("DateTime"));

    const ResolvedDataType duration = DataTypeIds::resolveLocally(QStringLiteral("ns=0;i=290"));
    QCOMPARE(duration.resolution, Resolution::KnownSubtype);
    QCOMPARE(duration.builtInBase, QStringLiteral("Double"));
    QCOMPARE(duration.typeName, QStringLiteral("Duration"));

    const ResolvedDataType localeId = DataTypeIds::resolveLocally(QStringLiteral("ns=0;i=295"));
    QCOMPARE(localeId.builtInBase, QStringLiteral("String"));

    const ResolvedDataType vendor = DataTypeIds::resolveLocally(QStringLiteral("ns=3;i=3013"));
    QCOMPARE(vendor.resolution, Resolution::None);
    QVERIFY(vendor.builtInBase.isEmpty());
}

void DataTypeResolverTest::resolvesVendorStringAlias()
{
    DataTypeHierarchy hierarchy;
    hierarchy.addType(QStringLiteral("ns=3;i=3013"), QStringLiteral("ns=0;i=12"),
                      QStringLiteral("STRING"));
    hierarchy.addType(QStringLiteral("ns=3;i=3002"), QStringLiteral("ns=0;i=5"),
                      QStringLiteral("UINT"));

    const ResolvedDataType string = hierarchy.resolve(QStringLiteral("ns=3;i=3013"));
    QCOMPARE(string.resolution, Resolution::Hierarchy);
    QCOMPARE(string.typeId, QStringLiteral("ns=3;i=3013"));
    QCOMPARE(string.typeName, QStringLiteral("STRING"));
    QCOMPARE(string.builtInBase, QStringLiteral("String"));
    QCOMPARE(string.superTypeIds, QStringList{QStringLiteral("ns=0;i=12")});

    const ResolvedDataType uint = hierarchy.resolve(QStringLiteral("ns=3;i=3002"));
    QCOMPARE(uint.builtInBase, QStringLiteral("UInt16"));
    QCOMPARE(uint.typeName, QStringLiteral("UINT"));
}

void DataTypeResolverTest::resolvesThroughKnownSubtype()
{
    DataTypeHierarchy hierarchy;
    hierarchy.addType(QStringLiteral("ns=2;i=50"), QStringLiteral("ns=2;i=40"),
                      QStringLiteral("MyDuration"));
    hierarchy.addType(QStringLiteral("ns=2;i=40"), QStringLiteral("ns=0;i=290"),
                      QStringLiteral("PlantDuration"));

    const ResolvedDataType resolved = hierarchy.resolve(QStringLiteral("ns=2;i=50"));
    QCOMPARE(resolved.resolution, Resolution::Hierarchy);
    QCOMPARE(resolved.builtInBase, QStringLiteral("Double"));
    QCOMPARE(resolved.superTypeIds,
             (QStringList{QStringLiteral("ns=2;i=40"), QStringLiteral("ns=0;i=290")}));
}

void DataTypeResolverTest::resolvesStructureAndEnumeration()
{
    DataTypeHierarchy hierarchy;
    const QString button = QStringLiteral("ns=4;s=|type|CODESYS.Application.STButton");
    hierarchy.addType(button, QStringLiteral("ns=0;i=22"), QStringLiteral("STButton"));
    hierarchy.addType(QStringLiteral("ns=4;s=|type|CODESYS.Application.E_Mode"),
                      QStringLiteral("ns=0;i=29"), QStringLiteral("E_Mode"));
    // ServerStatusDataType (i=862) is a standard structure outside the local tables.
    hierarchy.addType(QStringLiteral("ns=0;i=862"), QStringLiteral("ns=0;i=22"),
                      QStringLiteral("ServerStatusDataType"));

    const ResolvedDataType structure = hierarchy.resolve(button);
    QCOMPARE(structure.builtInBase, QStringLiteral("Structure"));
    QCOMPARE(structure.typeName, QStringLiteral("STButton"));

    QCOMPARE(hierarchy.resolve(QStringLiteral("ns=4;s=|type|CODESYS.Application.E_Mode"))
                 .builtInBase,
             QStringLiteral("Enumeration"));
    QCOMPARE(hierarchy.resolve(QStringLiteral("i=862")).builtInBase, QStringLiteral("Structure"));
}

void DataTypeResolverTest::reportsUnresolvedTypes()
{
    DataTypeHierarchy hierarchy;

    const ResolvedDataType empty = hierarchy.resolve(QString());
    QCOMPARE(empty.resolution, Resolution::Unresolved);
    QVERIFY(!empty.reason.isEmpty());

    const ResolvedDataType unknown = hierarchy.resolve(QStringLiteral("ns=5;i=1"));
    QCOMPARE(unknown.resolution, Resolution::Unresolved);
    QVERIFY(unknown.builtInBase.isEmpty());
    QVERIFY(!unknown.reason.isEmpty());

    hierarchy.addFailure(QStringLiteral("ns=5;i=2"), QStringLiteral("Broken"),
                         QStringLiteral("browse of ns=5;i=2 failed: BadNodeIdUnknown"));
    const ResolvedDataType failed = hierarchy.resolve(QStringLiteral("ns=5;i=2"));
    QCOMPARE(failed.resolution, Resolution::Unresolved);
    QCOMPARE(failed.typeName, QStringLiteral("Broken"));
    QVERIFY(failed.reason.contains(QLatin1String("BadNodeIdUnknown")));

    hierarchy.addType(QStringLiteral("ns=5;i=10"), QStringLiteral("ns=5;i=11"), QString());
    hierarchy.addType(QStringLiteral("ns=5;i=11"), QStringLiteral("ns=5;i=10"), QString());
    const ResolvedDataType cyclic = hierarchy.resolve(QStringLiteral("ns=5;i=10"));
    QCOMPARE(cyclic.resolution, Resolution::Unresolved);
    QVERIFY(cyclic.reason.contains(QLatin1String("cyclic")));
}

void DataTypeResolverTest::choosesCloneDataType()
{
    using ServerProject::cloneDataTypeFor;

    // Supported base types map exactly, independent of the value.
    QCOMPARE(cloneDataTypeFor(QStringLiteral("String"), QVariant()).dataType,
             QStringLiteral("String"));
    QVERIFY(!cloneDataTypeFor(QStringLiteral("String"), QVariant()).substituted);
    QCOMPARE(cloneDataTypeFor(QStringLiteral("UInt16"), QVariant(quint16(3))).dataType,
             QStringLiteral("UInt16"));

    // Enumerations are Int32 on the wire.
    QCOMPARE(cloneDataTypeFor(QStringLiteral("Enumeration"), QVariant(2)).dataType,
             QStringLiteral("Int32"));
    QVERIFY(!cloneDataTypeFor(QStringLiteral("Enumeration"), QVariant(2)).substituted);

    // Unresolved or abstract types take the value's type and are marked.
    const auto fromString = cloneDataTypeFor(QString(), QVariant(QStringLiteral("OK")));
    QCOMPARE(fromString.dataType, QStringLiteral("String"));
    QVERIFY(fromString.substituted);
    QCOMPARE(cloneDataTypeFor(QStringLiteral("Number"), QVariant(1.5)).dataType,
             QStringLiteral("Double"));
    QCOMPARE(cloneDataTypeFor(QString(), QVariant(QVariantList{true, false})).dataType,
             QStringLiteral("Boolean"));

    // Types Server Studio cannot model fall back to String and are marked.
    const auto dateTime = cloneDataTypeFor(QStringLiteral("DateTime"),
                                           QVariant(QDateTime::currentDateTimeUtc()));
    QCOMPARE(dateTime.dataType, QStringLiteral("String"));
    QVERIFY(dateTime.substituted);
    const auto structure = cloneDataTypeFor(QStringLiteral("Structure"), QVariant());
    QCOMPARE(structure.dataType, QStringLiteral("String"));
    QVERIFY(structure.substituted);

    // Never a silent Int32 for an unknown type without an Int32 value.
    QVERIFY(cloneDataTypeFor(QString(), QVariant()).dataType != QLatin1String("Int32"));
}

QTEST_GUILESS_MAIN(DataTypeResolverTest)

#include "tst_datatyperesolver.moc"
