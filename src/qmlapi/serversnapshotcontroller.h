#ifndef SERVERSNAPSHOTCONTROLLER_H
#define SERVERSNAPSHOTCONTROLLER_H

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "core/addressspacesnapshot.h"
#include "core/clonebrowser.h"
#include "core/diagnosticslevel.h"
#include "core/retranslatablestatus.h"

class OpcUaManager;
class QThread;

/**
 * Takes an address-space snapshot of the connected server and writes it as
 * `*.uasnapshot.json`; exposed to QML as `cppServerSnapshot`.
 *
 * The browse runs on the OPC UA worker through OpcUaManager, shares its single
 * browse slot with the Server Studio clone, and is told apart by its request
 * id. The file is written in a background thread, so the GUI stays responsive
 * for large servers.
 */
class ServerSnapshotController : public QObject
{
    Q_OBJECT

    /** Whether a snapshot is being browsed or written. */
    Q_PROPERTY(bool inProgress READ inProgress NOTIFY progressChanged)
    /** Progress text of the snapshot in flight; empty when none runs. */
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)

public:
    /** What a snapshot covers. */
    enum Scope {
        /** The Objects folder and everything below it. */
        ObjectsScope = 0,
        /** The node selected in the client tree, captured itself, and its subtree. */
        SelectedNodeScope = 1,
        /** The whole server from the Root folder, including Types and Views. */
        WholeServerScope = 2
    };
    Q_ENUM(Scope)

    /** Creates the controller owned by \a parent. */
    explicit ServerSnapshotController(QObject *parent = nullptr);
    /** Waits for a running file write before the controller goes away. */
    ~ServerSnapshotController() override;

    /** Uses \a manager for browsing; the pointer is not owned. */
    void setOpcUaManager(OpcUaManager *manager);

    /** Returns whether a snapshot is being browsed or written. */
    bool inProgress() const { return m_state != State::Idle; }

    /** Returns the progress text of the snapshot in flight. */
    QString progressText() const;

    /**
     * Starts a snapshot of \a scope into \a filePath (a local path or file URL).
     * \a includeValues reads variable values; \a maxNodes limits the browse
     * (0: default); \a maxArrayElements, when positive, keeps only that many
     * elements per array. Returns false when it cannot start; a notification
     * says why.
     */
    Q_INVOKABLE bool start(int scope, bool includeValues, int maxNodes, const QString &filePath,
                           int maxArrayElements = 0);

    /** Cancels the browse of the snapshot in flight; nothing is written. */
    Q_INVOKABLE void cancel();

    /** Returns a default file name such as `plc_20261126-1012.uasnapshot.json`. */
    Q_INVOKABLE QString suggestedFileName() const;

    /** Returns suggestedFileName() in the user's documents folder, as a file URL. */
    Q_INVOKABLE QUrl suggestedFileUrl() const;

    /** Rebuilds the last status message in the active language for the status bar. */
    void retranslate();

signals:
    /** Emitted when inProgress or progressText changes. */
    void progressChanged();
    /** Emitted with a translated status \a message at \a level. */
    void notification(int level, const QString &message);
    /** Re-emits the last status in the active language for the status bar only. */
    void statusRetranslated(int level, const QString &message);
    /** Emitted after an incomplete snapshot was written; \a report explains what is missing. */
    void snapshotIncomplete(const QString &report);

private:
    /** Phase of the snapshot in flight. */
    enum class State {
        Idle,     //!< Nothing runs.
        Browsing, //!< The worker browses the server.
        Writing   //!< The file is written in the background.
    };

    void handleBrowseResult(quint64 requestId, const QList<CloneNode> &nodes,
                            const QStringList &namespaceUris, const CloneCompletion &completion);
    void writeInBackground(const SnapshotInput &input);
    void handleWritten(const AddressSpaceSnapshot::WriteResult &result,
                       const CloneCompletion &completion);
    void setState(State state);
    void notify(Diagnostics::Level level, std::function<QString()> render);

    QPointer<OpcUaManager> m_manager;    //!< Client facade used for browsing.
    State m_state = State::Idle;         //!< Phase of the snapshot in flight.
    quint64 m_requestId = 0;             //!< Request id of the browse in flight.
    QString m_filePath;                  //!< Target file of the snapshot in flight.
    SnapshotScope m_scope;               //!< Scope of the snapshot in flight.
    QDateTime m_created;                 //!< Start time of the snapshot in flight.
    CloneProgress m_progress;            //!< Latest browse progress.
    QThread *m_writer = nullptr;         //!< Background file writer, if running.
    RetranslatableStatus m_lastStatus;   //!< Last status, for retranslate().
};

#endif // SERVERSNAPSHOTCONTROLLER_H
