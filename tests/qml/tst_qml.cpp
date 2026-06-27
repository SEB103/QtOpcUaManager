#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class MockOpcUaManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList opcUaBackend READ opcUaBackend CONSTANT)
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QStringList servers READ servers CONSTANT)
    Q_PROPERTY(QStringList endpoints READ endpoints CONSTANT)
    Q_PROPERTY(bool connected READ connected CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)
    Q_PROPERTY(int operationState READ operationState CONSTANT)
    Q_PROPERTY(bool endpointUrlRewriteEnabled READ endpointUrlRewriteEnabled
                   WRITE setEndpointUrlRewriteEnabled
                   NOTIFY endpointUrlRewriteEnabledChanged)
    Q_PROPERTY(QObject *treeModel READ treeModel CONSTANT)
    Q_PROPERTY(QString lastError READ lastError CONSTANT)
    Q_PROPERTY(int authMode READ authMode CONSTANT)

public:
    using QObject::QObject;

    QStringList opcUaBackend() const { return {QStringLiteral("open62541")}; }
    QString backend() const { return m_backend; }
    QStringList servers() const { return {QStringLiteral("Mock server | opc.tcp://127.0.0.1:4840 | Client")}; }
    QStringList endpoints() const { return {QStringLiteral("opc.tcp://127.0.0.1:4840 | None | None | auth:Anonymous")}; }
    bool connected() const { return false; }
    bool busy() const { return false; }
    int operationState() const { return 0; }
    bool endpointUrlRewriteEnabled() const { return m_endpointUrlRewriteEnabled; }
    QObject *treeModel() const { return nullptr; }
    QString lastError() const { return {}; }
    int authMode() const { return 0; }

    void setBackend(const QString &backend)
    {
        if (m_backend == backend)
            return;
        m_backend = backend;
        emit backendChanged();
    }

    void setEndpointUrlRewriteEnabled(bool enabled)
    {
        if (m_endpointUrlRewriteEnabled == enabled)
            return;
        m_endpointUrlRewriteEnabled = enabled;
        emit endpointUrlRewriteEnabledChanged();
    }

    Q_INVOKABLE void setAnonymousAuthentication() {}
    Q_INVOKABLE void setUsernameAuthentication(const QString &, const QString &) {}
    Q_INVOKABLE void setCertificatePrivateKeyPassword(const QString &) {}
    Q_INVOKABLE void setCertificateAuthentication() {}
    Q_INVOKABLE void discoverServers(const QString &) {}
    Q_INVOKABLE void requestEndpointsForServer(int) {}
    Q_INVOKABLE void connectToEndpoint(int) {}
    Q_INVOKABLE void disconnectFromServer() {}

signals:
    void backendChanged();
    void endpointUrlRewriteEnabledChanged();

private:
    QString m_backend {QStringLiteral("open62541")};
    bool m_endpointUrlRewriteEnabled {false};
};

class QmlTestSetup : public QObject
{
    Q_OBJECT

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("cppManagerOpcUa"), &m_manager);
    }

private:
    MockOpcUaManager m_manager;
};

QUICK_TEST_MAIN_WITH_SETUP(opcuamanager_qml, QmlTestSetup)

#include "tst_qml.moc"
