#ifndef APPENGINE_H
#define APPENGINE_H

#include <QQmlApplicationEngine>
#include <QString>

class OpcUaManager;
class OpcUaService;
class QThread;

/*!
 * \class AppEngine
 * \brief QML engine wrapper that owns application-level C++ services.
 */
class AppEngine : public QQmlApplicationEngine
{
    Q_OBJECT
    Q_DISABLE_COPY(AppEngine)
public:
    explicit AppEngine(const QString& initialUrl, QObject* parent = nullptr);
    ~AppEngine() override;

    bool startOpcUaBackend();

private:
    void createOpcUaRuntime();

    QString m_initialUrl;
    QThread* m_opcUaThread = nullptr;
    OpcUaManager* m_opcUaManager = nullptr;
    OpcUaService* m_opcUaService = nullptr;
    bool m_opcUaBackendStartRequested = false;
};

#endif // APPENGINE_H
