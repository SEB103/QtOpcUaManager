#include "core/apppaths.h"

#include "productinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
/*! \internal Marker file that selects portable mode when present next to the exe. */
constexpr auto kPortableMarker = "portable.ini";
/*! \internal Relative path of the seeded/writable SQLite node database. */
constexpr auto kDatabaseRelPath = "db/opcua_nodes.db";
/*! \internal Bundled seed directory holding the initial node database. */
constexpr auto kSeedDatabaseDir = "db";
/*! \internal Settings sub-folder created by AppCore::createSettings(). */
constexpr auto kSettingsSubDir = "ini";
} // namespace

/*!
 * \brief Returns the process-wide AppPaths instance.
 */
AppPaths& AppPaths::instance()
{
    static AppPaths paths;
    return paths;
}

/*!
 * \brief Resolves the run mode and all base directories.
 *
 * Portable mode is selected when a \c portable.ini marker sits next to the
 * executable. In portable mode every writable directory lives under the
 * application directory; in installed mode settings go to AppConfigLocation and
 * volatile/machine-local state (logs, PKI, the writable database) to
 * AppLocalDataLocation, while projects default to the user's Documents folder.
 *
 * The organization and application names must already be set on
 * QCoreApplication, because the installed-mode locations derive from them.
 * Calling this more than once is a no-op.
 */
void AppPaths::initialize()
{
    if (m_initialized)
        return;

    m_seedDir = QCoreApplication::applicationDirPath();
    m_portable = QFileInfo::exists(
        QDir(m_seedDir).filePath(QString::fromLatin1(kPortableMarker)));

    if (m_portable) {
        const QDir root(m_seedDir);
        m_configDir = root.filePath(QStringLiteral("config"));
        m_dataDir = root.filePath(QStringLiteral("data"));
        m_localDataDir = m_dataDir;
        m_logDir = root.filePath(QStringLiteral("logs"));
        m_pkiDir = QDir(m_dataDir).filePath(QStringLiteral("pki"));
        m_databaseFilePath =
            QDir(m_dataDir).filePath(QString::fromLatin1(kDatabaseRelPath));
        m_defaultProjectsDir = QDir(m_dataDir).filePath(QStringLiteral("projects"));
    } else {
        m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        m_localDataDir = m_dataDir;
        // Keep the historical "log" and "pki" folder names so existing installed
        // data under AppLocalDataLocation is reused unchanged.
        m_logDir = QDir(m_localDataDir).filePath(QStringLiteral("log"));
        m_pkiDir = QDir(m_localDataDir).filePath(QStringLiteral("pki"));
        m_databaseFilePath =
            QDir(m_dataDir).filePath(QString::fromLatin1(kDatabaseRelPath));
        const QString documents =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        // Use the stable product identifier (not the running executable name) so
        // the projects folder is consistent across the app and its tools/tests.
        m_defaultProjectsDir =
            QDir(documents).filePath(QStringLiteral(PRODUCT_IDENTIFIER));
    }

    m_seedDir = QDir::cleanPath(m_seedDir);
    m_configDir = QDir::cleanPath(m_configDir);
    m_dataDir = QDir::cleanPath(m_dataDir);
    m_localDataDir = QDir::cleanPath(m_localDataDir);
    m_logDir = QDir::cleanPath(m_logDir);
    m_pkiDir = QDir::cleanPath(m_pkiDir);
    m_databaseFilePath = QDir::cleanPath(m_databaseFilePath);
    m_defaultProjectsDir = QDir::cleanPath(m_defaultProjectsDir);

    m_initialized = true;
}

/*!
 * \internal
 * \brief Resolves paths on first access when initialize() has not been called.
 *
 * Production code calls initialize() explicitly from main(); this guard keeps
 * the getters correct for callers (such as tests) that construct dependent
 * objects without that call.
 */
void AppPaths::ensureInitialized() const
{
    if (!m_initialized)
        const_cast<AppPaths *>(this)->initialize();
}

/*!
 * \brief Returns whether the application runs in portable mode.
 */
bool AppPaths::isPortable() const
{
    ensureInitialized();
    return m_portable;
}

QString AppPaths::seedDir() const
{
    ensureInitialized();
    return m_seedDir;
}

QString AppPaths::configDir() const
{
    ensureInitialized();
    return m_configDir;
}

QString AppPaths::dataDir() const
{
    ensureInitialized();
    return m_dataDir;
}

QString AppPaths::localDataDir() const
{
    ensureInitialized();
    return m_localDataDir;
}

QString AppPaths::logDir() const
{
    ensureInitialized();
    return m_logDir;
}

QString AppPaths::pkiDir() const
{
    ensureInitialized();
    return m_pkiDir;
}

QString AppPaths::databaseFilePath() const
{
    ensureInitialized();
    return m_databaseFilePath;
}

QString AppPaths::defaultProjectsDir() const
{
    ensureInitialized();
    return m_defaultProjectsDir;
}

/*!
 * \brief Copies bundled seed material into writable storage on first run.
 *
 * Two one-time copies are performed when their target is missing:
 * - the seed SQLite node database bundled at \c <seedDir>/db is copied to the
 *   writable databaseFilePath(), so the application never writes into the
 *   read-only install directory;
 * - a legacy settings file written next to the executable by an earlier version
 *   (\c <seedDir>/ini/<app>.ini) is migrated to the resolved configDir(), so a
 *   move to the per-user location does not lose existing settings.
 *
 * Existing writable files are never overwritten, so user data survives restarts
 * and updates. The PKI skeleton is seeded separately by OpcUaService, which
 * targets pkiDir().
 */
void AppPaths::ensureSeededOnFirstRun()
{
    ensureInitialized();

    // Seed the writable node database from the bundled copy.
    if (!QFileInfo::exists(m_databaseFilePath)) {
        const QString seedDatabase =
            QDir(m_seedDir).filePath(QString::fromLatin1(kSeedDatabaseDir)
                                     + QStringLiteral("/opcua_nodes.db"));
        if (QFileInfo::exists(seedDatabase)) {
            const QString targetDir = QFileInfo(m_databaseFilePath).absolutePath();
            if (QDir().mkpath(targetDir))
                QFile::copy(seedDatabase, m_databaseFilePath);
        }
    }

    // Migrate a legacy exe-relative settings file to the resolved config dir.
    const QString appName = QCoreApplication::applicationName();
    const QString iniName = appName + QStringLiteral(".ini");
    const QString legacyIni = QDir(m_seedDir)
                                  .filePath(QString::fromLatin1(kSettingsSubDir)
                                            + QStringLiteral("/") + iniName);
    const QString targetIni = QDir(m_configDir)
                                  .filePath(QString::fromLatin1(kSettingsSubDir)
                                            + QStringLiteral("/") + iniName);
    if (QDir::cleanPath(legacyIni) != QDir::cleanPath(targetIni)
        && !QFileInfo::exists(targetIni)
        && QFileInfo::exists(legacyIni)) {
        const QString targetDir = QFileInfo(targetIni).absolutePath();
        if (QDir().mkpath(targetDir))
            QFile::copy(legacyIni, targetIni);
    }
}
