#include <QSettings>
#include <QTcpServer>
#include <QtTest>

#include "models/networkscanmodel.h"
#include "qmlapi/networkscancontroller.h"

/*! Verifies the QML facade of the network scanner. */
class NetworkScanControllerTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Isolates QSettings for this test. */
    void initTestCase();

    /*! Removes the persisted scanner input before each test. */
    void init();

    /*! Verifies validation texts for ranges and ports. */
    void validatesInput();

    /*! Verifies that invalid input neither starts a scan nor changes the state. */
    void invalidInputDoesNotStart();

    /*! Verifies that a started scan finishes and its input is persisted. */
    void startFinishesAndPersistsInput();

    /*! Removes the test settings. */
    void cleanupTestCase();
};

void NetworkScanControllerTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("OpcUaManagerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_networkscancontroller"));
}

void NetworkScanControllerTest::init()
{
    QSettings().remove(QStringLiteral("scanner"));
}

/*!
 * \brief Verifies validation texts for ranges and ports.
 */
void NetworkScanControllerTest::validatesInput()
{
    NetworkScanController controller;
    QVERIFY(controller.validateRange(QStringLiteral("10.10.1.0/24")).isEmpty());
    QVERIFY(controller.validateRange(QStringLiteral("10.10.1.0/24 (Ethernet 3)")).isEmpty());
    QVERIFY(!controller.validateRange(QStringLiteral("nope")).isEmpty());
    QVERIFY(controller.validatePorts(QStringLiteral("4840")).isEmpty());
    QVERIFY(!controller.validatePorts(QStringLiteral("0")).isEmpty());
    QCOMPARE(controller.lastPorts(), QStringLiteral("4840"));
}

/*!
 * \brief Verifies that invalid input neither starts a scan nor changes the state.
 */
void NetworkScanControllerTest::invalidInputDoesNotStart()
{
    NetworkScanController controller;
    QVERIFY(!controller.start(QStringLiteral("nope"), QStringLiteral("4840")));
    QVERIFY(!controller.start(QStringLiteral("127.0.0.1"), QStringLiteral("0")));
    QCOMPARE(controller.state(), 0);
    QVERIFY(controller.lastRange().isEmpty());
}

/*!
 * \brief Verifies that a started scan finishes and its input is persisted.
 */
void NetworkScanControllerTest::startFinishesAndPersistsInput()
{
    QTcpServer reserve;
    QVERIFY(reserve.listen(QHostAddress::LocalHost, 0));
    const QString closedPort = QString::number(reserve.serverPort());
    reserve.close();

    NetworkScanController controller;
    QVERIFY(controller.start(QStringLiteral("127.0.0.1"), closedPort));
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), 2, 5000);
    QCOMPARE(controller.model()->count(), 0);
    QCOMPARE(controller.totalTargets(), 1);
    QCOMPARE(controller.lastRange(), QStringLiteral("127.0.0.1"));
    QCOMPARE(controller.lastPorts(), closedPort);

    NetworkScanController reloaded;
    QCOMPARE(reloaded.lastRange(), QStringLiteral("127.0.0.1"));
    QCOMPARE(reloaded.lastPorts(), closedPort);
}

void NetworkScanControllerTest::cleanupTestCase()
{
    QSettings().remove(QStringLiteral("scanner"));
}

QTEST_GUILESS_MAIN(NetworkScanControllerTest)

#include "tst_networkscancontroller.moc"
