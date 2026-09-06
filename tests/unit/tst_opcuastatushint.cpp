#include <QtTest>

#include "core/opcuastatushint.h"

/*! Verifies the plain-language hints attached to OPC UA status codes. */
class OpcUaStatusHintTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a known status code yields an explanation. */
    void knownCodesProduceAHint();

    /*! Verifies that a status name embedded in a longer message is still matched. */
    void hintIsFoundInsideALongerMessage();

    /*! Verifies that unknown or empty status text yields no hint. */
    void unknownCodesProduceNoHint();

    /*! Verifies that describe() appends the hint and leaves other text alone. */
    void describeAppendsTheHintOnlyWhenOneExists();
};

/*!
 * \brief Verifies that a known status code yields an explanation.
 */
void OpcUaStatusHintTest::knownCodesProduceAHint()
{
    // The codes that dominate real commissioning failures must be covered.
    QVERIFY(!OpcUaStatusHint::hintForStatus(QStringLiteral("BadSecurityChecksFailed")).isEmpty());
    QVERIFY(!OpcUaStatusHint::hintForStatus(QStringLiteral("BadCertificateUriInvalid")).isEmpty());
    QVERIFY(!OpcUaStatusHint::hintForStatus(QStringLiteral("BadUserAccessDenied")).isEmpty());
    QVERIFY(!OpcUaStatusHint::hintForStatus(QStringLiteral("BadTypeMismatch")).isEmpty());
    QVERIFY(!OpcUaStatusHint::hintForStatus(QStringLiteral("BadNodeIdUnknown")).isEmpty());

    // Matching ignores case so a backend that lower-cases the name still matches.
    QCOMPARE(OpcUaStatusHint::hintForStatus(QStringLiteral("badnotwritable")),
             OpcUaStatusHint::hintForStatus(QStringLiteral("BadNotWritable")));
}

/*!
 * \brief Verifies that a status name embedded in a longer message is still matched.
 */
void OpcUaStatusHintTest::hintIsFoundInsideALongerMessage()
{
    const QString backendMessage =
        QStringLiteral("Connect failed: BadIdentityTokenRejected (0x80210000)");

    const QString hint = OpcUaStatusHint::hintForStatus(backendMessage);
    QVERIFY(!hint.isEmpty());
    QCOMPARE(hint, OpcUaStatusHint::hintForStatus(QStringLiteral("BadIdentityTokenRejected")));
}

/*!
 * \brief Verifies that unknown or empty status text yields no hint.
 */
void OpcUaStatusHintTest::unknownCodesProduceNoHint()
{
    // Inventing an explanation for an unrecognised code would mislead, so the
    // helper stays silent instead of guessing.
    QVERIFY(OpcUaStatusHint::hintForStatus(QString()).isEmpty());
    QVERIFY(OpcUaStatusHint::hintForStatus(QStringLiteral("Good")).isEmpty());
    QVERIFY(OpcUaStatusHint::hintForStatus(QStringLiteral("BadSomethingNobodyKnows")).isEmpty());
}

/*!
 * \brief Verifies that describe() appends the hint and leaves other text alone.
 */
void OpcUaStatusHintTest::describeAppendsTheHintOnlyWhenOneExists()
{
    const QString plain = QStringLiteral("Good");
    QCOMPARE(OpcUaStatusHint::describe(plain), plain);

    const QString described = OpcUaStatusHint::describe(QStringLiteral("BadNotWritable"));
    QVERIFY(described.startsWith(QStringLiteral("BadNotWritable")));
    QVERIFY(described.length() > QStringLiteral("BadNotWritable").length());
    QVERIFY(described.contains(OpcUaStatusHint::hintForStatus(QStringLiteral("BadNotWritable"))));
}

QTEST_MAIN(OpcUaStatusHintTest)

#include "tst_opcuastatushint.moc"
