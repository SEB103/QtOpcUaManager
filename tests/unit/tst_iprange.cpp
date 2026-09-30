#include <QtTest>

#include "core/iprange.h"

/*! Verifies range and port parsing and local subnet suggestions for the network scanner. */
class IpRangeTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies CIDR subnets, including network/broadcast exclusion. */
    void parsesCidrSubnets();

    /*! Verifies last-octet and full address ranges. */
    void parsesAddressRanges();

    /*! Verifies a single address and a trailing interface label. */
    void parsesSingleAddressAndIgnoresLabel();

    /*! Verifies that malformed input yields an error and no addresses. */
    void rejectsInvalidRanges_data();
    void rejectsInvalidRanges();

    /*! Verifies the 1024-address limit. */
    void enforcesAddressLimit();

    /*! Verifies port lists, ranges, and duplicate removal. */
    void parsesPorts();

    /*! Verifies invalid ports and the 16-port limit. */
    void rejectsInvalidPorts_data();
    void rejectsInvalidPorts();

    /*! Verifies that parsing stops as soon as the port list exceeds the limit. */
    void stopsParsingPortsAtTheLimit();

    /*! Verifies subnet suggestions derived from interface addresses. */
    void suggestsSubnetsFromInterfaces();
};

/*!
 * \brief Verifies CIDR subnets, including network/broadcast exclusion.
 */
void IpRangeTest::parsesCidrSubnets()
{
    const IpRange::AddressList subnet = IpRange::parseRange(QStringLiteral("10.10.1.0/24"));
    QVERIFY(subnet.error.isEmpty());
    QCOMPARE(subnet.addresses.size(), 254);
    QCOMPARE(subnet.addresses.first(), QHostAddress(QStringLiteral("10.10.1.1")));
    QCOMPARE(subnet.addresses.last(), QHostAddress(QStringLiteral("10.10.1.254")));

    // A host address inside the subnet selects the same network.
    const IpRange::AddressList unaligned = IpRange::parseRange(QStringLiteral("10.10.1.77/24"));
    QCOMPARE(unaligned.addresses, subnet.addresses);

    // /32 is a single host; nothing is excluded above /30.
    const IpRange::AddressList host = IpRange::parseRange(QStringLiteral("10.10.1.2/32"));
    QCOMPARE(host.addresses, QList<QHostAddress>{QHostAddress(QStringLiteral("10.10.1.2"))});
}

/*!
 * \brief Verifies last-octet and full address ranges.
 */
void IpRangeTest::parsesAddressRanges()
{
    const IpRange::AddressList octet = IpRange::parseRange(QStringLiteral("10.10.1.5-10"));
    QVERIFY(octet.error.isEmpty());
    QCOMPARE(octet.addresses.size(), 6);
    QCOMPARE(octet.addresses.first(), QHostAddress(QStringLiteral("10.10.1.5")));
    QCOMPARE(octet.addresses.last(), QHostAddress(QStringLiteral("10.10.1.10")));

    QCOMPARE(IpRange::parseRange(QStringLiteral("10.10.1.1-254")).addresses.size(), 254);

    const IpRange::AddressList full =
        IpRange::parseRange(QStringLiteral("10.10.1.250 - 10.10.2.5"));
    QVERIFY(full.error.isEmpty());
    QCOMPARE(full.addresses.size(), 12);
    QCOMPARE(full.addresses.last(), QHostAddress(QStringLiteral("10.10.2.5")));
}

/*!
 * \brief Verifies a single address and a trailing interface label.
 */
void IpRangeTest::parsesSingleAddressAndIgnoresLabel()
{
    QCOMPARE(IpRange::parseRange(QStringLiteral("10.10.1.2")).addresses,
             QList<QHostAddress>{QHostAddress(QStringLiteral("10.10.1.2"))});

    const IpRange::AddressList labelled =
        IpRange::parseRange(QStringLiteral("10.10.1.0/24 (Ethernet 3)"));
    QVERIFY(labelled.error.isEmpty());
    QCOMPARE(labelled.addresses.size(), 254);
}

void IpRangeTest::rejectsInvalidRanges_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("empty") << QString();
    QTest::newRow("text") << QStringLiteral("abc");
    QTest::newRow("three octets") << QStringLiteral("10.10.1");
    QTest::newRow("octet overflow") << QStringLiteral("10.10.1.300");
    QTest::newRow("reversed") << QStringLiteral("10.10.1.10-5");
    QTest::newRow("end octet overflow") << QStringLiteral("10.10.1.1-300");
    QTest::newRow("ipv6") << QStringLiteral("fe80::1/64");
    QTest::newRow("bad prefix") << QStringLiteral("10.10.1.0/33");
    QTest::newRow("host name") << QStringLiteral("AOPT690");
}

/*!
 * \brief Verifies that malformed input yields an error and no addresses.
 */
void IpRangeTest::rejectsInvalidRanges()
{
    QFETCH(QString, input);
    const IpRange::AddressList result = IpRange::parseRange(input);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.addresses.isEmpty());
}

/*!
 * \brief Verifies the 1024-address limit.
 */
void IpRangeTest::enforcesAddressLimit()
{
    QCOMPARE(IpRange::parseRange(QStringLiteral("10.0.0.0/22")).addresses.size(), 1022);

    const IpRange::AddressList tooLarge = IpRange::parseRange(QStringLiteral("10.0.0.0/16"));
    QVERIFY(tooLarge.addresses.isEmpty());
    QVERIFY(tooLarge.error.contains(QStringLiteral("65534")));
    QVERIFY(tooLarge.error.contains(QStringLiteral("1024")));
}

/*!
 * \brief Verifies port lists, ranges, and duplicate removal.
 */
void IpRangeTest::parsesPorts()
{
    QCOMPARE(IpRange::parsePorts(QStringLiteral("4840")).ports, QList<quint16>{4840});

    const IpRange::PortList list = IpRange::parsePorts(QStringLiteral("4840, 4841, 48010-48012"));
    QVERIFY(list.error.isEmpty());
    QCOMPARE(list.ports, (QList<quint16>{4840, 4841, 48010, 48011, 48012}));

    QCOMPARE(IpRange::parsePorts(QStringLiteral("4840,4840")).ports, QList<quint16>{4840});

    // Exactly kMaxPorts ports is still accepted.
    const IpRange::PortList limit = IpRange::parsePorts(QStringLiteral("4840-4855"));
    QVERIFY(limit.error.isEmpty());
    QCOMPARE(limit.ports.size(), 16);
}

void IpRangeTest::rejectsInvalidPorts_data()
{
    QTest::addColumn<QString>("input");
    QTest::newRow("empty") << QString();
    QTest::newRow("zero") << QStringLiteral("0");
    QTest::newRow("too high") << QStringLiteral("65536");
    QTest::newRow("text") << QStringLiteral("opc");
    QTest::newRow("reversed") << QStringLiteral("10-5");
    QTest::newRow("too many") << QStringLiteral("4840-4900");
    QTest::newRow("full range") << QStringLiteral("1-65535");
    QTest::newRow("seventeen ports") << QStringLiteral("4840-4856");
    QTest::newRow("seventeen single ports")
        << QStringLiteral("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17");
}

/*!
 * \brief Verifies invalid ports and the 16-port limit.
 */
void IpRangeTest::rejectsInvalidPorts()
{
    QFETCH(QString, input);
    const IpRange::PortList result = IpRange::parsePorts(input);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.ports.isEmpty());
}

/*!
 * \brief Verifies that parsing stops as soon as the port list exceeds the limit.
 *
 * The part after the seventeenth port is never examined, so the limit error is
 * reported instead of the invalid trailing part.
 */
void IpRangeTest::stopsParsingPortsAtTheLimit()
{
    const IpRange::PortList result = IpRange::parsePorts(
        QStringLiteral("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,opc"));
    QVERIFY(result.ports.isEmpty());
    QCOMPARE(result.error,
             QStringLiteral("Too many ports (17); the maximum is %1.").arg(IpRange::kMaxPorts));
}

/*!
 * \brief Verifies subnet suggestions derived from interface addresses.
 */
void IpRangeTest::suggestsSubnetsFromInterfaces()
{
    const QList<IpRange::InterfaceAddress> entries {
        {QStringLiteral("Ethernet 3"), QHostAddress(QStringLiteral("10.10.1.210")), 24},
        {QStringLiteral("WLAN"), QHostAddress(QStringLiteral("192.168.178.49")), 24},
        {QStringLiteral("VPN"), QHostAddress(QStringLiteral("172.16.5.9")), 16},
        {QStringLiteral("Loopback"), QHostAddress(QStringLiteral("127.0.0.1")), 8},
        {QStringLiteral("APIPA"), QHostAddress(QStringLiteral("169.254.3.4")), 16},
        {QStringLiteral("IPv6"), QHostAddress(QStringLiteral("fe80::1")), 64},
        {QStringLiteral("Unknown prefix"), QHostAddress(QStringLiteral("10.20.0.1")), -1},
        {QStringLiteral("Second NIC"), QHostAddress(QStringLiteral("10.10.1.211")), 24},
    };

    const QList<IpRange::Subnet> subnets = IpRange::subnetsFromInterfaces(entries);

    QCOMPARE(subnets.size(), 3);
    QCOMPARE(subnets.at(0).cidr, QStringLiteral("10.10.1.0/24"));
    QCOMPARE(subnets.at(0).label, QStringLiteral("10.10.1.0/24 (Ethernet 3)"));
    QCOMPARE(subnets.at(1).cidr, QStringLiteral("192.168.178.0/24"));
    // A /16 is narrowed to the /24 around the PC's own address.
    QCOMPARE(subnets.at(2).cidr, QStringLiteral("172.16.5.0/24"));
}

QTEST_GUILESS_MAIN(IpRangeTest)

#include "tst_iprange.moc"
