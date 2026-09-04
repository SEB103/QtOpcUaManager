#include "qmlapi/projectmanager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include "qmlapi/opcuamanager.h"
#include "project/projectserializer.h"

namespace {
/*! \internal Maximum number of entries kept in the recent-projects list. */
constexpr int kMaxRecentProjects = 10;
/*! \internal Settings array name holding the recent-projects list. */
constexpr auto kRecentArrayKey = "RecentProjects";
/*! \internal Settings key holding the default projects directory. */
constexpr auto kDefaultProjectsDirKey = "defaultProjectsDir";

/*!
 * \internal
 * \brief Returns a comparable canonical form of \a path for de-duplication.
 *
 * Paths are cleaned and compared case-insensitively so the same project file is
 * recognized regardless of separators or drive-letter casing on Windows.
 */
QString canonicalKey(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}
} // namespace

/*!
    \class ProjectManager
    \brief GUI-thread facade that owns the active project and the recent-projects list.

    \internal
*/

/*!
 * \brief Creates a project manager with no active project and an empty recent list.
 */
ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
}

ProjectManager::~ProjectManager() = default;

bool ProjectManager::hasActiveProject() const
{
    return !m_activePath.isEmpty();
}

QString ProjectManager::activeProjectName() const
{
    return m_activeName;
}

QString ProjectManager::activeProjectPath() const
{
    return m_activePath;
}

bool ProjectManager::dirty() const
{
    return m_dirty;
}

/*!
 * \brief Returns the recent-project rows exposed to QML, newest first.
 *
 * Each row is a map with \c path, \c displayName, \c lastOpened, \c endpoint,
 * and a computed \c available flag that is false when the file is missing.
 */
QVariantList ProjectManager::recentProjects() const
{
    QVariantList rows;
    rows.reserve(m_recent.size());
    for (const RecentEntry &entry : m_recent) {
        QVariantMap row;
        row.insert(QStringLiteral("path"), entry.path);
        row.insert(QStringLiteral("displayName"), entry.displayName);
        row.insert(QStringLiteral("lastOpened"), entry.lastOpened);
        row.insert(QStringLiteral("endpoint"), entry.endpoint);
        row.insert(QStringLiteral("available"), QFileInfo::exists(entry.path));
        rows.append(row);
    }
    return rows;
}

/*!
 * \brief Returns the configured default projects directory, or the built-in default.
 *
 * When no directory has been configured, falls back to an "OpcUaManager" folder
 * under the user's Documents location.
 */
QString ProjectManager::defaultProjectsDir() const
{
    QString dir;
    if (m_settings)
        dir = m_settings->value(QLatin1String(kDefaultProjectsDirKey)).toString();

    if (dir.isEmpty()) {
        const QString documents =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        dir = QDir(documents).filePath(QStringLiteral("OpcUaManager"));
    }
    return QDir::cleanPath(dir);
}

/*!
 * \brief Sets and persists the default projects directory from \a pathOrUrl.
 */
void ProjectManager::setDefaultProjectsDir(const QString &pathOrUrl)
{
    const QString dir = QDir::cleanPath(toLocalPath(pathOrUrl));
    if (dir.isEmpty() || dir == defaultProjectsDir())
        return;

    if (m_settings) {
        m_settings->setValue(QLatin1String(kDefaultProjectsDirKey), dir);
        m_settings->sync();
    }
    QDir().mkpath(dir);
    emit defaultProjectsDirChanged();
}

/*!
 * \brief Injects the INI settings store and loads the recent-projects list.
 * \param settings Non-owning settings store, or null to disable persistence.
 */
void ProjectManager::setSettings(QSettings *settings)
{
    m_settings = settings;
    loadRecentProjects();
    maybeMigrateLegacyState();
}

void ProjectManager::setOpcUaManager(OpcUaManager *manager)
{
    m_opcUaManager = manager;
    if (m_opcUaManager) {
        // A project-relevant change in the facade marks the active project dirty.
        connect(m_opcUaManager, &OpcUaManager::projectStateChanged, this, [this]() {
            if (hasActiveProject())
                setDirty(true);
        });
    }
}

/*!
 * \brief Creates a new empty project named \a name inside \a folderPathOrUrl.
 */
bool ProjectManager::createProject(const QString &name, const QString &folderPathOrUrl)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        emit projectError(tr("The project name must not be empty."));
        return false;
    }

    const QString folder = toLocalPath(folderPathOrUrl);
    if (folder.isEmpty()) {
        emit projectError(tr("A project folder must be selected."));
        return false;
    }

    QString fileName = trimmedName;
    if (!fileName.endsWith(QStringLiteral(".uaproj"), Qt::CaseInsensitive))
        fileName += QStringLiteral(".uaproj");
    return createProjectAtPath(QDir(folder).filePath(fileName));
}

/*!
 * \brief Creates a new empty project at the .uaproj path \a pathOrUrl and opens it.
 */
bool ProjectManager::createProjectAtPath(const QString &pathOrUrl)
{
    QString path = toLocalPath(pathOrUrl);
    if (path.isEmpty()) {
        emit projectError(tr("A project location must be selected."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".uaproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".uaproj");

    if (QFileInfo::exists(path)) {
        emit projectError(tr("A project file already exists at %1.").arg(path));
        return false;
    }

    ProjectData data;
    data.displayName = QFileInfo(path).completeBaseName();

    QString error;
    if (!ProjectSerializer::save(path, data, &error)) {
        emit projectError(error);
        return false;
    }

    if (m_opcUaManager) {
        m_opcUaManager->clearRuntimeState();
        m_opcUaManager->applyProject(data);
    }
    setActiveProject(path, data.displayName);
    addOrUpdateRecent(path, data.displayName, data.connection.discoveryUrl);
    return true;
}

/*!
 * \brief Opens an existing project, applies its state, and starts its connection.
 */
bool ProjectManager::openProject(const QString &pathOrUrl)
{
    const QString path = toLocalPath(pathOrUrl);
    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    if (!result.ok) {
        emit projectError(result.errorString);
        return false;
    }

    if (m_opcUaManager) {
        m_opcUaManager->clearRuntimeState();
        m_opcUaManager->applyProject(result.data);
    }
    setActiveProject(path, result.data.displayName);
    addOrUpdateRecent(path, result.data.displayName, result.data.connection.discoveryUrl);

    // Only after the full project state is restored does the connection begin.
    if (m_opcUaManager)
        m_opcUaManager->connectToProjectConnection();
    return true;
}

/*!
 * \brief Opens the recent-project entry at \a index.
 */
bool ProjectManager::openRecent(int index)
{
    if (index < 0 || index >= m_recent.size())
        return false;
    return openProject(m_recent.at(index).path);
}

/*!
 * \brief Saves the active project to its current file.
 */
bool ProjectManager::saveProject()
{
    if (m_activePath.isEmpty()) {
        emit projectError(tr("There is no active project to save."));
        return false;
    }
    return writeActiveProjectTo(m_activePath, m_activeName);
}

/*!
 * \brief Saves the active project to \a pathOrUrl, which becomes the active file.
 */
bool ProjectManager::saveProjectAs(const QString &pathOrUrl)
{
    if (!hasActiveProject()) {
        emit projectError(tr("There is no active project to save."));
        return false;
    }

    QString path = toLocalPath(pathOrUrl);
    if (path.isEmpty()) {
        emit projectError(tr("A project location must be selected."));
        return false;
    }
    if (!path.endsWith(QStringLiteral(".uaproj"), Qt::CaseInsensitive))
        path += QStringLiteral(".uaproj");

    return writeActiveProjectTo(path, QFileInfo(path).completeBaseName());
}

/*!
 * \brief Exports the active project's runtime state and writes it to \a path.
 *
 * On success the file becomes the active project under \a displayName and the
 * dirty flag is cleared; on failure projectError() is emitted.
 */
bool ProjectManager::writeActiveProjectTo(const QString &path, const QString &displayName)
{
    if (!m_opcUaManager) {
        emit projectError(tr("No project data is available to save."));
        return false;
    }

    ProjectData data = m_opcUaManager->exportProject();
    data.displayName = displayName;

    QString error;
    if (!ProjectSerializer::save(path, data, &error)) {
        emit projectError(error);
        return false;
    }

    setActiveProject(path, displayName);
    addOrUpdateRecent(path, displayName, data.connection.discoveryUrl);
    return true;
}

/*!
 * \brief Closes the active project and releases OPC UA runtime state.
 */
void ProjectManager::closeProject()
{
    if (m_opcUaManager)
        m_opcUaManager->clearRuntimeState();
    clearActiveProject();
}

/*!
 * \brief Creates a Default project from legacy pre-project state on first launch.
 */
void ProjectManager::maybeMigrateLegacyState()
{
    if (!m_recent.isEmpty() || !m_opcUaManager)
        return;
    if (!m_opcUaManager->hasLegacyState())
        return;

    const QString dir =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("projects"));
    const QString path = QDir(dir).filePath(QStringLiteral("Default.uaproj"));

    // A Default file from an earlier migration is simply relisted, not overwritten.
    if (!QFileInfo::exists(path)) {
        ProjectData data = m_opcUaManager->exportLegacyState();
        data.displayName = QStringLiteral("Default");

        QString error;
        if (!ProjectSerializer::save(path, data, &error)) {
            qWarning() << "ProjectManager: legacy migration failed:" << error;
            return;
        }
        addOrUpdateRecent(path, data.displayName, data.connection.discoveryUrl);
    } else {
        const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
        const QString endpoint = result.ok ? result.data.connection.discoveryUrl : QString();
        const QString name = result.ok && !result.data.displayName.isEmpty()
                                 ? result.data.displayName
                                 : QStringLiteral("Default");
        addOrUpdateRecent(path, name, endpoint);
    }
}

/*!
 * \brief Removes the recent-project entry at \a index from the list and the store.
 */
void ProjectManager::removeRecent(int index)
{
    if (index < 0 || index >= m_recent.size())
        return;

    m_recent.removeAt(index);
    saveRecentProjects();
    emit recentProjectsChanged();
}

/*!
 * \brief Returns the local file path for \a pathOrUrl, converting file: URLs.
 *
 * QML file dialogs deliver file: URLs while stored recent entries and command
 * lines use plain paths; this normalizes both to a local filesystem path.
 */
QString ProjectManager::toLocalPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    if (url.isLocalFile())
        return url.toLocalFile();
    return pathOrUrl;
}

/*!
 * \brief Loads the recent-projects list from the injected settings store.
 */
void ProjectManager::loadRecentProjects()
{
    m_recent.clear();

    if (m_settings) {
        const int count = m_settings->beginReadArray(QLatin1String(kRecentArrayKey));
        for (int i = 0; i < count; ++i) {
            m_settings->setArrayIndex(i);
            RecentEntry entry;
            entry.path = m_settings->value(QStringLiteral("path")).toString();
            entry.displayName = m_settings->value(QStringLiteral("displayName")).toString();
            entry.lastOpened = m_settings->value(QStringLiteral("lastOpened")).toString();
            entry.endpoint = m_settings->value(QStringLiteral("endpoint")).toString();
            if (!entry.path.isEmpty())
                m_recent.append(entry);
        }
        m_settings->endArray();
    }

    emit recentProjectsChanged();
}

/*!
 * \brief Writes the recent-projects list to the injected settings store.
 */
void ProjectManager::saveRecentProjects()
{
    if (!m_settings)
        return;

    m_settings->beginWriteArray(QLatin1String(kRecentArrayKey), m_recent.size());
    for (int i = 0; i < m_recent.size(); ++i) {
        m_settings->setArrayIndex(i);
        const RecentEntry &entry = m_recent.at(i);
        m_settings->setValue(QStringLiteral("path"), entry.path);
        m_settings->setValue(QStringLiteral("displayName"), entry.displayName);
        m_settings->setValue(QStringLiteral("lastOpened"), entry.lastOpened);
        m_settings->setValue(QStringLiteral("endpoint"), entry.endpoint);
    }
    m_settings->endArray();
    m_settings->sync();
}

/*!
 * \brief Inserts or refreshes the recent entry for \a path at the front of the list.
 *
 * An existing entry for the same file (compared case-insensitively) is removed
 * first, so the project moves to the front with a fresh timestamp instead of
 * being duplicated. The list is capped at kMaxRecentProjects entries.
 */
void ProjectManager::addOrUpdateRecent(const QString &path,
                                       const QString &displayName,
                                       const QString &endpoint)
{
    const QString absolutePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString key = canonicalKey(absolutePath);

    for (int i = m_recent.size() - 1; i >= 0; --i) {
        if (canonicalKey(m_recent.at(i).path) == key)
            m_recent.removeAt(i);
    }

    RecentEntry entry;
    entry.path = absolutePath;
    entry.displayName = displayName;
    entry.lastOpened = QDateTime::currentDateTime().toString(Qt::ISODate);
    entry.endpoint = endpoint;
    m_recent.prepend(entry);

    while (m_recent.size() > kMaxRecentProjects)
        m_recent.removeLast();

    saveRecentProjects();
    emit recentProjectsChanged();
}

/*!
 * \brief Makes \a path with \a displayName the active project and clears the dirty flag.
 */
void ProjectManager::setActiveProject(const QString &path, const QString &displayName)
{
    m_activePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    m_activeName = displayName;
    setDirty(false);
    emit activeProjectChanged();
}

/*!
 * \brief Clears the active project and the dirty flag.
 */
void ProjectManager::clearActiveProject()
{
    if (m_activePath.isEmpty() && m_activeName.isEmpty()) {
        setDirty(false);
        return;
    }
    m_activePath.clear();
    m_activeName.clear();
    setDirty(false);
    emit activeProjectChanged();
}

void ProjectManager::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}
