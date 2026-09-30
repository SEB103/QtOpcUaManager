#include <QSignalSpy>
#include <QtTest>

#include "models/networkscanmodel.h"

namespace {

/*!
 * \internal
 * \brief Returns a result row for \a address and \a port with \a status.
 */
NetworkScanResult makeResult(const QString &address, quint16 port,
                             NetworkScanStatus status = NetworkScanStatus::PortOpen)
{
    NetworkScanResult result;
    result.address = address;
    result.port = port;
    result.responseMs = 3;
    result.status = status;
    return result;
}

} // namespace

/*! Verifies the network scan result table model. */
class NetworkScanModelTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies the role names QML delegates rely on. */
    void exposesRoleNames();

    /*! Verifies that rows are kept in numeric address order, then port order. */
    void insertsRowsInAddressOrder();

    /*! Verifies that a result for an existing address and port updates the row in place. */
    void updatesExistingRow();

    /*! Verifies clearing and out-of-range access. */
    void clearsAndHandlesInvalidIndexes();
};

/*!
 * \brief Verifies the role names QML delegates rely on.
 */
void NetworkScanModelTest::exposesRoleNames()
{
    NetworkScanModel model;
    const QList<QByteArray> names = model.roleNames().values();
    for (const char *name : {"address", "port", "url", "applicationName", "applicationUri",
                             "productUri", "securitySummary", "authSummary", "responseMs",
                             "status", "errorText"}) {
        QVERIFY2(names.contains(QByteArray(name)), name);
    }
}

/*!
 * \brief Verifies that rows are kept in numeric address order, then port order.
 */
void NetworkScanModelTest::insertsRowsInAddressOrder()
{
    NetworkScanModel model;
    QSignalSpy countSpy(&model, &NetworkScanModel::countChanged);
    model.upsert(makeResult(QStringLiteral("10.10.1.20"), 4840));
    model.upsert(makeResult(QStringLiteral("10.10.1.3"), 4841));
    model.upsert(makeResult(QStringLiteral("10.10.1.3"), 4840));

    QCOMPARE(model.count(), 3);
    QCOMPARE(countSpy.size(), 3);
    // Numeric order: .3 comes before .20, and port 4840 before 4841.
    QCOMPARE(model.resultAt(0).url(), QStringLiteral("opc.tcp://10.10.1.3:4840"));
    QCOMPARE(model.resultAt(1).url(), QStringLiteral("opc.tcp://10.10.1.3:4841"));
    QCOMPARE(model.resultAt(2).url(), QStringLiteral("opc.tcp://10.10.1.20:4840"));
    QCOMPARE(model.data(model.index(0), NetworkScanModel::UrlRole).toString(),
             QStringLiteral("opc.tcp://10.10.1.3:4840"));
    QCOMPARE(model.data(model.index(0), NetworkScanModel::PortRole).toInt(), 4840);
}

/*!
 * \brief Verifies that a result for an existing address and port updates the row in place.
 */
void NetworkScanModelTest::updatesExistingRow()
{
    NetworkScanModel model;
    model.upsert(makeResult(QStringLiteral("10.10.1.2"), 4840));
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

    NetworkScanResult server = makeResult(QStringLiteral("10.10.1.2"), 4840, NetworkScanStatus::OpcUa);
    server.applicationName = QStringLiteral("OPCUAServer@AOPT690");
    server.securitySummary = QStringLiteral("None");
    server.authSummary = QStringLiteral("Anonymous, Username");
    model.upsert(server);

    QCOMPARE(model.count(), 1);
    QCOMPARE(changedSpy.size(), 1);
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, NetworkScanModel::StatusRole).toInt(), 1);
    QCOMPARE(model.data(row, NetworkScanModel::ApplicationNameRole).toString(),
             QStringLiteral("OPCUAServer@AOPT690"));
    QCOMPARE(model.data(row, NetworkScanModel::AuthSummaryRole).toString(),
             QStringLiteral("Anonymous, Username"));
}

/*!
 * \brief Verifies clearing and out-of-range access.
 */
void NetworkScanModelTest::clearsAndHandlesInvalidIndexes()
{
    NetworkScanModel model;
    model.upsert(makeResult(QStringLiteral("10.10.1.2"), 4840));
    QVERIFY(!model.data(model.index(5), NetworkScanModel::AddressRole).isValid());
    QCOMPARE(model.resultAt(5).port, quint16(0));

    QSignalSpy countSpy(&model, &NetworkScanModel::countChanged);
    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(countSpy.size(), 1);
}

QTEST_GUILESS_MAIN(NetworkScanModelTest)

#include "tst_networkscanmodel.moc"
