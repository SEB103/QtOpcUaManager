#include <QtTest>
#include <qopcuatype.h>

#include "core/opcuaaccesslevel.h"
#include "models/attributesmodel.h"

/*!
 * \internal
 * \brief Builds an attribute snapshot of a writable variable node.
 */
static OpcUaAttributeData makeVariable()
{
    OpcUaAttributeData data;
    data.nodeId = QStringLiteral("ns=2;s=Temp");
    data.nodeClass = int(QOpcUa::NodeClass::Variable);
    data.nodeClassName = QStringLiteral("Variable");
    data.browseName = QStringLiteral("Temp");
    data.displayName = QStringLiteral("Temperature");
    data.value = QStringLiteral("21.5");
    data.dataType = QStringLiteral("Double");
    data.sourceTimestamp = QStringLiteral("2026-09-06T10:00:00");
    data.serverTimestamp = QStringLiteral("2026-09-06T10:00:01");
    data.statusCode = QStringLiteral("Good");
    data.accessLevel = OpcUaAccessLevel::CurrentRead | OpcUaAccessLevel::CurrentWrite;
    data.userAccessLevel = OpcUaAccessLevel::CurrentRead;
    data.valueRank = -1;
    data.historizing = 0;
    data.minimumSamplingInterval = 100.0;
    data.writeMask = 0;
    return data;
}

/*! Verifies which attributes the panel lists and how it renders them. */
class AttributesModelTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a variable lists the value group with readable values. */
    void variableListsTheValueGroup();

    /*! Verifies that an attribute the server omits is marked, not dropped. */
    void absentAttributesAreMarkedAsNotProvided();

    /*! Verifies that a non-variable node does not list value attributes. */
    void nonVariableNodesOmitTheValueGroup();

    /*! Verifies that the access level is spelled out rather than shown as a mask. */
    void accessLevelIsSpelledOut();

    /*! Verifies that clear() empties the panel. */
    void clearEmptiesThePanel();
};

/*!
 * \brief Verifies that a variable lists the value group with readable values.
 */
void AttributesModelTest::variableListsTheValueGroup()
{
    AttributesModel model;
    model.setAttributes(makeVariable());

    QCOMPARE(model.valueFor(QStringLiteral("NodeId")), QStringLiteral("ns=2;s=Temp"));
    QCOMPARE(model.valueFor(QStringLiteral("DisplayName")), QStringLiteral("Temperature"));
    QCOMPARE(model.valueFor(QStringLiteral("Value")), QStringLiteral("21.5"));
    QCOMPARE(model.valueFor(QStringLiteral("DataType")), QStringLiteral("Double"));
    QCOMPARE(model.valueFor(QStringLiteral("StatusCode")), QStringLiteral("Good"));

    // The special ValueRank codes are explained rather than shown as bare numbers.
    QVERIFY(model.valueFor(QStringLiteral("ValueRank")).contains(QStringLiteral("Scalar")));

    QCOMPARE(model.valueFor(QStringLiteral("Historizing")), QStringLiteral("false"));
    QVERIFY(model.valueFor(QStringLiteral("MinimumSamplingInterval"))
                .contains(QStringLiteral("100")));

    // These attributes were previously never read at all.
    QVERIFY(!model.valueFor(QStringLiteral("AccessLevel")).isEmpty());
    QVERIFY(!model.valueFor(QStringLiteral("UserAccessLevel")).isEmpty());
    QCOMPARE(model.valueFor(QStringLiteral("WriteMask")), QStringLiteral("0"));
}

/*!
 * \brief Verifies that an attribute the server omits is marked, not dropped.
 */
void AttributesModelTest::absentAttributesAreMarkedAsNotProvided()
{
    OpcUaAttributeData data = makeVariable();
    data.description.clear();
    data.arrayDimensions.clear();
    data.accessLevel = OpcUaAccessLevel::Unknown;
    data.historizing = -1;
    data.minimumSamplingInterval = -1.0;
    data.writeMask = -1;

    AttributesModel model;
    model.setAttributes(data);

    // The row must exist: "the server did not say" is information, while a
    // dropped row is indistinguishable from an attribute that was never asked for.
    const QString placeholder = model.valueFor(QStringLiteral("Description"));
    QVERIFY(!placeholder.isEmpty());
    QCOMPARE(model.valueFor(QStringLiteral("ArrayDimensions")), placeholder);
    QCOMPARE(model.valueFor(QStringLiteral("AccessLevel")), placeholder);
    QCOMPARE(model.valueFor(QStringLiteral("Historizing")), placeholder);
    QCOMPARE(model.valueFor(QStringLiteral("MinimumSamplingInterval")), placeholder);
    QCOMPARE(model.valueFor(QStringLiteral("WriteMask")), placeholder);
}

/*!
 * \brief Verifies that a non-variable node does not list value attributes.
 */
void AttributesModelTest::nonVariableNodesOmitTheValueGroup()
{
    OpcUaAttributeData data;
    data.nodeId = QStringLiteral("ns=2;s=Machine");
    data.nodeClass = int(QOpcUa::NodeClass::Object);
    data.nodeClassName = QStringLiteral("Object");
    data.browseName = QStringLiteral("Machine");
    data.displayName = QStringLiteral("Machine");

    AttributesModel model;
    model.setAttributes(data);

    QCOMPARE(model.valueFor(QStringLiteral("NodeClass")), QStringLiteral("Object"));

    // A folder has no value, data type, or access level; listing them as
    // "not provided" would be noise on every object node.
    QVERIFY(model.valueFor(QStringLiteral("Value")).isEmpty());
    QVERIFY(model.valueFor(QStringLiteral("DataType")).isEmpty());
    QVERIFY(model.valueFor(QStringLiteral("AccessLevel")).isEmpty());
    QVERIFY(model.valueFor(QStringLiteral("StatusCode")).isEmpty());
}

/*!
 * \brief Verifies that the access level is spelled out rather than shown as a mask.
 */
void AttributesModelTest::accessLevelIsSpelledOut()
{
    AttributesModel model;
    model.setAttributes(makeVariable());

    const QString accessLevel = model.valueFor(QStringLiteral("AccessLevel"));
    QVERIFY(accessLevel.contains(QStringLiteral("CurrentRead")));
    QVERIFY(accessLevel.contains(QStringLiteral("CurrentWrite")));
    QVERIFY(accessLevel.contains(QStringLiteral("3")));

    // A read-only node must not claim write access.
    const QString userAccessLevel = model.valueFor(QStringLiteral("UserAccessLevel"));
    QVERIFY(userAccessLevel.contains(QStringLiteral("CurrentRead")));
    QVERIFY(!userAccessLevel.contains(QStringLiteral("CurrentWrite")));

    QVERIFY(OpcUaAccessLevel::allowsWrite(OpcUaAccessLevel::CurrentRead
                                          | OpcUaAccessLevel::CurrentWrite));
    QVERIFY(!OpcUaAccessLevel::allowsWrite(OpcUaAccessLevel::CurrentRead));
    QVERIFY(!OpcUaAccessLevel::allowsWrite(OpcUaAccessLevel::Unknown));
    QVERIFY(!OpcUaAccessLevel::isKnown(OpcUaAccessLevel::Unknown));
    QVERIFY(OpcUaAccessLevel::toString(OpcUaAccessLevel::Unknown).isEmpty());
    QVERIFY(!OpcUaAccessLevel::toString(0).isEmpty());
}

/*!
 * \brief Verifies that clear() empties the panel.
 */
void AttributesModelTest::clearEmptiesThePanel()
{
    AttributesModel model;
    model.setAttributes(makeVariable());
    QVERIFY(model.rowCount() > 0);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.valueFor(QStringLiteral("NodeId")).isEmpty());
}

QTEST_MAIN(AttributesModelTest)

#include "tst_attributesmodel.moc"
