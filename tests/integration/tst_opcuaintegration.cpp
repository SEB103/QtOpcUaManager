#include <QEventLoop>
#include <QOpcUaClient>
#include <QOpcUaProvider>
#include <QTimer>
#include <QUrl>
#include <QtTest>

#include <memory>

/*!
 * \brief Exercises Qt OPC UA endpoint discovery against an external server.
 *
 * The test is skipped unless \c OPCUAMANAGER_TEST_SERVER_URL is set. It is an
 * integration check for the local Qt OPC UA backend rather than a deterministic
 * unit test.
 */
class OpcUaIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Requests endpoints from the server configured by \c OPCUAMANAGER_TEST_SERVER_URL. */
    void requestEndpointsFromExternalServer();
};

void OpcUaIntegrationTest::requestEndpointsFromExternalServer()
{
    const QString urlText = qEnvironmentVariable("OPCUAMANAGER_TEST_SERVER_URL").trimmed();
    if (urlText.isEmpty())
        QSKIP("Set OPCUAMANAGER_TEST_SERVER_URL to run the external OPC UA integration test.");

    const QUrl url(urlText);
    QVERIFY2(url.isValid(), qPrintable(QStringLiteral("Invalid test URL: %1").arg(urlText)));

    QOpcUaProvider provider;
    const QStringList backends = provider.availableBackends();
    QVERIFY2(!backends.isEmpty(), "No Qt OPC UA backend is available.");

    std::unique_ptr<QOpcUaClient> client(provider.createClient(backends.first()));
    QVERIFY(client);

    bool completed = false;
    QList<QOpcUaEndpointDescription> receivedEndpoints;
    QOpcUa::UaStatusCode receivedStatus = QOpcUa::UaStatusCode::BadUnexpectedError;

    QEventLoop eventLoop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(10000);
    connect(&timeout, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    connect(client.get(),
            &QOpcUaClient::endpointsRequestFinished,
            &eventLoop,
            [&](const QList<QOpcUaEndpointDescription> &endpoints,
                QOpcUa::UaStatusCode statusCode,
                const QUrl &) {
                completed = true;
                receivedEndpoints = endpoints;
                receivedStatus = statusCode;
                eventLoop.quit();
            });

    QVERIFY(client->requestEndpoints(url));
    timeout.start();
    eventLoop.exec();

    QVERIFY2(completed, "GetEndpoints did not complete before the integration-test timeout.");
    QCOMPARE(receivedStatus, QOpcUa::UaStatusCode::Good);
    QVERIFY(!receivedEndpoints.isEmpty());
}

QTEST_GUILESS_MAIN(OpcUaIntegrationTest)

#include "tst_opcuaintegration.moc"
