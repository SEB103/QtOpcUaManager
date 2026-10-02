#include <QDateTime>
#include <QTimeZone>
#include <QtTest>

#include "core/opcuavaluetext.h"

/*! Verifies the value kind classification and array element texts. */
class OpcUaValueTextTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Provides values and the kind each one must be classified as. */
    void valueKindClassifiesValues_data();

    /*! Verifies that opcUaValueKind() classifies scalars and arrays. */
    void valueKindClassifiesValues();

    /*! Verifies that opcUaArrayElementTexts() lists array elements only. */
    void arrayElementTextsListElements();
};

/*!
 * \brief Provides values and the kind each one must be classified as.
 */
void OpcUaValueTextTest::valueKindClassifiesValues_data()
{
    QTest::addColumn<QVariant>("value");
    QTest::addColumn<OpcUaValueKind>("kind");

    QTest::newRow("invalid") << QVariant() << OpcUaValueKind::Empty;
    QTest::newRow("empty string") << QVariant(QString()) << OpcUaValueKind::Empty;
    QTest::newRow("bool") << QVariant(true) << OpcUaValueKind::Boolean;
    QTest::newRow("int") << QVariant(int(-7)) << OpcUaValueKind::Number;
    QTest::newRow("uint") << QVariant(uint(7)) << OpcUaValueKind::Number;
    QTest::newRow("int64") << QVariant(qint64(1) << 40) << OpcUaValueKind::Number;
    QTest::newRow("uint64") << QVariant(quint64(1) << 40) << OpcUaValueKind::Number;
    QTest::newRow("int16") << QVariant::fromValue(qint16(-3)) << OpcUaValueKind::Number;
    QTest::newRow("byte") << QVariant::fromValue(quint8(3)) << OpcUaValueKind::Number;
    QTest::newRow("sbyte") << QVariant::fromValue(qint8(-3)) << OpcUaValueKind::Number;
    QTest::newRow("float") << QVariant(1.5f) << OpcUaValueKind::Number;
    QTest::newRow("double") << QVariant(2.25) << OpcUaValueKind::Number;
    QTest::newRow("string") << QVariant(QStringLiteral("RUNNING")) << OpcUaValueKind::Text;
    QTest::newRow("numeric string") << QVariant(QStringLiteral("42")) << OpcUaValueKind::Text;
    QTest::newRow("date time")
        << QVariant(QDateTime(QDate(2026, 10, 2), QTime(12, 0), QTimeZone::UTC))
        << OpcUaValueKind::Text;
    QTest::newRow("int array") << QVariant(QVariantList{1, 2, 3}) << OpcUaValueKind::Number;
    QTest::newRow("bool array") << QVariant(QVariantList{true, false})
                                << OpcUaValueKind::Boolean;
    QTest::newRow("string array")
        << QVariant(QVariantList{QStringLiteral("a"), QStringLiteral("b")})
        << OpcUaValueKind::Text;
    QTest::newRow("array with leading empty")
        << QVariant(QVariantList{QVariant(), 4}) << OpcUaValueKind::Number;
    QTest::newRow("empty array") << QVariant(QVariantList{}) << OpcUaValueKind::Empty;
}

/*!
 * \brief Verifies that opcUaValueKind() classifies scalars and arrays.
 */
void OpcUaValueTextTest::valueKindClassifiesValues()
{
    QFETCH(QVariant, value);
    QFETCH(OpcUaValueKind, kind);

    QCOMPARE(opcUaValueKind(value), kind);
}

/*!
 * \brief Verifies that opcUaArrayElementTexts() lists array elements only.
 *
 * A string must not be split into characters, and a scalar or an empty array
 * yields no elements, so the caller can treat a non-empty result as an array.
 */
void OpcUaValueTextTest::arrayElementTextsListElements()
{
    QCOMPARE(opcUaArrayElementTexts(QVariant(QVariantList{1, 2.5, true})),
             (QStringList{QStringLiteral("1"), QStringLiteral("2.5"), QStringLiteral("true")}));
    QVERIFY(opcUaArrayElementTexts(QVariant(QStringLiteral("a, b"))).isEmpty());
    QVERIFY(opcUaArrayElementTexts(QVariant(42)).isEmpty());
    QVERIFY(opcUaArrayElementTexts(QVariant(QVariantList{})).isEmpty());
    QVERIFY(opcUaArrayElementTexts(QVariant()).isEmpty());
}

QTEST_MAIN(OpcUaValueTextTest)

#include "tst_opcuavaluetext.moc"
