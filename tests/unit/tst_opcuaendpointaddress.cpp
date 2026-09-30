#include <QtTest>

#include <QOpcUaEndpointDescription>
#include <QOpcUaUserTokenPolicy>

#include "core/opcuaendpointaddress.h"

namespace {

/*!
 * \internal
 * \brief Returns an endpoint description with the given URL, policy suffix, and token types.
 */
QOpcUaEndpointDescription makeEndpoint(const QString &url,
                                       const QString &policySuffix = QStringLiteral("None"),
                                       QOpcUaEndpointDescription::MessageSecurityMode mode
                                       = QOpcUaEndpointDescription::MessageSecurityMode::None,
                                       const QList<QOpcUaUserTokenPolicy::TokenType> &tokenTypes
                                       = {QOpcUaUserTokenPolicy::TokenType::Anonymous})
{
    QOpcUaEndpointDescription endpoint;
    endpoint.setEndpointUrl(url);
    endpoint.setSecurityPolicy(QStringLiteral("http://opcfoundation.org/UA/SecurityPolicy#")
                               + policySuffix);
    endpoint.setSecurityMode(mode);
    QList<QOpcUaUserTokenPolicy> tokens;
    for (const auto type : tokenTypes) {
        QOpcUaUserTokenPolicy token;
        token.setTokenType(type);
        tokens << token;
    }
    endpoint.setUserIdentityTokens(tokens);
    return endpoint;
}

} // namespace

/*!
 * \brief Verifies that discovery and endpoint URLs are redirected to the host that answered.
 *
 * Remote servers such as PLC panels often advertise their own host name, which the
 * client cannot resolve. These tests pin the rule that the client keeps talking to
 * the host it actually reached.
 */
class OpcUaEndpointAddressTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that an advertised host name is replaced by the reached IP address. */
    void advertisedHostIsReplacedByReachedHost();

    /*! Verifies that the advertised port and path are preserved. */
    void advertisedPortAndPathArePreserved();

    /*! Verifies that a matching host, compared case-insensitively, is left unchanged. */
    void matchingHostIsLeftUnchanged();

    /*! Verifies the fallbacks for invalid advertised or reached URLs. */
    void invalidInputsFallBack();

    /*! Verifies that endpoints are moved to the reached host and duplicates are removed. */
    void endpointsAreRedirectedAndDeduplicated();

    /*! Verifies that endpoints differing in security or tokens are all kept. */
    void distinctEndpointsAreKept();

    /*! Verifies host literal detection used for the resolution hint. */
    void hostLiteralDetection();
};

/*!
 * \brief Verifies that an advertised host name is replaced by the reached IP address.
 */
void OpcUaEndpointAddressTest::advertisedHostIsReplacedByReachedHost()
{
    // Captured from a WAGO panel: FindServers on the IP returns its host name.
    const QUrl reached(QStringLiteral("opc.tcp://10.10.1.2:4840"));
    const QUrl advertised(QStringLiteral("opc.tcp://AOPT690:4840"));

    QCOMPARE(OpcUaEndpointAddress::reachableUrl(advertised, reached),
             QUrl(QStringLiteral("opc.tcp://10.10.1.2:4840")));
}

/*!
 * \brief Verifies that the advertised port and path are preserved.
 */
void OpcUaEndpointAddressTest::advertisedPortAndPathArePreserved()
{
    const QUrl reached(QStringLiteral("opc.tcp://10.10.1.2:4840"));
    const QUrl advertised(QStringLiteral("opc.tcp://plc-hall-3:48010/UA/Server"));

    QCOMPARE(OpcUaEndpointAddress::reachableUrl(advertised, reached),
             QUrl(QStringLiteral("opc.tcp://10.10.1.2:48010/UA/Server")));
}

/*!
 * \brief Verifies that a matching host, compared case-insensitively, is left unchanged.
 */
void OpcUaEndpointAddressTest::matchingHostIsLeftUnchanged()
{
    const QUrl reached(QStringLiteral("opc.tcp://aopt690:4840"));
    const QUrl advertised(QStringLiteral("opc.tcp://AOPT690:4841"));

    QCOMPARE(OpcUaEndpointAddress::reachableUrl(advertised, reached), advertised);
}

/*!
 * \brief Verifies the fallbacks for invalid advertised or reached URLs.
 */
void OpcUaEndpointAddressTest::invalidInputsFallBack()
{
    const QUrl reached(QStringLiteral("opc.tcp://10.10.1.2:4840"));
    const QUrl advertised(QStringLiteral("opc.tcp://AOPT690:4840"));

    // Nothing usable was advertised: use the reached URL.
    QCOMPARE(OpcUaEndpointAddress::reachableUrl(QUrl(), reached), reached);
    // The reached URL is unknown: keep the advertised URL.
    QCOMPARE(OpcUaEndpointAddress::reachableUrl(advertised, QUrl()), advertised);
}

/*!
 * \brief Verifies that endpoints are moved to the reached host and duplicates are removed.
 */
void OpcUaEndpointAddressTest::endpointsAreRedirectedAndDeduplicated()
{
    // Captured from a WAGO panel: the same endpoint is offered on the host name and the IP.
    const QUrl reached(QStringLiteral("opc.tcp://10.10.1.2:4840"));
    const QList<QOpcUaEndpointDescription> endpoints {
        makeEndpoint(QStringLiteral("opc.tcp://AOPT690:4840")),
        makeEndpoint(QStringLiteral("opc.tcp://10.10.1.2:4840")),
    };

    const auto adapted = OpcUaEndpointAddress::reachableEndpoints(endpoints, reached);

    QCOMPARE(adapted.size(), 1);
    QCOMPARE(adapted.first().endpointUrl(), QStringLiteral("opc.tcp://10.10.1.2:4840"));
}

/*!
 * \brief Verifies that endpoints differing in security or tokens are all kept.
 */
void OpcUaEndpointAddressTest::distinctEndpointsAreKept()
{
    const QUrl reached(QStringLiteral("opc.tcp://10.10.1.2:4840"));
    const QList<QOpcUaEndpointDescription> endpoints {
        makeEndpoint(QStringLiteral("opc.tcp://AOPT690:4840")),
        makeEndpoint(QStringLiteral("opc.tcp://AOPT690:4840"),
                     QStringLiteral("Basic256Sha256"),
                     QOpcUaEndpointDescription::MessageSecurityMode::SignAndEncrypt),
        makeEndpoint(QStringLiteral("opc.tcp://AOPT690:4840"),
                     QStringLiteral("None"),
                     QOpcUaEndpointDescription::MessageSecurityMode::None,
                     {QOpcUaUserTokenPolicy::TokenType::Username}),
    };

    const auto adapted = OpcUaEndpointAddress::reachableEndpoints(endpoints, reached);

    QCOMPARE(adapted.size(), 3);
    for (const auto &endpoint : adapted)
        QCOMPARE(QUrl(endpoint.endpointUrl()).host(), QStringLiteral("10.10.1.2"));
    // The original order is preserved.
    QVERIFY(adapted.at(1).securityPolicy().endsWith(QStringLiteral("#Basic256Sha256")));
}

/*!
 * \brief Verifies host literal detection used for the resolution hint.
 */
void OpcUaEndpointAddressTest::hostLiteralDetection()
{
    QVERIFY(OpcUaEndpointAddress::isHostName(QStringLiteral("AOPT690")));
    QVERIFY(OpcUaEndpointAddress::isHostName(QStringLiteral("plc.example.local")));
    QVERIFY(!OpcUaEndpointAddress::isHostName(QStringLiteral("10.10.1.2")));
    QVERIFY(!OpcUaEndpointAddress::isHostName(QStringLiteral("::1")));
    QVERIFY(!OpcUaEndpointAddress::isHostName(QStringLiteral("localhost")));
    QVERIFY(!OpcUaEndpointAddress::isHostName(QString()));
}

QTEST_GUILESS_MAIN(OpcUaEndpointAddressTest)

#include "tst_opcuaendpointaddress.moc"
