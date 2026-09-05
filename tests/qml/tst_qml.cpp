#include <QQmlContext>
#include <QQmlEngine>
#include <QVariantMap>
#include <QtQuickTest/quicktest.h>

#include "models/dataaccessmodel.h"
#include "models/dataviewfiltermodel.h"

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

    /*! Real Data Access View model so the table delegates see genuine roles. */
    Q_PROPERTY(QObject *dataModel READ dataModel CONSTANT)

    /*! Real sorted and filtered view of the Data Access View model. */
    Q_PROPERTY(QObject *dataViewModel READ dataViewModel CONSTANT)

    /*! Mock focus-segment model object; null because smoke tests do not inspect rows. */
    Q_PROPERTY(QObject *focusModel READ focusModel CONSTANT)

    /*! Mock shared node selection. */
    Q_PROPERTY(QString selectedNodeId READ selectedNodeId CONSTANT)

    /*! Mock pinned focus node id. */
    Q_PROPERTY(QString focusNodeId READ focusNodeId CONSTANT)

    /*! Mock paused state of table updates. */
    Q_PROPERTY(bool updatesPaused READ updatesPaused WRITE setUpdatesPaused
                   NOTIFY updatesPausedChanged)

    /*! Mock Data Access View layout state, writable so the table can persist it. */
    Q_PROPERTY(QVariantMap dataViewState READ dataViewState WRITE setDataViewState
                   NOTIFY dataViewStateChanged)

public:
    /*! Creates the mock and wires the filter proxy onto the data model. */
    explicit MockOpcUaManager(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_dataViewModel.setSourceModel(&m_dataModel);
    }

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

    /*! Returns the real Data Access View model. */
    QObject *dataModel() { return &m_dataModel; }

    /*! Returns the real sorted and filtered Data Access View model. */
    QObject *dataViewModel() { return &m_dataViewModel; }

    /*! Returns no focus model because QML smoke tests only create components. */
    QObject *focusModel() const { return nullptr; }

    /*! Returns an empty mock node selection. */
    QString selectedNodeId() const { return {}; }

    /*! Returns an empty mock focus node id. */
    QString focusNodeId() const { return {}; }

    /*! Returns whether table updates are paused in the mock. */
    bool updatesPaused() const { return m_updatesPaused; }

    /*! Updates the mock paused state to  paused. */
    void setUpdatesPaused(bool paused)
    {
        if (m_updatesPaused == paused)
            return;
        m_updatesPaused = paused;
        emit updatesPausedChanged();
    }

    /*! Returns the mock Data Access View layout state. */
    QVariantMap dataViewState() const { return m_dataViewState; }

    /*! Stores the Data Access View layout  state written by the table. */
    void setDataViewState(const QVariantMap &state)
    {
        if (m_dataViewState == state)
            return;
        m_dataViewState = state;
        emit dataViewStateChanged();
    }

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

    /*! Mock no-op for Data Access View row selection. */
    Q_INVOKABLE void selectDataRow(int) {}

    /*! Mock no-op for value writes. */
    Q_INVOKABLE void writeValue(int, const QVariant &) {}

    /*! Mock no-op for bulk row removal. */
    Q_INVOKABLE void removeNodes(const QList<int> &) {}

    /*! Mock no-op for sampling-interval changes. */
    Q_INVOKABLE void setSamplingInterval(int, int) {}

    /*! Mock clipboard write that discards the text. */
    Q_INVOKABLE void copyToClipboard(const QString &) {}

    /*! Mock row-to-text conversion returning an empty string. */
    Q_INVOKABLE QString dataViewRowsAsText(const QList<int> &, const QList<int> &) const
    {
        return {};
    }

    /*! Mock CSV export that reports failure without touching the file system. */
    Q_INVOKABLE bool exportDataViewCsv(const QUrl &, const QList<int> &, const QList<int> &)
    {
        return false;
    }

    /*! Mock drop handler that never adds a node. */
    Q_INVOKABLE bool monitorNodeById(const QString &) { return false; }

signals:
    /*! Emitted when the mock backend changes. */
    void backendChanged();

    /*! Emitted when the mock paused state changes. */
    void updatesPausedChanged();

    /*! Emitted when the mock Data Access View layout state changes. */
    void dataViewStateChanged();

    /*! Emitted when the mock endpoint URL rewriting state changes. */
    void endpointUrlRewriteEnabledChanged();

private:
    /*! Mock selected backend plugin name. */
    QString m_backend {QStringLiteral("open62541")};

    /*! Mock endpoint URL rewriting state. */
    bool m_endpointUrlRewriteEnabled {false};

    /*! Real Data Access View model backing the table under test. */
    DataAccessModel m_dataModel;

    /*! Real sorting and filtering proxy shown by the table under test. */
    DataViewFilterModel m_dataViewModel;

    /*! Mock paused state of table updates. */
    bool m_updatesPaused {false};

    /*! Mock Data Access View layout state written back by the table. */
    QVariantMap m_dataViewState;
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
