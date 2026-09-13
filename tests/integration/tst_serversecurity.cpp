// Integration tests for the Security Test Lab: the runtime enforces the
// project's authentication policy and advertises encrypted endpoints. Both
// launch the runtime as a fixture and use a real client.

#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaProvider>

#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectserializer.h"

#ifndef OPCUA_SERVER_RUNTIME_PATH
#define OPCUA_SERVER_RUNTIME_PATH ""
#endif

using namespace ServerProject;

/*! Verifies authentication enforcement and secure-endpoint advertisement. */
class ServerSecurityTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Skips the suite when the runtime or backend is unavailable. */
    void initTestCase();

    /*! A server that disallows anonymous rejects an anonymous session. */
    void anonymousRejected();

    /*! A server with encryption enabled advertises a Basic256Sha256 endpoint. */
    void secureEndpointAdvertised();

    /*!
     * With acceptAllClientCerts disabled the runtime builds a real (empty) trust
     * list, still starts, and advertises the secure endpoint. This exercises the
     * strict trust-list configuration path.
     */
    void strictTrustModeStartsAndAdvertises();

    /*! Stops any running runtime after each test. */
    void cleanup();

private:
    /*! Saves \a project, launches the runtime on \a port and waits for READY. */
    bool launch(const ProjectData &project, quint16 port);

    /*! Stops the runtime process if it is running. */
    void stopRuntime();

    QProcess m_process;
    QTemporaryDir m_dir;
    QString m_runtimePath;
    QOpcUaProvider m_provider;
    bool m_available = false;
};

void ServerSecurityTest::initTestCase()
{
    m_runtimePath = QStringLiteral(OPCUA_SERVER_RUNTIME_PATH);
    m_available = !m_runtimePath.isEmpty() && QFileInfo::exists(m_runtimePath)
                  && m_provider.availableBackends().contains(QStringLiteral("open62541"))
                  && m_dir.isValid();
    if (!m_available)
        QSKIP("OpcUaServerRuntime or open62541 client backend is not available.");
}

bool ServerSecurityTest::launch(const ProjectData &project, quint16 port)
{
    const QString projectPath =
        m_dir.filePath(QStringLiteral("sec-%1.uaserver").arg(port));
    const Serializer::SaveResult saved = Serializer::save(projectPath, project);
    if (!saved.ok)
        return false;

    const QString pkiDir = m_dir.filePath(QStringLiteral("pki-%1").arg(port));
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start(m_runtimePath,
                    {QStringLiteral("--project"), projectPath,
                     QStringLiteral("--port"), QString::number(port),
                     QStringLiteral("--pki"), pkiDir},
                    QIODevice::ReadWrite | QIODevice::Text);
    if (!m_process.waitForStarted(5000))
        return false;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 15000) {
        if (m_process.waitForReadyRead(1000)) {
            if (QString::fromUtf8(m_process.readAllStandardOutput())
                    .contains(QLatin1String("READY endpoint=")))
                return true;
        }
        if (m_process.state() == QProcess::NotRunning)
            return false;
    }
    return false;
}

void ServerSecurityTest::stopRuntime()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.write("STOP\n");
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
}

void ServerSecurityTest::anonymousRejected()
{
    ProjectData project;
    project.security.allowAnonymous = false;
    project.security.allowNone = true;
    project.security.enableSecurity = false;
    project.security.users.append({QStringLiteral("admin"), QStringLiteral("secret")});

    const quint16 port = 48411;
    QVERIFY2(launch(project, port), "runtime did not become ready");

    std::unique_ptr<QOpcUaClient> client(m_provider.createClient(QStringLiteral("open62541")));
    QVERIFY(client);
    const QString url = QStringLiteral("opc.tcp://127.0.0.1:%1").arg(port);

    QSignalSpy endpointsSpy(client.get(), &QOpcUaClient::endpointsRequestFinished);
    client->requestEndpoints(QUrl(url));
    QVERIFY2(endpointsSpy.wait(10000), "no endpoints response");
    const auto endpoints =
        endpointsSpy.takeFirst().at(0).value<QList<QOpcUaEndpointDescription>>();
    QVERIFY(!endpoints.isEmpty());

    // Connect with the default anonymous identity: the server must not grant a
    // session, so the client never reaches the Connected state.
    client->connectToEndpoint(endpoints.first());
    QTest::qWait(4000);
    QVERIFY2(client->state() != QOpcUaClient::ClientState::Connected,
             "anonymous session was unexpectedly accepted");

    if (client->state() == QOpcUaClient::ClientState::Connected)
        client->disconnectFromEndpoint();
}

void ServerSecurityTest::secureEndpointAdvertised()
{
    ProjectData project;
    project.security.allowAnonymous = true;
    project.security.allowNone = true;
    project.security.enableSecurity = true;

    const quint16 port = 48412;
    QVERIFY2(launch(project, port), "runtime did not become ready");

    std::unique_ptr<QOpcUaClient> client(m_provider.createClient(QStringLiteral("open62541")));
    QVERIFY(client);
    const QString url = QStringLiteral("opc.tcp://127.0.0.1:%1").arg(port);

    QSignalSpy endpointsSpy(client.get(), &QOpcUaClient::endpointsRequestFinished);
    client->requestEndpoints(QUrl(url));
    QVERIFY2(endpointsSpy.wait(10000), "no endpoints response");
    const auto endpoints =
        endpointsSpy.takeFirst().at(0).value<QList<QOpcUaEndpointDescription>>();

    bool foundSecure = false;
    for (const QOpcUaEndpointDescription &endpoint : endpoints) {
        if (endpoint.securityPolicy().contains(QLatin1String("Basic256Sha256"))) {
            foundSecure = true;
            break;
        }
    }
    QVERIFY2(foundSecure, "no Basic256Sha256 endpoint was advertised");
}

void ServerSecurityTest::strictTrustModeStartsAndAdvertises()
{
    ProjectData project;
    project.security.allowAnonymous = true;
    project.security.allowNone = true;
    project.security.enableSecurity = true;
    // Strict mode: the runtime enforces a real trust list from the server PKI
    // instead of accepting every client certificate.
    project.security.acceptAllClientCerts = false;

    const quint16 port = 48413;
    QVERIFY2(launch(project, port), "runtime did not become ready in strict trust mode");

    std::unique_ptr<QOpcUaClient> client(m_provider.createClient(QStringLiteral("open62541")));
    QVERIFY(client);
    const QString url = QStringLiteral("opc.tcp://127.0.0.1:%1").arg(port);

    QSignalSpy endpointsSpy(client.get(), &QOpcUaClient::endpointsRequestFinished);
    client->requestEndpoints(QUrl(url));
    QVERIFY2(endpointsSpy.wait(10000), "no endpoints response");
    const auto endpoints =
        endpointsSpy.takeFirst().at(0).value<QList<QOpcUaEndpointDescription>>();

    bool foundSecure = false;
    for (const QOpcUaEndpointDescription &endpoint : endpoints) {
        if (endpoint.securityPolicy().contains(QLatin1String("Basic256Sha256"))) {
            foundSecure = true;
            break;
        }
    }
    QVERIFY2(foundSecure, "strict trust mode did not advertise a secure endpoint");

    // The server PKI skeleton, including the rejected store, was created.
    const QString rejectedDir =
        m_dir.filePath(QStringLiteral("pki-%1/rejected/certs").arg(port));
    QVERIFY2(QDir(rejectedDir).exists(), "the rejected certificate store was not created");
}

void ServerSecurityTest::cleanup()
{
    stopRuntime();
}

QTEST_GUILESS_MAIN(ServerSecurityTest)

#include "tst_serversecurity.moc"
