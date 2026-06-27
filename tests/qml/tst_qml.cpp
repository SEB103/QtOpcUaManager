#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest/quicktest.h>

class MockOpcUaManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected CONSTANT)
    Q_PROPERTY(bool busy READ busy CONSTANT)

public:
    using QObject::QObject;

    bool connected() const { return false; }
    bool busy() const { return false; }
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
