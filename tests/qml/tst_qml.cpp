#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

/*! Mock QML-facing OPC UA manager used by Quick Test components. */
class MockOpcUaManager : public QObject
{
    Q_OBJECT

    /*! Mock available backend plugin names. */
    Q_PROPERTY(QStringList opcUaBackend READ opcUaBackend CONSTANT)

    /*! Mock selected backend plugin name. */
    Q_PROPERTY(QString backend READ backend WRITE setBackend NOTIFY backendChanged)

    /*! Mock discovered server display rows. */
    Q_PROPERTY(QStringList servers READ servers CONSTANT)

    /*! Mock discovered endpoint display rows. */
    Q_PROPERTY(QStringList endpoints READ endpoints CONSTANT)

    /*! Mock connection state. */
    Q_PROPERTY(bool connected READ connected CONSTANT)

    /*! Mock busy state. */
    Q_PROPERTY(bool busy READ busy CONSTANT)

    /*! Mock operation state. */
    Q_PROPERTY(int operationState READ operationState CONSTANT)

    /*! Mock endpoint URL rewriting state. */
    Q_PROPERTY(bool endpointUrlRewriteEnabled READ endpointUrlRewriteEnabled
                   WRITE setEndpointUrlRewriteEnabled
                   NOTIFY endpointUrlRewriteEnabledChanged)

    /*! Mock tree model object; null because smoke tests do not inspect rows. */
    Q_PROPERTY(QObject *treeModel READ treeModel CONSTANT)

    /*! Mock last error text. */
    Q_PROPERTY(QString lastError READ lastError CONSTANT)

    /*! Mock authentication mode. */
    Q_PROPERTY(int authMode READ authMode CONSTANT)

public:
    using QObject::QObject;

    /*! Returns one mock backend name. */
    QStringList opcUaBackend() const { return {QStringLiteral("open62541")}; }

    /*! Returns the mock selected backend. */
    QString backend() const { return m_backend; }

    /*! Returns one mock server display row. */
    QStringList servers() const { return {QStringLiteral("Mock server | opc.tcp://127.0.0.1:4840 | Client")}; }

    /*! Returns one mock endpoint display row. */
    QStringList endpoints() const { return {QStringLiteral("opc.tcp://127.0.0.1:4840 | None | None | auth:Anonymous")}; }

    /*! Returns the mock disconnected state. */
    bool connected() const { return false; }

    /*! Returns the mock idle state. */
    bool busy() const { return false; }

    /*! Returns the mock idle operation state. */
    int operationState() const { return 0; }

    /*! Returns whether endpoint URL rewriting is enabled in the mock. */
    bool endpointUrlRewriteEnabled() const { return m_endpointUrlRewriteEnabled; }

    /*! Returns no tree model because QML smoke tests only create components. */
    QObject *treeModel() const { return nullptr; }

    /*! Returns an empty mock error string. */
    QString lastError() const { return {}; }

    /*! Returns anonymous authentication mode. */
    int authMode() const { return 0; }

    /*! Updates the mock selected \a backend and emits backendChanged(). */
    void setBackend(const QString &backend)
    {
        if (m_backend == backend)
            return;
        m_backend = backend;
        emit backendChanged();
    }

    /*! Updates endpoint URL rewriting state to \a enabled. */
    void setEndpointUrlRewriteEnabled(bool enabled)
    {
        if (m_endpointUrlRewriteEnabled == enabled)
            return;
        m_endpointUrlRewriteEnabled = enabled;
        emit endpointUrlRewriteEnabledChanged();
    }

    /*! Mock no-op for anonymous authentication requests. */
    Q_INVOKABLE void setAnonymousAuthentication() {}

    /*! Mock no-op for username authentication requests. */
    Q_INVOKABLE void setUsernameAuthentication(const QString &, const QString &) {}

    /*! Mock no-op for certificate private-key password requests. */
    Q_INVOKABLE void setCertificatePrivateKeyPassword(const QString &) {}

    /*! Mock no-op for certificate authentication requests. */
    Q_INVOKABLE void setCertificateAuthentication() {}

    /*! Mock no-op for discovery requests. */
    Q_INVOKABLE void discoverServers(const QString &) {}

    /*! Mock no-op for endpoint requests by server index. */
    Q_INVOKABLE void requestEndpointsForServer(int) {}

    /*! Mock no-op for endpoint connection requests. */
    Q_INVOKABLE void connectToEndpoint(int) {}

    /*! Mock no-op for disconnection requests. */
    Q_INVOKABLE void disconnectFromServer() {}

signals:
    /*! Emitted when the mock backend changes. */
    void backendChanged();

    /*! Emitted when the mock endpoint URL rewriting state changes. */
    void endpointUrlRewriteEnabledChanged();

private:
    /*! Mock selected backend plugin name. */
    QString m_backend {QStringLiteral("open62541")};

    /*! Mock endpoint URL rewriting state. */
    bool m_endpointUrlRewriteEnabled {false};
};

/*! Quick Test setup object that injects C++ context properties into each engine. */
class QmlTestSetup : public QObject
{
    Q_OBJECT

public slots:
    /*! Adds the mock OPC UA manager to \a engine as \c cppManagerOpcUa. */
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty(QStringLiteral("cppManagerOpcUa"), &m_manager);
    }

private:
    /*! Mock manager kept alive for the lifetime of the Quick Test setup object. */
    MockOpcUaManager m_manager;
};

QUICK_TEST_MAIN_WITH_SETUP(opcuamanager_qml, QmlTestSetup)

#include "tst_qml.moc"
