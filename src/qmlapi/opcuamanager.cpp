#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <algorithm>
#include <functional>
#include <utility>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QtQuick/QQuickTextDocument>

#include "core/apppaths.h"
#include "core/opcuaaccesslevel.h"
#include "core/opcuaservice.h"
#include "core/opcuastatushint.h"
#include "models/structuredvalueformatter.h"
#include "opcuamanager.h"
#include "structuredvaluehighlighter.h"

namespace {

/*!
 * \internal
 * \brief QSettings key storing the persisted structured-value output format.
 */
constexpr auto kValueFormatSettingsKey = "view/valueFormat";

/*!
 * \internal
 * \brief Returns \a text quoted, escaped, and made inert for one CSV field.
 *
 * A field is quoted only when it contains a separator, a quote, or a line break,
 * which keeps ordinary values readable in the exported file. Embedded quotes are
 * doubled, as RFC 4180 requires.
 *
 * A field opening with an equals sign, a plus, a minus, or an at sign is a live
 * formula to a spreadsheet, and quoting is no defense because the quotes are
 * stripped while parsing. The exported values come from the connected OPC UA
 * server, so such a field is prefixed with an apostrophe, which spreadsheets read
 * as "the rest is text" and drop again on display.
 */
QString csvField(const QString &text)
{
    const bool startsFormula = !text.isEmpty()
                               && (text.startsWith(QLatin1Char('='))
                                   || text.startsWith(QLatin1Char('+'))
                                   || text.startsWith(QLatin1Char('-'))
                                   || text.startsWith(QLatin1Char('@')));

    const bool needsQuotes = startsFormula
                             || text.contains(QLatin1Char(','))
                             || text.contains(QLatin1Char('"'))
                             || text.contains(QLatin1Char('\n'))
                             || text.contains(QLatin1Char('\r'));
    if (!needsQuotes)
        return text;

    QString escaped = startsFormula ? QLatin1Char('\'') + text : text;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

/*!
 * \internal
 * \brief Returns \a text flattened to a single tab-separated cell.
 *
 * Tabs and line breaks would break the row and column structure of clipboard
 * text, so they collapse to spaces.
 */
QString plainCell(const QString &text)
{
    QString flattened = text;
    flattened.replace(QLatin1Char('\t'), QLatin1Char(' '));
    flattened.replace(QLatin1Char('\r'), QLatin1Char(' '));
    flattened.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return flattened;
}

/*!
 * \internal
 * \brief Maps an OpcUaManager::ValueFormat to the formatter output format.
 */
StructuredValueFormatter::Format toFormatterFormat(OpcUaManager::ValueFormat format)
{
    return format == OpcUaManager::FormatXml ? StructuredValueFormatter::Format::Xml
                                             : StructuredValueFormatter::Format::Json;
}

/*!
 * \internal
 * \brief Maps an OpcUaManager::ValueFormat to the highlighter language.
 */
StructuredValueHighlighter::Language toHighlighterLanguage(OpcUaManager::ValueFormat format)
{
    return format == OpcUaManager::FormatXml ? StructuredValueHighlighter::Language::Xml
                                             : StructuredValueHighlighter::Language::Json;
}

} // namespace

/*!
 * \property OpcUaManager::opcUaBackend
 * \brief Available Qt OPC UA backend plugin names.
 */

/*!
 * \property OpcUaManager::backend
 * \brief Currently selected Qt OPC UA backend plugin name.
 */

/*!
 * \property OpcUaManager::servers
 * \brief Display rows for discovered OPC UA servers.
 */

/*!
 * \property OpcUaManager::endpoints
 * \brief Display rows for endpoints returned by the selected server.
 */

/*!
 * \property OpcUaManager::connected
 * \brief Whether an OPC UA session is currently connected.
 */

/*!
 * \property OpcUaManager::busy
 * \brief Whether discovery, endpoint lookup, connect, or disconnect is active.
 */

/*!
 * \property OpcUaManager::operationState
 * \brief Current operation state as an OpcUaManager::OperationState integer.
 */

/*!
 * \property OpcUaManager::clientState
 * \brief Current QOpcUaClient state as an OpcUaManager::ClientState integer.
 */

/*!
 * \property OpcUaManager::endpointUrlRewriteEnabled
 * \brief Whether advertised endpoint URLs are rewritten to the discovery host and port.
 */

/*!
 * \property OpcUaManager::treeModel
 * \brief Tree model exposed to QML for address-space browsing; owned by this manager.
 */

/*!
 * \property OpcUaManager::lastError
 * \brief Last user-visible OPC UA error text; empty when no error is active.
 */

/*!
 * \property OpcUaManager::authMode
 * \brief Current QOpcUaUserTokenPolicy::TokenType as an integer for QML controls.
 */

OpcUaManager::OpcUaManager(const QString &initialUrl, QObject *parent)
    : QObject(parent)
    , m_initialUrl(initialUrl)
    , m_treeModel(new OpcUaModel(this))
    , m_focusModel(new OpcUaModel(this))
    , m_dataModel(new DataAccessModel(this))
    , m_dataViewModel(new DataViewFilterModel(this))
    , m_trendModel(new TrendModel(3000, this))
    , m_attributesModel(new AttributesModel(this))
    , m_nodeDatabase(std::make_unique<NodeDatabase>())
{
    m_dataViewModel->setSourceModel(m_dataModel);

    // The status bar shows how many nodes the project watches, so republish the
    // count whenever the table gains or loses rows.
    connect(m_dataModel, &QAbstractItemModel::rowsInserted,
            this, &OpcUaManager::monitoredNodeCountChanged);
    connect(m_dataModel, &QAbstractItemModel::rowsRemoved,
            this, &OpcUaManager::monitoredNodeCountChanged);
    connect(m_dataModel, &QAbstractItemModel::modelReset,
            this, &OpcUaManager::monitoredNodeCountChanged);

    // The legacy SQLite store is opened read-only for one-time migration into a
    // project file. Monitored nodes are no longer seeded from it at startup; the
    // active project is the source of truth and fills the Data Access View through
    // applyProject(). See exportLegacyState().
    const QString databasePath = AppPaths::instance().databaseFilePath();
    if (!m_nodeDatabase->open(databasePath))
        qWarning() << "OpcUaManager: failed to open node database at" << databasePath;

    // Restore the structured-value output format chosen in a previous session.
    const int storedFormat =
        QSettings().value(QLatin1String(kValueFormatSettingsKey), FormatJson).toInt();
    m_valueFormat = storedFormat == FormatXml ? FormatXml : FormatJson;
}

OpcUaManager::~OpcUaManager() = default;

/*!
 * \property OpcUaManager::dataModel
 * \brief Data Access View table model exposed to QML; owned by this manager.
 */

/*!
 * \property OpcUaManager::attributesModel
 * \brief Attributes panel model exposed to QML; owned by this manager.
 */

QStringList OpcUaManager::opcUaBackend() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_availableBackends;
}

QString OpcUaManager::backend() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_backend;
}

QStringList OpcUaManager::servers() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_servers;
}

QStringList OpcUaManager::endpoints() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_endpoints;
}

bool OpcUaManager::connected() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_connected;
}

bool OpcUaManager::busy() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_operationState != OperationIdle;
}

int OpcUaManager::operationState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_operationState;
}

int OpcUaManager::clientState() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_clientState;
}

bool OpcUaManager::endpointUrlRewriteEnabled() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_endpointUrlRewriteEnabled;
}

OpcUaModel *OpcUaManager::treeModel() const
{
    return m_treeModel;
}

/*!
 * \brief Requests a recursive clone browse of the connected server's Objects.
 */
void OpcUaManager::requestCloneSnapshot()
{
    // The clone always starts at the standard Objects folder; a single clone is
    // in flight at a time, so the request id is unused.
    emit cloneBrowseRequested(QStringLiteral("ns=0;i=85"), 0);
}

/*!
 * \brief Returns the owned focus-segment tree model exposed to QML.
 */
OpcUaModel *OpcUaManager::focusModel() const
{
    return m_focusModel;
}

/*!
 * \brief Returns the node id of the pinned focus node, or an empty string.
 */
QString OpcUaManager::focusNodeId() const
{
    return m_focusNodeId;
}

/*!
 * \brief Returns the display name of the pinned focus node.
 */
QString OpcUaManager::focusNodeName() const
{
    return m_focusNodeName;
}

/*!
 * \brief Returns whether a stored connection is available for reconnection.
 */
bool OpcUaManager::hasLastConnection() const
{
    return m_hasLastConnection;
}

/*!
 * \brief Injects the INI settings store used for legacy migration and defaults.
 * \param settings Non-owning settings store, or null to disable persistence.
 *
 * No project state is restored at injection time; the launcher opens a project
 * later, and applyProject() restores its state then.
 */
void OpcUaManager::setSettings(QSettings *settings)
{
    // The project layer owns connection and focus persistence; the injected store
    // is retained only for reading legacy state during one-time migration and for
    // the native value-format default. No project state is restored here at
    // startup, because no project is active until the launcher opens one.
    m_settings = settings;
}

/*!
 * \brief Refreshes the owned models' tr()-built text after a UI language switch.
 *
 * QML bindings are retranslated by the engine, but the table headers and the
 * attribute descriptions these models produce with tr() are cached, so they are
 * rebuilt explicitly here. Live subscription values, timestamps, and status text
 * are server data and are left untouched.
 */
void OpcUaManager::retranslate()
{
    if (m_dataModel)
        m_dataModel->retranslate();
    if (m_attributesModel)
        m_attributesModel->retranslate();

    // Rebuild the last notification in the new language for the persistent status
    // bar only. This must not go through notification(), which would re-raise the
    // transient banner on every language switch.
    if (m_lastStatus.isValid())
        emit statusRetranslated(m_lastStatus.level, m_lastStatus.render());
}

/*!
 * \internal
 * \brief Emits \a render's text at \a level and stores the renderer for retranslate().
 *
 * The renderer captures its runtime arguments by value, so re-invoking it later
 * from retranslate() re-runs its tr() calls in the active language and yields the
 * same message translated afresh.
 */
void OpcUaManager::notify(Diagnostics::Level level, std::function<QString()> render)
{
    m_lastStatus.level = level;
    m_lastStatus.render = std::move(render);
    emit notification(level, m_lastStatus.render());
}

DataAccessModel *OpcUaManager::dataModel() const
{
    return m_dataModel;
}

/*!
 * \brief Returns the owned sorted and filtered view of the Data Access View model.
 */
DataViewFilterModel *OpcUaManager::dataViewModel() const
{
    return m_dataViewModel;
}

/*!
 * \brief Returns the owned sample history shown by the trend panel.
 */
TrendModel *OpcUaManager::trendModel() const
{
    return m_trendModel;
}

/*!
 * \brief Returns the number of nodes currently shown in the Data Access View.
 */
int OpcUaManager::monitoredNodeCount() const
{
    return m_dataModel ? m_dataModel->rowCount() : 0;
}

/*!
 * \brief Returns the one-line connection description shown in the status bar.
 */
QString OpcUaManager::connectionSummary() const
{
    if (!m_connection.endpoint.isEmpty())
        return m_connection.endpoint;
    return m_connection.discoveryUrl;
}

/*!
 * \brief Sets whether subscription updates are withheld from the table.
 * \param paused Whether incoming values should stop reaching the table.
 */
void OpcUaManager::setUpdatesPaused(bool paused)
{
    if (m_updatesPaused == paused)
        return;

    m_updatesPaused = paused;
    emit updatesPausedChanged();
}

/*!
 * \brief Stores the Data Access View layout \a state and marks the project changed.
 *
 * Called by the table whenever the user resizes, hides, shows, or re-sorts a
 * column, so the layout is saved with the project like any other project state.
 */
void OpcUaManager::setDataViewState(const QVariantMap &state)
{
    if (m_dataViewState == state)
        return;

    m_dataViewState = state;
    emit dataViewStateChanged();
    emit projectStateChanged();
}

AttributesModel *OpcUaManager::attributesModel() const
{
    return m_attributesModel;
}

QString OpcUaManager::lastError() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_lastError;
}

int OpcUaManager::authMode() const
{
    QMutexLocker locker(&m_stateMutex);
    return m_authMode;
}

void OpcUaManager::setBackend(const QString &backend)
{
    emit setBackendRequested(backend);
}

void OpcUaManager::setEndpointUrlRewriteEnabled(bool enabled)
{
    emit setEndpointUrlRewriteEnabledRequested(enabled);
}

void OpcUaManager::setAnonymousAuthentication()
{
    emit setAnonymousAuthenticationRequested();
}

void OpcUaManager::setUsernameAuthentication(const QString &userName, const QString &password)
{
    // Remember only the user name for persistence; the password is never stored.
    m_lastUserName = userName;
    emit setUsernameAuthenticationRequested(userName, password);
}

void OpcUaManager::setCertificatePrivateKeyPassword(const QString &password)
{
    emit setCertificatePrivateKeyPasswordRequested(password);
}

void OpcUaManager::setCertificateAuthentication()
{
    emit setCertificateAuthenticationRequested();
}

void OpcUaManager::discoverServers(const QString &hostOrUrl)
{
    const QString effectiveUrl = hostOrUrl.trimmed().isEmpty() ? m_initialUrl : hostOrUrl;
    m_lastDiscoveryUrl = effectiveUrl;
    qInfo() << "OpcUaManager discoverServers requested:" << effectiveUrl;
    emit discoverServersRequested(effectiveUrl);
}

void OpcUaManager::requestEndpoints(const QString &serverUrl)
{
    qInfo() << "OpcUaManager requestEndpoints requested:" << serverUrl;
    if (!serverUrl.trimmed().isEmpty())
        m_currentServer = serverUrl.trimmed();
    emit requestEndpointsRequested(serverUrl);
}

void OpcUaManager::requestEndpointsForServer(int serverIndex)
{
    qInfo() << "OpcUaManager requestEndpointsForServer requested. index:" << serverIndex;
    QMutexLocker locker(&m_stateMutex);
    const QString server = m_servers.value(serverIndex);
    locker.unlock();
    if (!server.isEmpty())
        m_currentServer = server;
    emit requestEndpointsForServerRequested(serverIndex);
}

void OpcUaManager::connectToEndpoint(int endpointIndex)
{
    qInfo() << "OpcUaManager connectToEndpoint requested. index:" << endpointIndex;
    {
        QMutexLocker locker(&m_stateMutex);
        m_lastEndpointDisplay = m_endpoints.value(endpointIndex);
    }
    emit connectToEndpointRequested(endpointIndex);
}

void OpcUaManager::disconnectFromServer()
{
    qInfo() << "OpcUaManager disconnectFromServer requested.";
    emit disconnectFromServerRequested();
}

/*!
 * \brief Builds a slash-separated browse path for \a treeIndex from its ancestors.
 *
 * The path is composed from the display names of the node and its ancestors, so
 * the Data Access View can show where a monitored node lives in the address space.
 */
QString OpcUaManager::buildNodePath(const QModelIndex &treeIndex) const
{
    if (!treeIndex.isValid())
        return {};

    const QAbstractItemModel *model = treeIndex.model();
    if (!model)
        return {};

    QStringList segments;
    for (QModelIndex index = treeIndex; index.isValid(); index = index.parent()) {
        const QString name = model->data(index, OpcUaModel::DisplayNameRole).toString();
        if (!name.isEmpty())
            segments.prepend(name);
    }

    QString path = segments.join(QLatin1Char('/'));

    // The focus model is rooted at the pinned node, so a path walked inside it is
    // relative to that node. Prefix the focus node's own absolute path to keep the
    // stored path server-absolute for cross-panel reveal and persistence.
    if (model == m_focusModel && !m_focusNodePath.isEmpty()) {
        path = path.isEmpty()
                   ? m_focusNodePath
                   : m_focusNodePath + QLatin1Char('/') + path;
    }

    return path;
}

/*!
 * \internal
 * \brief Pushes the monitored node ids of the current server to the tree model.
 *
 * Collects the node ids of every Data Access View row that belongs to the
 * currently connected server and hands them to the tree model, which uses them
 * to restore the monitoring checkbox for matching nodes as the tree is browsed.
 */
void OpcUaManager::refreshMonitoredNodeIds()
{
    if (!m_treeModel || !m_dataModel)
        return;

    const QString server = m_currentServer.isEmpty() ? m_initialUrl : m_currentServer;
    QSet<QString> ids;
    const int rows = m_dataModel->rowCount();
    for (int i = 0; i < rows; ++i) {
        if (m_dataModel->serverAt(i) == server) {
            const QString nodeId = m_dataModel->nodeIdAt(i);
            if (!nodeId.isEmpty())
                ids.insert(nodeId);
        }
    }
    m_treeModel->setMonitoredNodeIds(ids);
    if (m_focusModel)
        m_focusModel->setMonitoredNodeIds(ids);
}

/*!
 * \internal
 * \brief Adds or removes the node at \a treeIndex without refreshing the id sets.
 * \param on Whether the node should be monitored.
 * \return Whether the node was found and the change applied.
 *
 * Refreshing the monitored-id sets rescans every Data View row and walks both
 * tree models end to end, which a bulk add must not pay per child. This part is
 * therefore separate from the refresh and the project-state notification, which
 * the caller performs once for the whole operation.
 */
bool OpcUaManager::applyNodeMonitored(const QModelIndex &treeIndex, bool on)
{
    if (!treeIndex.isValid())
        return false;

    OpcUaModel *model = modelForIndex(treeIndex);
    const QString nodeId = model->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return false;

    const QString server = m_currentServer.isEmpty() ? m_initialUrl : m_currentServer;

    if (on) {
        MonitoredNodeRecord record;
        record.server = server;
        record.nodeId = nodeId;
        record.nodePath = buildNodePath(treeIndex);
        record.displayName = model->data(treeIndex, OpcUaModel::DisplayNameRole).toString();
        record.dataType = model->data(treeIndex, OpcUaModel::DataTypeRole).toString();

        m_dataModel->addRow(record);

        // The browse already resolved AccessLevel, so the row knows whether it
        // can be written before the user reaches for the editor.
        m_dataModel->setAccessLevelForNode(
            nodeId, model->data(treeIndex, OpcUaModel::AccessLevelRole).toInt());

        model->setMonitoringEnabledAt(treeIndex, true);
        if (connected())
            emit subscribeNodeRequested(nodeId, double(record.samplingIntervalMs));
    } else {
        const int row = m_dataModel->rowCount();
        for (int i = 0; i < row; ++i) {
            if (m_dataModel->nodeIdAt(i) == nodeId) {
                m_dataModel->removeAt(i);
                break;
            }
        }
        model->setMonitoringEnabledAt(treeIndex, false);
        emit unsubscribeNodeRequested(nodeId);
    }

    return true;
}

/*!
 * \brief Adds or removes the node at \a treeIndex from the Data Access View.
 * \param on Whether the node should be monitored.
 *
 * Adding inserts the node into the table and starts a live subscription when
 * connected; removing reverses both steps. The monitored-node set is part of the
 * active project, so projectStateChanged() is emitted for the project manager to
 * record an unsaved change; the set is persisted when the project is saved.
 */
void OpcUaManager::setNodeMonitored(const QModelIndex &treeIndex, bool on)
{
    if (!applyNodeMonitored(treeIndex, on))
        return;

    // Keep both models' monitored-id sets in sync so later re-browses stay correct.
    refreshMonitoredNodeIds();

    // The monitored-node set belongs to the active project; mark it changed.
    emit projectStateChanged();
}

/*!
 * \brief Adds every loaded monitorable child of \a treeIndex to the Data Access View.
 * \return The number of nodes that were added.
 *
 * Performs the same per-node work as setNodeMonitored() for every child, so a
 * bulk add behaves like ticking each checkbox by hand, including subscription
 * start. The monitored-id refresh and the project-state notification happen once
 * for the whole batch instead of once per child, because each refresh walks both
 * tree models end to end. Children that are already monitored are skipped, which
 * makes repeated calls on the same branch idempotent.
 */
int OpcUaManager::monitorChildVariables(const QModelIndex &treeIndex)
{
    if (!treeIndex.isValid())
        return 0;

    OpcUaModel *model = modelForIndex(treeIndex);
    if (!model)
        return 0;

    const int rows = model->rowCount(treeIndex);

    int added = 0;
    for (int row = 0; row < rows; ++row) {
        const QModelIndex child = model->index(row, 0, treeIndex);
        if (!child.isValid())
            continue;
        if (!model->data(child, OpcUaModel::CanMonitorRole).toBool())
            continue;
        if (model->monitoringEnabledAt(child))
            continue;

        if (applyNodeMonitored(child, true))
            ++added;
    }

    // A single checkbox needs no message because the new row is the feedback; a
    // bulk add does, because the user cannot tell how many nodes qualified.
    if (added > 0) {
        refreshMonitoredNodeIds();
        emit projectStateChanged();
        notify(Diagnostics::Info, [=, this] {
            return tr("Added %n node(s) to the Data View.", nullptr, added);
        });
    } else {
        notify(Diagnostics::Warning, [this] {
            return tr("No monitorable child node was found. Expand the branch first.");
        });
    }

    return added;
}

/*!
 * \brief Returns the server-absolute browse path of the node at \a treeIndex.
 */
QString OpcUaManager::browsePathAt(const QModelIndex &treeIndex) const
{
    return buildNodePath(treeIndex);
}

/*!
 * \brief Copies \a text to the system clipboard.
 */
void OpcUaManager::copyToClipboard(const QString &text) const
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

/*!
 * \brief Removes the Data Access View row at \a row from the table.
 *
 * The monitored-node set is part of the active project, so projectStateChanged()
 * is emitted; the change is persisted when the project is saved.
 */
void OpcUaManager::removeNode(int row)
{
    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    m_dataModel->removeAt(row);
    if (m_trendModel)
        m_trendModel->dropNode(nodeId);
    emit unsubscribeNodeRequested(nodeId);
    emit projectStateChanged();
}

/*!
 * \brief Removes every Data Access View source row in \a rows.
 *
 * Duplicates are ignored and the rows are removed from the highest index down,
 * so the indexes still to be processed keep pointing at the intended rows.
 */
void OpcUaManager::removeNodes(const QList<int> &rows)
{
    QList<int> ordered = rows;
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

    for (const int row : std::as_const(ordered))
        removeNode(row);
}

/*!
 * \brief Adds the already-browsed node \a nodeId to the Data Access View.
 * \return \c true when a row was added.
 */
bool OpcUaManager::monitorNodeById(const QString &nodeId)
{
    if (nodeId.isEmpty())
        return false;

    QModelIndex treeIndex;
    if (m_treeModel)
        treeIndex = m_treeModel->indexForNodeId(nodeId);
    if (!treeIndex.isValid() && m_focusModel)
        treeIndex = m_focusModel->indexForNodeId(nodeId);
    if (!treeIndex.isValid())
        return false;

    OpcUaModel *model = modelForIndex(treeIndex);
    if (!model)
        return false;
    if (!model->data(treeIndex, OpcUaModel::CanMonitorRole).toBool())
        return false;
    if (model->monitoringEnabledAt(treeIndex))
        return false;

    setNodeMonitored(treeIndex, true);
    return true;
}

/*!
 * \brief Sets the sampling interval of source row \a row to \a intervalMs.
 *
 * The backend cannot retune a live monitored item, so a changed interval drops
 * the subscription and creates a new one. Nothing happens while disconnected
 * beyond storing the value; the interval is applied when the session reconnects.
 */
void OpcUaManager::setSamplingInterval(int row, int intervalMs)
{
    if (!m_dataModel || !m_dataModel->setSamplingIntervalAt(row, intervalMs))
        return;

    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (!nodeId.isEmpty() && connected()) {
        emit unsubscribeNodeRequested(nodeId);
        emit subscribeNodeRequested(nodeId, double(m_dataModel->samplingIntervalAt(row)));
    }

    const int applied = m_dataModel->samplingIntervalAt(row);
    notify(Diagnostics::Info, [=, this] {
        return applied > 0
                   ? tr("Sampling interval set to %1 ms.").arg(applied)
                   : tr("Sampling interval reset to the default.");
    });

    emit projectStateChanged();
}

/*!
 * \brief Writes the Data Access View to \a fileUrl as UTF-8 CSV.
 * \param fileUrl Target file, as a local file URL.
 * \param viewRows View rows to export, in display order.
 * \param columns Column indexes to export, in display order.
 * \return \c true on success.
 *
 * Exports exactly what the table shows, so the current sorting, quick filter,
 * column selection, and view row numbering all carry over into the file. A field
 * that would read as a spreadsheet formula is made inert, because the values come
 * from the connected server. A byte order mark is
 * written because spreadsheet applications otherwise misread UTF-8 on Windows.
 */
bool OpcUaManager::exportDataViewCsv(const QUrl &fileUrl,
                                     const QList<int> &viewRows,
                                     const QList<int> &columns)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        applyLastError(tr("No target file was selected for the CSV export."));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        applyLastError(tr("The CSV file could not be opened for writing: %1")
                           .arg(file.errorString()));
        return false;
    }

    QString text;
    text.append(QChar(0xFEFF));

    QStringList headerCells;
    headerCells.reserve(columns.size());
    for (const int column : columns)
        headerCells.append(csvField(m_dataModel->columnTitle(column)));
    text.append(headerCells.join(QLatin1Char(',')));
    text.append(QLatin1String("\r\n"));

    for (const int viewRow : viewRows) {
        QStringList cells;
        cells.reserve(columns.size());
        for (const int column : columns) {
            const QModelIndex index = m_dataViewModel->index(viewRow, column);
            cells.append(csvField(m_dataViewModel->data(index, Qt::DisplayRole).toString()));
        }
        text.append(cells.join(QLatin1Char(',')));
        text.append(QLatin1String("\r\n"));
    }

    file.write(text.toUtf8());
    if (!file.commit()) {
        applyLastError(tr("The CSV file could not be written: %1").arg(file.errorString()));
        return false;
    }

    notify(Diagnostics::Info, [this, rows = int(viewRows.size()), path] {
        return tr("Exported %n row(s) to %1.", nullptr, rows)
            .arg(QFileInfo(path).fileName());
    });
    return true;
}

/*!
 * \brief Returns \a viewRows rendered as tab-separated text with a header line.
 * \param viewRows View rows to render, in display order.
 * \param columns Column indexes to render, in display order.
 */
QString OpcUaManager::dataViewRowsAsText(const QList<int> &viewRows,
                                         const QList<int> &columns) const
{
    QStringList lines;
    lines.reserve(viewRows.size() + 1);

    QStringList headerCells;
    headerCells.reserve(columns.size());
    for (const int column : columns)
        headerCells.append(plainCell(m_dataModel->columnTitle(column)));
    lines.append(headerCells.join(QLatin1Char('\t')));

    for (const int viewRow : viewRows) {
        QStringList cells;
        cells.reserve(columns.size());
        for (const int column : columns) {
            const QModelIndex index = m_dataViewModel->index(viewRow, column);
            cells.append(plainCell(m_dataViewModel->data(index, Qt::DisplayRole).toString()));
        }
        lines.append(cells.join(QLatin1Char('\t')));
    }

    return lines.join(QLatin1Char('\n'));
}

/*!
 * \brief Requests the attributes of the node at \a treeIndex for the panel.
 *
 * Only the most recent request is applied; earlier in-flight results are ignored
 * so that fast selection changes do not show stale attributes.
 */
void OpcUaManager::requestAttributes(const QModelIndex &treeIndex)
{
    if (!treeIndex.isValid())
        return;

    const QString nodeId = modelForIndex(treeIndex)->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    selectNode(nodeId);
}

/*!
 * \brief Selects the Data Access View row at \a row, driving the shared node selection.
 *
 * The Data Access View only knows the node id string, so it resolves the id from
 * the table model and selects it directly instead of going through a tree index.
 */
void OpcUaManager::selectDataRow(int row)
{
    if (!m_dataModel)
        return;

    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    selectNode(nodeId);
}

/*!
 * \internal
 * \brief Makes \a nodeId the selected node and requests its attributes and value.
 *
 * Centralizes selection so that clicks in the address-space tree and the Data
 * Access View share one selected-node state. Only the most recent request is
 * applied; earlier in-flight results are ignored so that fast selection changes
 * do not show stale attributes. The selected node id is exposed to QML so both
 * panels can highlight the matching row.
 */
void OpcUaManager::selectNode(const QString &nodeId)
{
    const quint64 requestId = ++m_nextAttributeRequestId;
    m_pendingAttributeRequestId = requestId;
    m_pendingStructuredRequestId = requestId;

    if (m_selectedNodeId != nodeId) {
        m_selectedNodeId = nodeId;
        emit selectedNodeIdChanged();
    }

    emit readAttributesRequested(nodeId, requestId);
    emit readStructuredValueRequested(nodeId, requestId);
}

/*!
 * \internal
 * \brief Returns the owned model that produced \a index.
 *
 * A tree \a index carries a pointer to its source model, so a click in the focus
 * segment is routed to the focus model and any other index to the main tree model.
 */
OpcUaModel *OpcUaManager::modelForIndex(const QModelIndex &index) const
{
    return index.model() == m_focusModel ? m_focusModel : m_treeModel;
}

/*!
 * \brief Pins the node at the tree \a treeIndex as the focus node.
 *
 * Resolves the node id, absolute path, and display name from the source model and
 * re-roots the focus segment on that node. The index may come from either model.
 */
void OpcUaManager::setFocusNodeFromIndex(const QModelIndex &treeIndex)
{
    if (!treeIndex.isValid())
        return;

    OpcUaModel *model = modelForIndex(treeIndex);
    const QString nodeId = model->nodeIdAt(treeIndex);
    if (nodeId.isEmpty())
        return;

    const QString name = model->data(treeIndex, OpcUaModel::DisplayNameRole).toString();
    const QString path = buildNodePath(treeIndex);
    setFocusNode(nodeId, path, name);
}

/*!
 * \internal
 * \brief Pins \a nodeId as the focus node and updates the focus segment.
 * \param absolutePath The server-absolute display path of the node.
 * \param displayName The display name shown as the focus segment title.
 */
void OpcUaManager::setFocusNode(const QString &nodeId,
                                const QString &absolutePath,
                                const QString &displayName)
{
    if (nodeId.isEmpty())
        return;

    m_focusNodeId = nodeId;
    m_focusNodePath = absolutePath;
    m_focusNodeName = displayName.isEmpty() ? nodeId : displayName;

    applyFocusNodeToModel();
    emit focusNodeChanged();
    emit projectStateChanged();
}

/*!
 * \brief Clears the pinned focus node and empties the focus segment.
 */
void OpcUaManager::clearFocusNode()
{
    m_focusNodeId.clear();
    m_focusNodePath.clear();
    m_focusNodeName.clear();

    if (m_focusModel)
        m_focusModel->clear();

    emit focusNodeChanged();
    emit projectStateChanged();
}

/*!
 * \internal
 * \brief Applies the pinned focus node to the focus model for the current session.
 *
 * The focus model is re-rooted at the pinned node and activated only while a
 * session is connected; otherwise it is cleared so the segment stays empty.
 */
void OpcUaManager::applyFocusNodeToModel()
{
    if (!m_focusModel)
        return;

    if (m_focusNodeId.isEmpty() || !connected()) {
        m_focusModel->clear();
        return;
    }

    m_focusModel->setRootNode(m_focusNodeId, m_focusNodeName);
    m_focusModel->setConnectionActive(true);
}

/*!
 * \internal
 * \brief Assigns a routed browse id for \a model and forwards the request.
 *
 * Both tree models generate their own request ids, which can collide. Each browse
 * is remapped to a manager-global id sent to the service, so the reply is routed
 * back to the model that asked for it with the request id that model expects.
 */
void OpcUaManager::routeFetch(OpcUaModel *model,
                              const QString &parentNodeId,
                              quint64 modelRequestId)
{
    const quint64 browseId = ++m_nextBrowseRequestId;
    m_browseRouting.insert(browseId, BrowseRoute{model, modelRequestId});
    emit browseChildrenRequested(parentNodeId, browseId);
}

/*!
 * \internal
 * \brief Recomputes hasLastConnection() from the active project connection.
 */
void OpcUaManager::updateHasLastConnection()
{
    const bool has = m_connection.isConfigured();
    if (has == m_hasLastConnection)
        return;

    m_hasLastConnection = has;
    emit hasLastConnectionChanged();
}

/*!
 * \internal
 * \brief Rebuilds m_connection from the live session and emits on a real change.
 *
 * Called after a successful connect. When the resulting configuration differs
 * from the one currently held (for example, a brand-new project connecting for
 * the first time), projectStateChanged() is emitted so the project manager can
 * record the unsaved change. Reconnecting with the same parameters an opened
 * project already carries produces no change and no dirty flag.
 */
void OpcUaManager::updateConnectionFromLiveState()
{
    ProjectConnectionConfig live;
    {
        QMutexLocker locker(&m_stateMutex);
        live.backend = m_backend;
        live.authMode = m_authMode;
        live.endpointUrlRewriteEnabled = m_endpointUrlRewriteEnabled;
    }
    live.discoveryUrl = m_lastDiscoveryUrl;
    live.server = m_currentServer;
    live.endpoint = m_lastEndpointDisplay;
    live.userName = live.authMode == 1 ? m_lastUserName : QString();

    const bool changed = live != m_connection;
    m_connection = live;
    updateHasLastConnection();
    if (changed) {
        emit projectStateChanged();
        emit connectionSummaryChanged();
    }
}

/*!
 * \brief Reconnects using the active project's stored connection configuration.
 *
 * Selects the backend and authentication mode and starts server discovery.
 * Username authentication needs a password, which is never stored, so
 * passwordRequired() is emitted and the run resumes in provideReconnectPassword().
 */
void OpcUaManager::connectToLast()
{
    if (!m_connection.isConfigured()) {
        applyLastError(tr("No stored connection is available."));
        return;
    }

    notify(Diagnostics::Info, [this, summary = connectionSummary()] {
        return tr("Reconnecting to %1…").arg(summary);
    });
    connectUsingConfig(m_connection);
}

/*!
 * \brief Connects using the active project's stored connection configuration, if any.
 *
 * Called right after a project is opened. Does nothing when the project has no
 * connection configured, so a freshly created project stays disconnected until
 * the user configures a connection.
 */
void OpcUaManager::connectToProjectConnection()
{
    if (!m_connection.isConfigured())
        return;
    connectUsingConfig(m_connection);
}

/*!
 * \brief Connects the client to a local endpoint at \a discoveryUrl.
 *
 * Builds an anonymous open62541 connection for the given discovery URL and
 * drives it through the shared reconnect state machine, exactly like reopening
 * a project. Endpoint URL rewriting is enabled so an endpoint the local runtime
 * advertises under a different host still resolves to the discovery host. The
 * empty stored endpoint makes the state machine fall back to the first endpoint.
 */
void OpcUaManager::connectToLocalEndpoint(const QString &discoveryUrl)
{
    if (discoveryUrl.isEmpty()) {
        applyLastError(tr("No local endpoint is available."));
        return;
    }

    ProjectConnectionConfig config;
    config.discoveryUrl = discoveryUrl;
    config.backend = QStringLiteral("open62541");
    config.authMode = 0; // anonymous
    config.endpointUrlRewriteEnabled = true;

    notify(Diagnostics::Info, [this, discoveryUrl] {
        return tr("Connecting to %1…").arg(discoveryUrl);
    });
    connectUsingConfig(config);
}

/*!
 * \internal
 * \brief Seeds the reconnect state machine from \a config and starts connecting.
 *
 * Shared by connectToLast() and connectToProjectConnection(). The pinned focus
 * node is not restored here; applyProject() already loaded it before this runs.
 */
void OpcUaManager::connectUsingConfig(const ProjectConnectionConfig &config)
{
    if (connected() || busy())
        return;
    if (!config.isConfigured()) {
        applyLastError(tr("No stored connection is available."));
        return;
    }

    m_reconnectDiscoveryUrl = config.discoveryUrl;
    m_reconnectServer = config.server;
    m_reconnectEndpoint = config.endpoint;
    m_reconnectAuthMode = config.authMode;
    m_reconnectUser = config.userName;

    setEndpointUrlRewriteEnabled(config.endpointUrlRewriteEnabled);

    if (!config.backend.isEmpty())
        setBackend(config.backend);

    // Username authentication (authMode 1) needs a password, which is never
    // stored: ask QML for it and continue in provideReconnectPassword().
    if (m_reconnectAuthMode == 1) {
        m_reconnectStage = ReconnectStage::AwaitingPassword;
        emit passwordRequired(m_reconnectUser);
        return;
    }

    if (m_reconnectAuthMode == 2)
        setCertificateAuthentication();
    else
        setAnonymousAuthentication();

    startReconnectDiscovery();
}

/*!
 * \brief Supplies the \a password and resumes a waiting connectToLast() run.
 */
void OpcUaManager::provideReconnectPassword(const QString &password)
{
    if (m_reconnectStage != ReconnectStage::AwaitingPassword)
        return;

    setUsernameAuthentication(m_reconnectUser, password);
    startReconnectDiscovery();
}

/*!
 * \internal
 * \brief Starts server discovery for an in-progress connectToLast() run.
 */
void OpcUaManager::startReconnectDiscovery()
{
    m_reconnectStage = ReconnectStage::DiscoveringServers;
    discoverServers(m_reconnectDiscoveryUrl);
}

/*!
 * \internal
 * \brief Aborts an in-progress connectToLast() run and reports \a reason.
 */
void OpcUaManager::abortReconnect(const QString &reason)
{
    if (m_reconnectStage == ReconnectStage::Idle)
        return;

    m_reconnectStage = ReconnectStage::Idle;
    if (!reason.isEmpty())
        applyLastError(reason);
}

/*!
 * \brief Returns the current structured-value output format.
 */
OpcUaManager::ValueFormat OpcUaManager::valueFormat() const
{
    return m_valueFormat;
}

/*!
 * \brief Returns the formatted structured value text for the selected node.
 */
QString OpcUaManager::structuredValueText() const
{
    return m_structuredValueText;
}

/*!
 * \brief Returns whether a renderable structured value is available for the panel.
 */
bool OpcUaManager::structuredValueAvailable() const
{
    return m_structuredValueAvailable;
}

/*!
 * \brief Returns the node id of the currently selected node.
 */
QString OpcUaManager::selectedNodeId() const
{
    return m_selectedNodeId;
}

/*!
 * \brief Sets the structured-value output \a format and re-renders the cached value.
 *
 * The choice is persisted so it is restored on the next run. When a decoded value
 * is cached it is re-rendered immediately in the new format.
 */
void OpcUaManager::setValueFormat(ValueFormat format)
{
    if (format == m_valueFormat)
        return;

    m_valueFormat = format;
    QSettings().setValue(QLatin1String(kValueFormatSettingsKey), static_cast<int>(format));
    emit valueFormatChanged();
    emit projectStateChanged();

    if (m_structuredValueHighlighter)
        m_structuredValueHighlighter->setLanguage(toHighlighterLanguage(m_valueFormat));

    if (m_structuredValueAvailable) {
        m_structuredValueText =
            StructuredValueFormatter::format(m_structuredValueRoot, toFormatterFormat(m_valueFormat));
        emit structuredValueChanged();
    }
}

/*!
 * \brief Installs the structured-value syntax highlighter on \a document.
 *
 * Attaches a StructuredValueHighlighter to the TextArea's underlying
 * QTextDocument and seeds it with the current output format. The highlighter is
 * parented to that document, so it is destroyed with the TextArea; a QPointer
 * guards the manager's reference against that. The call is ignored when
 * \a document is null or a highlighter is already installed.
 */
void OpcUaManager::installStructuredValueHighlighter(QQuickTextDocument *document)
{
    if (!document || m_structuredValueHighlighter)
        return;

    QTextDocument *textDocument = document->textDocument();
    if (!textDocument)
        return;

    m_structuredValueHighlighter = new StructuredValueHighlighter(textDocument);
    m_structuredValueHighlighter->setLanguage(toHighlighterLanguage(m_valueFormat));
    m_structuredValueHighlighter->setDarkTheme(m_structuredValueDarkTheme);
}

/*!
 * \brief Selects the dark or light palette for the structured-value highlighter.
 *
 * The value is remembered so it can be applied when the highlighter is installed;
 * an already installed highlighter is updated immediately and re-highlights.
 */
void OpcUaManager::setStructuredValueDarkTheme(bool dark)
{
    m_structuredValueDarkTheme = dark;
    if (m_structuredValueHighlighter)
        m_structuredValueHighlighter->setDarkTheme(dark);
}

/*!
 * \brief Re-reads and re-decodes the structured value of the last selected node.
 */
void OpcUaManager::refreshStructuredValue()
{
    if (m_selectedNodeId.isEmpty())
        return;

    m_pendingStructuredRequestId = ++m_nextAttributeRequestId;
    emit readStructuredValueRequested(m_selectedNodeId, m_pendingStructuredRequestId);
}

/*!
 * \brief Writes \a value to the node backing the Data Access View row at \a row.
 */
void OpcUaManager::writeValue(int row, const QVariant &value)
{
    const QString nodeId = m_dataModel->nodeIdAt(row);
    if (nodeId.isEmpty())
        return;

    // Sending a write the AccessLevel already rules out only produces a status
    // code the user has to decode; saying so directly is more useful.
    if (m_dataModel->writabilityAt(row) == DataAccessModel::WritabilityReadOnly) {
        applyLastError(tr("%1 is read-only: the server does not grant CurrentWrite.")
                           .arg(displayNameForNodeId(nodeId)));
        return;
    }

    emit writeValueRequested(nodeId, value);
}

/*!
 * \brief Applies \a data as the active project's runtime state.
 *
 * Stores the connection configuration for a later connect, restores the pinned
 * focus node and value format into memory, and loads the monitored nodes into the
 * Data Access View. No connection is started and projectStateChanged() is not
 * emitted, because loading a project is not an unsaved change.
 */
void OpcUaManager::applyProject(const ProjectData &data)
{
    m_connection = data.connection;
    updateHasLastConnection();

    // Restore the pinned focus node into memory; it is applied to the focus model
    // once a session connects, in applyFocusNodeToModel().
    m_focusNodeId = data.focusNode.nodeId;
    m_focusNodePath = data.focusNode.path;
    m_focusNodeName = data.focusNode.displayName.isEmpty()
                          ? data.focusNode.nodeId
                          : data.focusNode.displayName;
    emit focusNodeChanged();

    // Restore the per-project value format without re-persisting it.
    const ValueFormat format = data.settings.valueFormat == FormatXml ? FormatXml : FormatJson;
    if (format != m_valueFormat) {
        m_valueFormat = format;
        if (m_structuredValueHighlighter)
            m_structuredValueHighlighter->setLanguage(toHighlighterLanguage(m_valueFormat));
        emit valueFormatChanged();
    }

    // Load the monitored nodes into the Data Access View and seed the tree so the
    // monitoring checkbox is restored for those nodes when the address space is
    // browsed after connecting.
    m_dataModel->setRecords(data.monitoredNodes);
    refreshMonitoredNodeIds();

    // Restore the table layout without re-persisting it; an empty map from a
    // version 1 project leaves the table on its built-in defaults.
    if (m_dataViewState != data.settings.dataView) {
        m_dataViewState = data.settings.dataView;
        emit dataViewStateChanged();
    }

    emit connectionSummaryChanged();
}

/*!
 * \brief Snapshots the current runtime state into a ProjectData for saving.
 *
 * The display name is left empty for the project manager to fill in, since it
 * owns the project's identity and file path.
 */
ProjectData OpcUaManager::exportProject() const
{
    ProjectData data;
    data.formatVersion = kProjectFormatVersion;
    data.connection = m_connection;
    data.focusNode.nodeId = m_focusNodeId;
    data.focusNode.path = m_focusNodePath;
    data.focusNode.displayName = m_focusNodeName;
    if (m_dataModel)
        data.monitoredNodes = m_dataModel->records();
    data.settings.valueFormat = static_cast<int>(m_valueFormat);
    data.settings.dataView = m_dataViewState;
    return data;
}

/*!
 * \brief Releases all project runtime state.
 *
 * Disconnects any active session, clears the tree, focus, data, and attribute
 * models, and resets the stored connection, focus node, and selection so no stale
 * OPC UA state remains after a project is closed or switched.
 */
void OpcUaManager::clearRuntimeState()
{
    if (connected())
        disconnectFromServer();

    if (m_treeModel)
        m_treeModel->clear();
    if (m_focusModel)
        m_focusModel->clear();
    if (m_dataModel)
        m_dataModel->setRecords({});
    if (m_trendModel)
        m_trendModel->clear();
    if (m_dataViewModel) {
        m_dataViewModel->setFilterText(QString());
        m_dataViewModel->applySort(-1, Qt::AscendingOrder);
    }
    if (m_attributesModel)
        m_attributesModel->clear();

    if (!m_dataViewState.isEmpty()) {
        m_dataViewState.clear();
        emit dataViewStateChanged();
    }
    setUpdatesPaused(false);
    emit connectionSummaryChanged();

    m_focusNodeId.clear();
    m_focusNodePath.clear();
    m_focusNodeName.clear();
    emit focusNodeChanged();

    if (!m_selectedNodeId.isEmpty()) {
        m_selectedNodeId.clear();
        emit selectedNodeIdChanged();
    }

    m_connection = ProjectConnectionConfig{};
    updateHasLastConnection();
    m_reconnectStage = ReconnectStage::Idle;
}

/*!
 * \brief Returns whether legacy pre-project state exists to migrate into a project.
 */
bool OpcUaManager::hasLegacyState() const
{
    if (m_settings) {
        if (!m_settings->value(QStringLiteral("LastConnection/discoveryUrl")).toString().isEmpty())
            return true;
        if (!m_settings->value(QStringLiteral("LastFocusNode/nodeId")).toString().isEmpty())
            return true;
    }
    if (m_nodeDatabase && m_nodeDatabase->isOpen() && !m_nodeDatabase->loadAll().isEmpty())
        return true;
    return false;
}

/*!
 * \brief Builds a ProjectData from legacy INI, native, and SQLite state.
 *
 * Reads the INI last-connection and focus-node groups, the native value format,
 * and the SQLite monitored-node table so a one-time migration can preserve the
 * previous single-session state as a Default project. The password is not part of
 * the legacy state and is never migrated.
 */
ProjectData OpcUaManager::exportLegacyState() const
{
    ProjectData data;
    data.formatVersion = kProjectFormatVersion;

    if (m_settings) {
        m_settings->beginGroup(QStringLiteral("LastConnection"));
        data.connection.discoveryUrl = m_settings->value(QStringLiteral("discoveryUrl")).toString();
        data.connection.backend = m_settings->value(QStringLiteral("backend")).toString();
        data.connection.server = m_settings->value(QStringLiteral("server")).toString();
        data.connection.endpoint = m_settings->value(QStringLiteral("endpoint")).toString();
        data.connection.authMode = m_settings->value(QStringLiteral("authMode"), 0).toInt();
        data.connection.userName = m_settings->value(QStringLiteral("userName")).toString();
        m_settings->endGroup();

        m_settings->beginGroup(QStringLiteral("LastFocusNode"));
        data.focusNode.nodeId = m_settings->value(QStringLiteral("nodeId")).toString();
        data.focusNode.path = m_settings->value(QStringLiteral("path")).toString();
        data.focusNode.displayName = m_settings->value(QStringLiteral("displayName")).toString();
        m_settings->endGroup();
    }

    data.settings.valueFormat =
        QSettings().value(QLatin1String(kValueFormatSettingsKey), FormatJson).toInt();

    if (m_nodeDatabase && m_nodeDatabase->isOpen())
        data.monitoredNodes = m_nodeDatabase->loadAll();

    return data;
}

/*!
 * \brief Connects the GUI facade to the worker-thread OPC UA service.
 * All command signals are queued to \a service, and all service
 * results are queued back to this GUI-thread object. The function is idempotent
 * for the same service pointer and emits initializeRequested() after wiring.
 */
void OpcUaManager::attachService(OpcUaService *service)
{
    if (!service || m_service == service)
        return;
    m_service = service;

    connect(this, &OpcUaManager::initializeRequested,
            service, &OpcUaService::initialize, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setBackendRequested,
            service, &OpcUaService::setBackend, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setAnonymousAuthenticationRequested,
            service, &OpcUaService::setAnonymousAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setUsernameAuthenticationRequested,
            service, &OpcUaService::setUsernameAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setCertificatePrivateKeyPasswordRequested,
            service, &OpcUaService::setCertificatePrivateKeyPassword, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setCertificateAuthenticationRequested,
            service, &OpcUaService::setCertificateAuthentication, Qt::QueuedConnection);
    connect(this, &OpcUaManager::setEndpointUrlRewriteEnabledRequested,
            service, &OpcUaService::setEndpointUrlRewriteEnabled, Qt::QueuedConnection);
    connect(this, &OpcUaManager::discoverServersRequested,
            service, &OpcUaService::discoverServers, Qt::QueuedConnection);
    connect(this, &OpcUaManager::requestEndpointsRequested,
            service, &OpcUaService::requestEndpoints, Qt::QueuedConnection);
    connect(this, &OpcUaManager::requestEndpointsForServerRequested,
            service, &OpcUaService::requestEndpointsForServer, Qt::QueuedConnection);
    connect(this, &OpcUaManager::connectToEndpointRequested,
            service, &OpcUaService::connectToEndpoint, Qt::QueuedConnection);
    connect(this, &OpcUaManager::disconnectFromServerRequested,
            service, &OpcUaService::disconnectFromServer, Qt::QueuedConnection);
    connect(this, &OpcUaManager::browseChildrenRequested,
            service, &OpcUaService::browseChildren, Qt::QueuedConnection);
    connect(this, &OpcUaManager::cloneBrowseRequested,
            service, &OpcUaService::browseForClone, Qt::QueuedConnection);

    connect(service, &OpcUaService::availableBackendsChanged,
            this, &OpcUaManager::applyAvailableBackends, Qt::QueuedConnection);
    connect(service, &OpcUaService::backendChanged,
            this, &OpcUaManager::applyBackend, Qt::QueuedConnection);
    connect(service, &OpcUaService::serversChanged,
            this, &OpcUaManager::applyServers, Qt::QueuedConnection);
    connect(service, &OpcUaService::endpointsChanged,
            this, &OpcUaManager::applyEndpoints, Qt::QueuedConnection);
    connect(service, &OpcUaService::connectedChanged,
            this, &OpcUaManager::applyConnected, Qt::QueuedConnection);
    connect(service, &OpcUaService::operationStateChanged,
            this, &OpcUaManager::applyOperationState, Qt::QueuedConnection);
    connect(service, &OpcUaService::clientStateChanged,
            this, &OpcUaManager::applyClientState, Qt::QueuedConnection);
    connect(service, &OpcUaService::endpointUrlRewriteEnabledChanged,
            this, &OpcUaManager::applyEndpointUrlRewriteEnabled, Qt::QueuedConnection);
    connect(service, &OpcUaService::lastErrorChanged,
            this, &OpcUaManager::applyLastError, Qt::QueuedConnection);
    connect(service, &OpcUaService::authModeChanged,
            this, &OpcUaManager::applyAuthMode, Qt::QueuedConnection);
    connect(service, &OpcUaService::browseChildrenReady,
            this, &OpcUaManager::applyBrowseChildren, Qt::QueuedConnection);
    // The clone result is relayed straight to GUI consumers (Server Studio).
    connect(service, &OpcUaService::cloneSnapshotReady,
            this, &OpcUaManager::cloneSnapshotReady, Qt::QueuedConnection);

    // Both the main tree and the focus segment browse through the service. Each
    // model's request is remapped to a manager-global id so the reply is routed
    // back to the model that asked for it (see routeFetch/applyBrowseChildren).
    connect(m_treeModel, &OpcUaModel::fetchChildrenRequested, this,
            [this](const QString &parentNodeId, quint64 requestId) {
                routeFetch(m_treeModel, parentNodeId, requestId);
            });
    connect(m_focusModel, &OpcUaModel::fetchChildrenRequested, this,
            [this](const QString &parentNodeId, quint64 requestId) {
                routeFetch(m_focusModel, parentNodeId, requestId);
            });

    connect(this, &OpcUaManager::readAttributesRequested,
            service, &OpcUaService::readNodeAttributes, Qt::QueuedConnection);
    connect(this, &OpcUaManager::readStructuredValueRequested,
            service, &OpcUaService::readStructuredValue, Qt::QueuedConnection);
    connect(this, &OpcUaManager::subscribeNodeRequested,
            service, &OpcUaService::subscribeNode, Qt::QueuedConnection);
    connect(this, &OpcUaManager::unsubscribeNodeRequested,
            service, &OpcUaService::unsubscribeNode, Qt::QueuedConnection);
    connect(this, &OpcUaManager::writeValueRequested,
            service, &OpcUaService::writeNodeValue, Qt::QueuedConnection);

    connect(service, &OpcUaService::nodeAttributesReady,
            this, &OpcUaManager::applyNodeAttributes, Qt::QueuedConnection);
    connect(service, &OpcUaService::structuredValueReady,
            this, &OpcUaManager::applyStructuredValue, Qt::QueuedConnection);
    connect(service, &OpcUaService::monitoredValueChanged,
            this, &OpcUaManager::applyMonitoredValue, Qt::QueuedConnection);
    connect(service, &OpcUaService::writeCompleted,
            this, &OpcUaManager::applyWriteCompleted, Qt::QueuedConnection);

    emit initializeRequested();
}

/*!
 * \brief Mirrors backend plugin names from the worker service.
 * \param backends The available backend plugin names.
 */
void OpcUaManager::applyAvailableBackends(const QStringList &backends)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_availableBackends == backends)
        return;
    m_availableBackends = backends;
    locker.unlock();
    emit opcUaBackendChanged();
}

/*!
 * \brief Mirrors the selected backend from the worker service.
 * \param backend The backend plugin name.
 */
void OpcUaManager::applyBackend(const QString &backend)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_backend == backend)
        return;
    m_backend = backend;
    locker.unlock();
    emit backendChanged();
}

/*!
 * \brief Mirrors discovered server display rows from the worker service.
 * \param servers List of server display strings.
 */
void OpcUaManager::applyServers(const QStringList &servers)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_servers != servers) {
            m_servers = servers;
            changed = true;
        }
    }
    if (changed)
        emit serversChanged();

    if (m_reconnectStage != ReconnectStage::DiscoveringServers)
        return;

    // discoverServers() clears the list and emits an empty serversChanged() before
    // the network result arrives. That transient empty emission must not abort an
    // in-progress reconnect; wait for the populated result instead.
    if (servers.isEmpty())
        return;

    // Pick the stored server: exact display match first, then a substring match,
    // then the first server as a fallback.
    int index = servers.indexOf(m_reconnectServer);
    if (index < 0 && !m_reconnectServer.isEmpty()) {
        for (int i = 0; i < servers.size(); ++i) {
            if (servers.at(i).contains(m_reconnectServer)) {
                index = i;
                break;
            }
        }
    }
    if (index < 0 && !servers.isEmpty())
        index = 0;
    if (index < 0) {
        abortReconnect(tr("The stored server was not found during discovery."));
        return;
    }

    m_reconnectStage = ReconnectStage::RequestingEndpoints;
    requestEndpointsForServer(index);
}

/*!
 * \brief Mirrors endpoint display rows from the worker service.
 * \param endpoints List of endpoint display strings.
 */
void OpcUaManager::applyEndpoints(const QStringList &endpoints)
{
    bool changed = false;
    {
        QMutexLocker locker(&m_stateMutex);
        if (m_endpoints != endpoints) {
            m_endpoints = endpoints;
            changed = true;
        }
    }
    if (changed)
        emit endpointsChanged();

    if (m_reconnectStage != ReconnectStage::RequestingEndpoints)
        return;

    // Endpoint discovery clears the list first, like server discovery; ignore the
    // transient empty emission and wait for the populated endpoint list.
    if (endpoints.isEmpty())
        return;

    // Pick the stored endpoint by exact display match, else the first endpoint.
    int index = endpoints.indexOf(m_reconnectEndpoint);
    if (index < 0 && !endpoints.isEmpty())
        index = 0;
    if (index < 0) {
        abortReconnect(tr("The stored endpoint was not found."));
        return;
    }

    m_reconnectStage = ReconnectStage::Connecting;
    connectToEndpoint(index);
}

/*!
 * \brief Mirrors connection state and updates the tree model activation state.
 * \param connected Whether the service is connected.
 */
void OpcUaManager::applyConnected(bool connected)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_connected == connected)
        return;
    m_connected = connected;
    locker.unlock();

    if (m_treeModel)
        m_treeModel->setConnectionActive(connected);

    notify(Diagnostics::Info, [this, connected, summary = connectionSummary()] {
        return connected ? tr("Connected to %1.").arg(summary)
                         : tr("Disconnected from the server.");
    });

    if (connected) {
        // Seed the tree with the persisted monitored node ids before its browse
        // results arrive, so the monitoring checkbox is restored for those nodes.
        refreshMonitoredNodeIds();

        // Restore the pinned focus segment now that the session is active.
        applyFocusNodeToModel();

        // Capture the parameters that produced this successful connection into the
        // active project's connection config and mark any reconnect as finished.
        updateConnectionFromLiveState();
        m_reconnectStage = ReconnectStage::Idle;

        // Re-establish subscriptions for every persisted monitored node so the
        // Data Access View resumes updating after a (re)connect.
        const int rows = m_dataModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            const QString nodeId = m_dataModel->nodeIdAt(i);
            if (!nodeId.isEmpty())
                emit subscribeNodeRequested(nodeId,
                                            double(m_dataModel->samplingIntervalAt(i)));
        }
    } else {
        if (m_focusModel)
            m_focusModel->clear();
        // Drop routes for browses that will never return after the session ended.
        m_browseRouting.clear();
        m_dataModel->clearValues();
        // A reconnect starts a new history: joining the samples across the gap
        // would draw a straight line through a period with no data at all.
        if (m_trendModel)
            m_trendModel->clear();
        m_attributesModel->clear();
    }

    emit connectedChanged();
}

/*!
 * \brief Mirrors operation state and emits busyChanged() when the derived busy value changes.
 * \param operationState The current operation state as an integer.
 */
void OpcUaManager::applyOperationState(int operationState)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_operationState == operationState)
        return;
    const bool wasBusy = m_operationState != OperationIdle;
    m_operationState = operationState;
    const bool isBusy = m_operationState != OperationIdle;
    locker.unlock();

    emit operationStateChanged();
    if (wasBusy != isBusy)
        emit busyChanged();
}

/*!
 * \brief Mirrors the underlying client state from the worker service.
 * \param clientState The current client state as an integer.
 */
void OpcUaManager::applyClientState(int clientState)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_clientState == clientState)
        return;
    m_clientState = clientState;
    locker.unlock();
    emit clientStateChanged();
}

/*!
 * \brief Mirrors endpoint URL rewrite state from the worker service.
 * \param enabled Whether endpoint URL rewriting is enabled.
 */
void OpcUaManager::applyEndpointUrlRewriteEnabled(bool enabled)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_endpointUrlRewriteEnabled == enabled)
        return;
    m_endpointUrlRewriteEnabled = enabled;
    locker.unlock();
    emit endpointUrlRewriteEnabledChanged();
}

/*!
 * \brief Mirrors the last user-visible error text from the worker service.
 * \param lastError The error text.
 */
void OpcUaManager::applyLastError(const QString &lastError)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_lastError == lastError)
        return;
    m_lastError = lastError;
    locker.unlock();

    // A real error during an automatic reconnect ends the run so the state machine
    // does not keep matching stale discovery results.
    if (!lastError.isEmpty()
        && m_reconnectStage != ReconnectStage::Idle
        && m_reconnectStage != ReconnectStage::AwaitingPassword) {
        m_reconnectStage = ReconnectStage::Idle;
    }

    emit lastErrorChanged();

    // Clearing the error is not worth reporting; a new one always is, because
    // the connection dialog that used to be its only home is usually closed.
    if (!lastError.isEmpty()) {
        notify(Diagnostics::Error, [lastError] {
            return OpcUaStatusHint::describe(lastError);
        });
    }
}

/*!
 * \brief Mirrors the authentication mode from the worker service.
 * \param authMode The authentication mode as an integer.
 */
void OpcUaManager::applyAuthMode(int authMode)
{
    QMutexLocker locker(&m_stateMutex);
    if (m_authMode == authMode)
        return;
    m_authMode = authMode;
    locker.unlock();
    emit authModeChanged();
}

/*!
 * \brief Forwards browse results from the worker service into the GUI tree model.
 * \param parentNodeId The parent node identifier.
 * \param requestId The request identifier.
 * \param children List of child node data.
 * \param success Whether the browse operation succeeded.
 */
void OpcUaManager::applyBrowseChildren(const QString &parentNodeId,
                                       quint64 requestId,
                                       const QList<OpcUaNodeData> &children,
                                       bool success)
{
    const auto it = m_browseRouting.find(requestId);
    if (it == m_browseRouting.end())
        return;

    const BrowseRoute route = it.value();
    m_browseRouting.erase(it);

    if (route.model)
        route.model->applyChildrenSnapshot(parentNodeId, route.modelRequestId, children, success);
}

/*!
 * \brief Applies node attributes for \a requestId to the Attributes panel model.
 * \param data The attribute snapshot read from the server.
 * \param success Whether the read succeeded.
 *
 * Stale results from superseded requests are ignored so the panel always shows
 * the attributes of the most recently selected node.
 */
void OpcUaManager::applyNodeAttributes(quint64 requestId,
                                       const OpcUaAttributeData &data,
                                       bool success)
{
    if (requestId != m_pendingAttributeRequestId)
        return;

    if (!success) {
        m_attributesModel->clear();
        if (!m_selectedEnumOptions.isEmpty()) {
            m_selectedEnumOptions.clear();
            emit selectedNodeAttributesChanged();
        }
        return;
    }

    m_attributesModel->setAttributes(data);

    // A row already in the table learns its writability from this read too, so a
    // node restored from a project stops being unknown as soon as it is selected.
    if (m_dataModel)
        m_dataModel->setAccessLevelForNode(data.nodeId, data.accessLevel);

    QVariantList options;
    options.reserve(data.enumOptions.size());
    for (const auto &option : data.enumOptions) {
        options.append(QVariantMap {
            {QStringLiteral("value"), QVariant::fromValue(option.first)},
            {QStringLiteral("label"), option.second}
        });
    }

    m_selectedEnumOptions = options;
    emit selectedNodeAttributesChanged();
}

/*!
 * \brief Applies the decoded value tree for \a requestId and \a nodeId to the View panel.
 * \param root The decoded value tree read from the server.
 * \param success Whether the read and decode succeeded.
 *
 * Stale results from superseded requests are ignored. The tree is cached so the
 * text can be re-rendered when the output format changes. A value is considered
 * available when it is a structure or array, or a scalar with a valid value, so
 * that non-variable nodes (folders and objects) leave the panel empty.
 */
void OpcUaManager::applyStructuredValue(quint64 requestId,
                                        const QString &nodeId,
                                        const OpcUaValueTreeNode &root,
                                        bool success)
{
    Q_UNUSED(nodeId)
    if (requestId != m_pendingStructuredRequestId)
        return;

    const bool hasContent = success
        && (root.kind != OpcUaValueTreeNode::Kind::Scalar || root.scalarValue.isValid());

    m_structuredValueRoot = root;
    m_structuredValueAvailable = hasContent;
    m_structuredValueText = hasContent
        ? StructuredValueFormatter::format(root, toFormatterFormat(m_valueFormat))
        : QString();
    emit structuredValueChanged();
}

/*!
 * \brief Applies a live value \a update to the Data Access View table model.
 */
void OpcUaManager::applyMonitoredValue(const OpcUaValueUpdate &update)
{
    // The trend records every update regardless of the table's pause. Its own
    // pause freezes the time axis and keeps collecting, so dropping samples here
    // would leave a hole the curve then spans with a straight line, showing
    // values the variable never had.
    recordTrendSample(update);

    // While paused the table keeps the values the user is reading. The
    // subscription stays active, so the row catches up on the next data change.
    if (m_updatesPaused)
        return;

    m_dataModel->updateValue(update);
}

/*!
 * \brief Applies a write result for \a nodeId, surfacing \a error when it failed.
 * \param success Whether the write succeeded.
 *
 * Successful writes are reflected through the active subscription, so only
 * failures need to update the user-visible error text.
 */
void OpcUaManager::applyWriteCompleted(const QString &nodeId, bool success, const QString &error)
{
    if (success) {
        notify(Diagnostics::Info, [this, name = displayNameForNodeId(nodeId)] {
            return tr("Wrote the value of %1.").arg(name);
        });
        return;
    }

    qWarning() << "OpcUaManager: write failed for" << nodeId << ":" << error;
    applyLastError(tr("Writing %1 failed: %2")
                       .arg(displayNameForNodeId(nodeId), error));
}

/*!
 * \brief Records \a update in the trend history when its value is numeric.
 *
 * Booleans are mapped to 1 and 0 so they can share the plot with numbers; text
 * values have no position on a value axis and are skipped, which simply leaves
 * that node without a curve.
 */
void OpcUaManager::recordTrendSample(const OpcUaValueUpdate &update)
{
    if (!m_trendModel || update.nodeId.isEmpty())
        return;

    const QString text = update.value.trimmed();
    if (text.isEmpty())
        return;

    double value = 0.0;
    if (text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        value = 1.0;
    } else if (text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        value = 0.0;
    } else {
        bool ok = false;
        value = text.toDouble(&ok);
        if (!ok)
            return;
    }

    m_trendModel->appendSample(update.nodeId, QDateTime::currentMSecsSinceEpoch(), value);
}

/*!
 * \brief Returns the display name of \a nodeId, falling back to the node id.
 *
 * A message naming "Temperature" is far more useful than one naming
 * "ns=4;s=|var|CODESYS...", so the Data Access View row is consulted first.
 */
QString OpcUaManager::displayNameForNodeId(const QString &nodeId) const
{
    if (!m_dataModel)
        return nodeId;

    const int rows = m_dataModel->rowCount();
    for (int row = 0; row < rows; ++row) {
        if (m_dataModel->nodeIdAt(row) != nodeId)
            continue;
        const QString displayName =
            m_dataModel->data(m_dataModel->index(row, 0),
                              DataAccessModel::DisplayNameRole).toString();
        return displayName.isEmpty() ? nodeId : displayName;
    }

    return nodeId;
}
