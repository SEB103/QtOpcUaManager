#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QtTest>

#include "core/opcuavaluetree.h"
#include "models/structuredvalueformatter.h"

/*!
 * \internal
 * \brief Builds a scalar value tree node named \a name with type \a typeName.
 */
static OpcUaValueTreeNode makeScalar(const QString &name,
                                     const QString &typeName,
                                     const QVariant &value)
{
    OpcUaValueTreeNode node;
    node.name = name;
    node.typeName = typeName;
    node.kind = OpcUaValueTreeNode::Kind::Scalar;
    node.scalarValue = value;
    node.scalarText = value.toString();
    return node;
}

/*!
 * \internal
 * \brief Builds an array node named \a name with element type \a typeName and \a elements.
 */
static OpcUaValueTreeNode makeArray(const QString &name,
                                    const QString &typeName,
                                    const QList<OpcUaValueTreeNode> &elements)
{
    OpcUaValueTreeNode node;
    node.name = name;
    node.typeName = typeName;
    node.valueRank = 1;
    node.kind = OpcUaValueTreeNode::Kind::Array;
    node.children = elements;
    return node;
}

/*!
 * \internal
 * \brief Builds a struct node named \a name with type \a typeName and \a fields.
 */
static OpcUaValueTreeNode makeStruct(const QString &name,
                                     const QString &typeName,
                                     const QList<OpcUaValueTreeNode> &fields)
{
    OpcUaValueTreeNode node;
    node.name = name;
    node.typeName = typeName;
    node.kind = OpcUaValueTreeNode::Kind::Struct;
    node.children = fields;
    return node;
}

/*! Verifies StructuredValueFormatter JSON and XML output for all value kinds. */
class StructuredValueFormatterTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a scalar keeps its JSON type (number, bool, string). */
    void jsonScalarKeepsType();

    /*! Verifies that a flat structure renders field name, type, and value. */
    void jsonStructRendersFields();

    /*! Verifies that an array renders its element list and value rank. */
    void jsonArrayRendersElements();

    /*! Verifies nested cases: array of struct, struct of array, struct of struct. */
    void jsonNestedStructures();

    /*! Verifies that XML output is well-formed and carries type attributes. */
    void xmlIsWellFormed();
};

void StructuredValueFormatterTest::jsonScalarKeepsType()
{
    const OpcUaValueTreeNode root = makeScalar(QString(), QStringLiteral("Int32"), 10);
    const QString text =
        StructuredValueFormatter::format(root, StructuredValueFormatter::Format::Json);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);

    const QJsonObject object = doc.object();
    QCOMPARE(object.value(QStringLiteral("type")).toString(), QStringLiteral("Int32"));
    QVERIFY(object.value(QStringLiteral("value")).isDouble());
    QCOMPARE(object.value(QStringLiteral("value")).toInt(), 10);
}

void StructuredValueFormatterTest::jsonStructRendersFields()
{
    const OpcUaValueTreeNode root = makeStruct(QString(), QStringLiteral("MyStruct"), {
        makeScalar(QStringLiteral("Field1"), QStringLiteral("UInt32"), 10u),
        makeScalar(QStringLiteral("Field2"), QStringLiteral("String"), QStringLiteral("Test")),
        makeScalar(QStringLiteral("Field3"), QStringLiteral("Boolean"), true)
    });

    const QString text =
        StructuredValueFormatter::format(root, StructuredValueFormatter::Format::Json);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);

    const QJsonObject fields = doc.object().value(QStringLiteral("value")).toObject();

    const QJsonObject field1 = fields.value(QStringLiteral("Field1")).toObject();
    QCOMPARE(field1.value(QStringLiteral("type")).toString(), QStringLiteral("UInt32"));
    QCOMPARE(field1.value(QStringLiteral("value")).toInt(), 10);

    const QJsonObject field2 = fields.value(QStringLiteral("Field2")).toObject();
    QCOMPARE(field2.value(QStringLiteral("type")).toString(), QStringLiteral("String"));
    QCOMPARE(field2.value(QStringLiteral("value")).toString(), QStringLiteral("Test"));

    const QJsonObject field3 = fields.value(QStringLiteral("Field3")).toObject();
    QCOMPARE(field3.value(QStringLiteral("type")).toString(), QStringLiteral("Boolean"));
    QCOMPARE(field3.value(QStringLiteral("value")).toBool(), true);
}

void StructuredValueFormatterTest::jsonArrayRendersElements()
{
    const OpcUaValueTreeNode root = makeArray(QString(), QStringLiteral("Int32"), {
        makeScalar(QString(), QStringLiteral("Int32"), 1),
        makeScalar(QString(), QStringLiteral("Int32"), 2),
        makeScalar(QString(), QStringLiteral("Int32"), 3)
    });

    const QString text =
        StructuredValueFormatter::format(root, StructuredValueFormatter::Format::Json);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);

    const QJsonObject object = doc.object();
    QCOMPARE(object.value(QStringLiteral("valueRank")).toInt(), 1);

    const QJsonArray values = object.value(QStringLiteral("value")).toArray();
    QCOMPARE(values.size(), 3);
    QCOMPARE(values.at(0).toObject().value(QStringLiteral("value")).toInt(), 1);
    QCOMPARE(values.at(2).toObject().value(QStringLiteral("value")).toInt(), 3);
}

void StructuredValueFormatterTest::jsonNestedStructures()
{
    // Struct containing an array field and a nested struct field, plus an
    // array-of-struct field, to cover all nesting combinations at once.
    const OpcUaValueTreeNode root = makeStruct(QString(), QStringLiteral("Outer"), {
        makeArray(QStringLiteral("Numbers"), QStringLiteral("Int32"), {
            makeScalar(QString(), QStringLiteral("Int32"), 7),
            makeScalar(QString(), QStringLiteral("Int32"), 8)
        }),
        makeStruct(QStringLiteral("Inner"), QStringLiteral("InnerType"), {
            makeScalar(QStringLiteral("Flag"), QStringLiteral("Boolean"), false)
        }),
        makeArray(QStringLiteral("Items"), QStringLiteral("ItemType"), {
            makeStruct(QString(), QStringLiteral("ItemType"), {
                makeScalar(QStringLiteral("Id"), QStringLiteral("UInt32"), 42u)
            })
        })
    });

    const QString text =
        StructuredValueFormatter::format(root, StructuredValueFormatter::Format::Json);

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);

    const QJsonObject fields = doc.object().value(QStringLiteral("value")).toObject();

    // Struct of array.
    const QJsonObject numbers = fields.value(QStringLiteral("Numbers")).toObject();
    QCOMPARE(numbers.value(QStringLiteral("valueRank")).toInt(), 1);
    QCOMPARE(numbers.value(QStringLiteral("value")).toArray().size(), 2);

    // Struct of struct.
    const QJsonObject inner =
        fields.value(QStringLiteral("Inner")).toObject().value(QStringLiteral("value")).toObject();
    QCOMPARE(inner.value(QStringLiteral("Flag")).toObject().value(QStringLiteral("value")).toBool(),
             false);

    // Array of struct.
    const QJsonArray items = fields.value(QStringLiteral("Items")).toObject()
        .value(QStringLiteral("value")).toArray();
    QCOMPARE(items.size(), 1);
    const QJsonObject itemFields =
        items.at(0).toObject().value(QStringLiteral("value")).toObject();
    QCOMPARE(itemFields.value(QStringLiteral("Id")).toObject().value(QStringLiteral("value")).toInt(),
             42);
}

void StructuredValueFormatterTest::xmlIsWellFormed()
{
    const OpcUaValueTreeNode root = makeStruct(QString(), QStringLiteral("MyStruct"), {
        makeScalar(QStringLiteral("Field1"), QStringLiteral("UInt32"), 10u),
        makeArray(QStringLiteral("List"), QStringLiteral("Int32"), {
            makeScalar(QString(), QStringLiteral("Int32"), 1)
        })
    });

    const QString text =
        StructuredValueFormatter::format(root, StructuredValueFormatter::Format::Xml);

    QXmlStreamReader reader(text);
    bool sawStruct = false;
    bool sawArray = false;
    bool sawValue = false;
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (name == QLatin1String("Struct")) {
                sawStruct = true;
                QCOMPARE(reader.attributes().value(QLatin1String("type")).toString(),
                         QStringLiteral("MyStruct"));
            } else if (name == QLatin1String("Array")) {
                sawArray = true;
                QCOMPARE(reader.attributes().value(QLatin1String("valueRank")).toString(),
                         QStringLiteral("1"));
            } else if (name == QLatin1String("Value")) {
                sawValue = true;
            }
        }
    }

    QVERIFY(!reader.hasError());
    QVERIFY(sawStruct);
    QVERIFY(sawArray);
    QVERIFY(sawValue);
}

QTEST_MAIN(StructuredValueFormatterTest)
#include "tst_structuredvalueformatter.moc"
