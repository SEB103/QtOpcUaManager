#include "serversnapshotcontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>

#include <memory>

#include "browseprogresstext.h"
#include "opcuamanager.h"

/*!
    \class ServerSnapshotController
    \inmodule OpcUaManager
    \brief Takes an address-space snapshot of the connected server and writes it as
           \c{*.uasnapshot.json}.

    Exposed to QML as \c cppServerSnapshot. start() requests a browse through
    OpcUaManager with CloneOptions for the chosen scope: values on or off, the
    node limit, and for the selected-node scope a captured root. The browse
    shares the worker's single browse slot with the Server Studio clone; only
    the result carrying this controller's request id is used.

    When the browse ends, the snapshot document is built and written by
    AddressSpaceSnapshot in a background thread. A cancelled browse writes
    nothing. An incomplete browse (node or depth limit, timeout, failed reads)
    is still written, with its completion state in the file, and reported
    through snapshotIncomplete() so it is not mistaken for a complete record.
*/

namespace {

/*!
 * \internal
 * \brief Returns \a filePath as a local path; a \c file: URL from a FileDialog is converted.
 */
QString localPath(const QString &filePath)
{
    const QUrl url(filePath);
    return url.isLocalFile() ? url.toLocalFile() : filePath;
}

/*!
 * \internal
 * \brief Returns the sentences that explain why the browse of \a completion is incomplete.
 */
QString incompleteDetails(const CloneCompletion &completion)
{
    QStringList sentences;
    if (completion.nodeLimitReached) {
        sentences.append(ServerSnapshotController::tr("The limit of %1 nodes was reached.")
                             .arg(completion.options.maxNodes));
    }
    if (completion.depthLimitedNodes > 0) {
        sentences.append(ServerSnapshotController::tr("%1 node(s) at the depth limit of %2 were not browsed.")
                             .arg(completion.depthLimitedNodes)
                             .arg(completion.options.maxDepth));
    }
    if (completion.state == CloneCompletion::State::TimedOut) {
        sentences.append(ServerSnapshotController::tr("The server did not answer in time; %1 "
                                                      "node(s) were left unread.")
                             .arg(completion.pendingRequests));
    }
    const int failed = completion.failedReads + completion.failedBrowses;
    if (failed > 0) {
        sentences.append(
            ServerSnapshotController::tr("%1 node(s) could not be read or browsed completely.").arg(failed));
    }
    return sentences.join(QLatin1Char(' '));
}

} // namespace

/*!
 * \brief Constructs the controller owned by \a parent.
 */
ServerSnapshotController::ServerSnapshotController(QObject *parent)
    : QObject(parent)
{
}

/*!
 * \brief Waits for a running background write, so the file is complete before exit.
 */
ServerSnapshotController::~ServerSnapshotController()
{
    if (m_writer) {
        m_writer->wait();
        delete m_writer;
    }
}

/*!
 * \brief Uses \a manager for browsing and listens to its browse results.
 * \param manager Client facade; not owned, may be null.
 */
void ServerSnapshotController::setOpcUaManager(OpcUaManager *manager)
{
    if (m_manager)
        disconnect(m_manager, nullptr, this, nullptr);
    m_manager = manager;
    if (!m_manager)
        return;
    connect(m_manager, &OpcUaManager::cloneSnapshotReady, this,
            &ServerSnapshotController::handleBrowseResult);
    connect(m_manager, &OpcUaManager::cloneProgress, this,
            [this](quint64 requestId, const CloneProgress &progress) {
                if (m_state != State::Browsing || requestId != m_requestId)
                    return;
                m_progress = progress;
                emit progressChanged();
            });
}

/*!
 * \brief Returns the progress text of the snapshot in flight; empty when none runs.
 */
QString ServerSnapshotController::progressText() const
{
    switch (m_state) {
    case State::Browsing: return BrowseProgressText::render(m_progress);
    case State::Writing: return tr("Writing the snapshot file…");
    case State::Idle: break;
    }
    return {};
}

/*!
 * \brief Starts a snapshot of \a scope into \a filePath.
 * \param scope A Scope value: Objects, the selected client node, or the whole server.
 * \param includeValues Whether variable values are read.
 * \param maxNodes Node limit of the browse; 0 uses the default limit.
 * \param filePath Target file, as a local path or a \c file: URL; a name without
 *        a \c .json suffix gets \c .uasnapshot.json.
 * \param maxArrayElements Elements kept per array; 0 keeps all.
 * \return Whether the browse was started; otherwise a notification says why.
 */
bool ServerSnapshotController::start(int scope, bool includeValues, int maxNodes,
                                     const QString &filePath, int maxArrayElements)
{
    if (!m_manager) {
        notify(Diagnostics::Error, [] { return tr("No OPC UA client is available."); });
        return false;
    }
    if (inProgress()) {
        notify(Diagnostics::Warning,
               [] { return tr("Another clone or snapshot of the server is already running."); });
        return false;
    }
    QString path = localPath(filePath);
    if (!path.isEmpty() && !path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".uasnapshot.json");
    if (path.isEmpty()) {
        notify(Diagnostics::Error, [] { return tr("Choose a file for the snapshot."); });
        return false;
    }

    SnapshotScope snapshotScope;
    snapshotScope.includeValues = includeValues;
    CloneOptions options;
    options.readValues = includeValues;
    if (maxNodes > 0)
        options.maxNodes = maxNodes;
    options.maxArrayElements = std::max(0, maxArrayElements);

    switch (scope) {
    case SelectedNodeScope:
        snapshotScope.kind = QStringLiteral("Node");
        snapshotScope.rootNodeId = m_manager->selectedNodeId();
        options.captureRoot = true;
        if (snapshotScope.rootNodeId.isEmpty()) {
            notify(Diagnostics::Error,
                   [] { return tr("Select a node in the address-space tree first."); });
            return false;
        }
        break;
    case WholeServerScope:
        snapshotScope.kind = QStringLiteral("Server");
        snapshotScope.rootNodeId = QStringLiteral("ns=0;i=84");
        break;
    case ObjectsScope:
    default:
        snapshotScope.kind = QStringLiteral("Objects");
        snapshotScope.rootNodeId = QStringLiteral("ns=0;i=85");
        snapshotScope.rootBrowsePath = QStringLiteral("/Objects");
        break;
    }

    const quint64 requestId = m_manager->requestAddressSpaceBrowse(snapshotScope.rootNodeId,
                                                                   options);
    if (requestId == 0) {
        notify(Diagnostics::Warning,
               [] { return tr("Another clone or snapshot of the server is already running."); });
        return false;
    }

    m_requestId = requestId;
    m_filePath = path;
    m_scope = snapshotScope;
    m_created = QDateTime::currentDateTimeUtc();
    m_progress = CloneProgress();
    setState(State::Browsing);
    notify(Diagnostics::Info,
           [] { return tr("Taking a snapshot of the server address space…"); });
    return true;
}

/*!
 * \brief Cancels the browse of the snapshot in flight; nothing is written.
 *
 * A file that is already being written is completed.
 */
void ServerSnapshotController::cancel()
{
    if (m_state == State::Browsing && m_manager)
        m_manager->cancelCloneSnapshot();
}

/*!
 * \brief Returns a default file name for a snapshot of the connected server.
 *
 * The name is made of the host of the most recent server URL and the local
 * time, for example \c{plc_20261126-1012.uasnapshot.json}; without a known
 * host it starts with \c server.
 */
QString ServerSnapshotController::suggestedFileName() const
{
    QString host;
    if (m_manager && !m_manager->recentServerUrls().isEmpty())
        host = QUrl(m_manager->recentServerUrls().first()).host();
    host.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (host.isEmpty())
        host = QStringLiteral("server");
    return QStringLiteral("%1_%2.uasnapshot.json")
        .arg(host, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")));
}

/*!
 * \brief Returns suggestedFileName() in the user's documents folder, as a file URL.
 *
 * Used as the initial selection of the save dialog.
 */
QUrl ServerSnapshotController::suggestedFileUrl() const
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(QDir(folder).filePath(suggestedFileName()));
}

/*!
 * \brief Rebuilds the last status message in the active language for the status bar.
 */
void ServerSnapshotController::retranslate()
{
    if (m_lastStatus.isValid())
        emit statusRetranslated(m_lastStatus.level, m_lastStatus.render());
}

/*!
 * \internal
 * \brief Takes the browse result of request \a requestId and starts the file write.
 * \param requestId Request id of the result; other requests are ignored.
 * \param nodes Captured nodes.
 * \param namespaceUris NamespaceArray of the server.
 * \param completion Outcome of the browse.
 */
void ServerSnapshotController::handleBrowseResult(quint64 requestId,
                                                  const QList<CloneNode> &nodes,
                                                  const QStringList &namespaceUris,
                                                  const CloneCompletion &completion)
{
    if (m_state != State::Browsing || requestId != m_requestId)
        return;

    if (completion.state == CloneCompletion::State::Cancelled) {
        setState(State::Idle);
        notify(Diagnostics::Info, [] { return tr("Snapshot cancelled; no file was written."); });
        return;
    }
    if (completion.state == CloneCompletion::State::Failed && nodes.isEmpty()) {
        setState(State::Idle);
        notify(Diagnostics::Error, [] {
            return tr("The snapshot could not be taken: the server could not be browsed.");
        });
        return;
    }

    SnapshotInput input;
    input.application = QCoreApplication::applicationName();
    input.applicationVersion = QCoreApplication::applicationVersion();
    input.created = m_created;
    input.scope = m_scope;
    input.nodes = nodes;
    input.namespaceUris = namespaceUris;
    input.completion = completion;
    writeInBackground(input);
}

/*!
 * \internal
 * \brief Writes the snapshot of \a input in a background thread.
 *
 * Building the JSON document of a large server takes seconds and hundreds of
 * megabytes, so it never runs in the GUI thread. The thread works on its own
 * copy of the input; the result is applied in the GUI thread.
 */
void ServerSnapshotController::writeInBackground(const SnapshotInput &input)
{
    setState(State::Writing);
    auto result = std::make_shared<AddressSpaceSnapshot::WriteResult>();
    m_writer = QThread::create([input, path = m_filePath, result]() {
        *result = AddressSpaceSnapshot::write(path, input);
    });
    connect(m_writer, &QThread::finished, this,
            [this, result, completion = input.completion]() {
                m_writer->deleteLater();
                m_writer = nullptr;
                handleWritten(*result, completion);
            });
    m_writer->start();
}

/*!
 * \internal
 * \brief Reports the write \a result of a snapshot whose browse ended with \a completion.
 */
void ServerSnapshotController::handleWritten(const AddressSpaceSnapshot::WriteResult &result,
                                             const CloneCompletion &completion)
{
    setState(State::Idle);
    if (!result.ok) {
        notify(Diagnostics::Error, [error = result.errorString] {
            return tr("The snapshot could not be written: %1").arg(error);
        });
        return;
    }

    const bool exact = completion.isComplete() && completion.failedReads == 0
                       && completion.failedBrowses == 0;
    const auto report = [exact, completion, path = m_filePath, n = completion.nodeCount] {
        if (exact)
            return tr("Snapshot saved: %1 node(s) in %2.").arg(n).arg(path);
        return tr("Snapshot saved, but incomplete: %1 node(s) in %2.").arg(n).arg(path)
               + QLatin1Char(' ') + incompleteDetails(completion);
    };
    notify(exact ? Diagnostics::Info : Diagnostics::Warning, report);
    if (!exact)
        emit snapshotIncomplete(report());
}

/*!
 * \internal
 * \brief Sets the phase of the snapshot to \a state and notifies QML.
 */
void ServerSnapshotController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit progressChanged();
}

/*!
 * \internal
 * \brief Emits \a render's text at \a level and stores the renderer for retranslate().
 */
void ServerSnapshotController::notify(Diagnostics::Level level, std::function<QString()> render)
{
    m_lastStatus.level = level;
    m_lastStatus.render = std::move(render);
    emit notification(level, m_lastStatus.render());
}
