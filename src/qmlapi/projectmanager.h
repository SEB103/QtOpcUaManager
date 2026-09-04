#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

class OpcUaManager;

/**
 * GUI-thread facade that owns the active project and the recent-projects list.
 *
 * Exposed to QML as the \c cppProjectManager context property. The active
 * project's .uaproj file is the single source of truth for project-specific
 * state; this class coordinates loading that state into and saving it from
 * OpcUaManager. The recent-projects list is application-global metadata and is
 * stored in the injected QSettings store, never inside a project file.
 */
class ProjectManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProjectManager)

    /** Whether a project is currently active. */
    Q_PROPERTY(bool hasActiveProject READ hasActiveProject NOTIFY activeProjectChanged)

    /** Display name of the active project, or an empty string when none is active. */
    Q_PROPERTY(QString activeProjectName READ activeProjectName NOTIFY activeProjectChanged)

    /** Absolute file path of the active project's .uaproj, or an empty string. */
    Q_PROPERTY(QString activeProjectPath READ activeProjectPath NOTIFY activeProjectChanged)

    /** Whether the active project has unsaved changes. */
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

    /** Recent-project entries, newest first, as QVariantMap rows for QML. */
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)

public:
    /** Creates a project manager with no active project and an empty recent list. */
    explicit ProjectManager(QObject *parent = nullptr);

    /** Destroys the project manager. */
    ~ProjectManager() override;

    /** Returns whether a project is currently active. */
    bool hasActiveProject() const;

    /** Returns the display name of the active project, or an empty string. */
    QString activeProjectName() const;

    /** Returns the absolute .uaproj path of the active project, or an empty string. */
    QString activeProjectPath() const;

    /** Returns whether the active project has unsaved changes. */
    bool dirty() const;

    /** Returns the recent-project rows exposed to QML, newest first. */
    QVariantList recentProjects() const;

    /**
     * Injects the INI settings store used to persist the recent-projects list.
     * Ownership stays with the caller; passing null disables persistence.
     */
    void setSettings(QSettings *settings);

    /** Sets the OPC UA facade project state is applied to and exported from. */
    void setOpcUaManager(OpcUaManager *manager);

    /**
     * Creates a new empty project named \a name inside \a folderPathOrUrl, opens
     * it as the active project, and enters the workspace with no connection.
     * Returns true on success; on failure emits projectError() and returns false.
     */
    Q_INVOKABLE bool createProject(const QString &name, const QString &folderPathOrUrl);

    /**
     * Creates a new empty project at the .uaproj path \a pathOrUrl (as returned by
     * a save file dialog), deriving the display name from the file name, and opens
     * it. Returns true on success; on failure emits projectError() and returns false.
     */
    Q_INVOKABLE bool createProjectAtPath(const QString &pathOrUrl);

    /**
     * Opens the project at \a pathOrUrl: validates and loads the file, applies its
     * state to the OPC UA facade, makes it the active project, and starts the
     * connection when one is configured. Returns true on success; on failure emits
     * projectError() and returns false.
     */
    Q_INVOKABLE bool openProject(const QString &pathOrUrl);

    /** Opens the recent-project entry at \a index. Returns false when out of range. */
    Q_INVOKABLE bool openRecent(int index);

    /**
     * Saves the active project to its current file, clearing the dirty flag.
     * Returns true on success; on failure emits projectError() and returns false.
     */
    Q_INVOKABLE bool saveProject();

    /**
     * Saves the active project to \a pathOrUrl, which becomes the active project
     * file, and derives the display name from the file name. Returns true on
     * success; on failure emits projectError() and returns false.
     */
    Q_INVOKABLE bool saveProjectAs(const QString &pathOrUrl);

    /** Closes the active project, releases OPC UA state, and returns to the launcher. */
    Q_INVOKABLE void closeProject();

    /** Removes the recent-project entry at \a index from the list and the store. */
    Q_INVOKABLE void removeRecent(int index);

    /** Returns the local file path for \a pathOrUrl, converting file: URLs. */
    Q_INVOKABLE static QString toLocalPath(const QString &pathOrUrl);

signals:
    /** Emitted when the active project, its name, or its path changes. */
    void activeProjectChanged();

    /** Emitted when the unsaved-changes state changes. */
    void dirtyChanged();

    /** Emitted when the recent-projects list changes. */
    void recentProjectsChanged();

    /** Emitted with a user-facing \a message when a project operation fails. */
    void projectError(const QString &message);

protected:
    /**
     * Creates a Default project from legacy pre-project state on first launch.
     * Runs only when the recent list is empty and legacy state exists, so an
     * existing installation's single session is preserved as a real project.
     */
    void maybeMigrateLegacyState();
    /** Loads the recent-projects list from the injected settings store. */
    void loadRecentProjects();

    /** Writes the recent-projects list to the injected settings store. */
    void saveRecentProjects();

    /**
     * Inserts or refreshes the recent entry for \a path, moving it to the front
     * with the current timestamp, \a displayName, and \a endpoint. Caps the list.
     */
    void addOrUpdateRecent(const QString &path, const QString &displayName, const QString &endpoint);

    /** Exports the active project's runtime state and writes it to \a path as \a displayName. */
    bool writeActiveProjectTo(const QString &path, const QString &displayName);

    /** Makes \a path with \a displayName the active project and clears the dirty flag. */
    void setActiveProject(const QString &path, const QString &displayName);

    /** Clears the active project and the dirty flag. */
    void clearActiveProject();

    /** Sets the unsaved-changes flag to \a dirty and emits on change. */
    void setDirty(bool dirty);

    /** One recent-project entry as stored in settings. */
    struct RecentEntry
    {
        /** Absolute .uaproj file path. */
        QString path;
        /** Display name last seen for the project. */
        QString displayName;
        /** Last-opened timestamp in ISO 8601 format. */
        QString lastOpened;
        /** Last known OPC UA endpoint, shown as a subtitle. */
        QString endpoint;
    };

    /** Non-owning INI settings store for the recent-projects list. */
    QSettings *m_settings {nullptr};

    /** Non-owning OPC UA facade the project state is applied to and exported from. */
    OpcUaManager *m_opcUaManager {nullptr};

    /** Recent-project entries in display order, newest first. */
    QList<RecentEntry> m_recent;

    /** Absolute .uaproj path of the active project; empty when none is active. */
    QString m_activePath;

    /** Display name of the active project. */
    QString m_activeName;

    /** Whether the active project has unsaved changes. */
    bool m_dirty {false};
};

#endif // PROJECTMANAGER_H
