#ifndef SERVERSTUDIO_H
#define SERVERSTUDIO_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "serverproject/serverprojectdata.h"
#include "serverruntimecontroller.h"

class OpcUaManager;
class ServerNodeModel;

/**
 * QML-facing facade for the Server Studio mode, exposed as \c cppServerStudio.
 *
 * Owns the in-memory server project (create/open/save/close), the design-time
 * address-space tree model, the node create/edit/remove operations, and the
 * headless runtime lifecycle. "Open in Client" connects the existing OPC UA
 * client to the running local endpoint through the ordinary client path.
 */
class ServerStudio : public QObject
{
    Q_OBJECT

    // Runtime lifecycle.
    Q_PROPERTY(ServerRuntimeController::State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool crashed READ crashed NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString endpointUrl READ endpointUrl NOTIFY endpointUrlChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)

    // Live diagnostics reported by a running runtime.
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(int secureChannelCount READ secureChannelCount NOTIFY diagnosticsChanged)
    Q_PROPERTY(QString diagnosticsText READ diagnosticsText NOTIFY diagnosticsChanged)

    // Project state.
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int port READ port NOTIFY projectChanged)

    // Address-space editing.
    Q_PROPERTY(QObject *nodeModel READ nodeModel CONSTANT)
    Q_PROPERTY(QStringList dataTypeNames READ dataTypeNames CONSTANT)
    Q_PROPERTY(QStringList simulationKindNames READ simulationKindNames CONSTANT)
    Q_PROPERTY(QStringList enumTypeNames READ enumTypeNames NOTIFY enumsChanged)
    Q_PROPERTY(QVariantList enumTypes READ enumTypes NOTIFY enumsChanged)
    Q_PROPERTY(QString selectedNodeId READ selectedNodeId WRITE setSelectedNodeId
                   NOTIFY selectedNodeChanged)
    Q_PROPERTY(QVariantMap selectedNode READ selectedNode NOTIFY selectedNodeChanged)

    // Security configuration.
    Q_PROPERTY(QVariantMap security READ security NOTIFY securityChanged)

public:
    /** Creates the facade, its runtime controller and its tree model. */
    explicit ServerStudio(QObject *parent = nullptr);
    ~ServerStudio() override;

    /** Injects the client \a manager used by openInClient(); non-owning. */
    void setOpcUaManager(OpcUaManager *manager);

    // Runtime lifecycle accessors.
    ServerRuntimeController::State state() const;
    bool running() const;
    bool busy() const;
    bool crashed() const;
    QString stateText() const;
    QString endpointUrl() const;

    /**
     * Whether the running server's started configuration diverges from the
     * current in-memory project, i.e. edits are pending a restart to apply.
     * Always false while the server is not running.
     */
    bool restartRequired() const;

    int sessionCount() const;
    int secureChannelCount() const;
    QString diagnosticsText() const;

    // Project accessors.
    bool hasProject() const { return m_hasProject; }
    QString projectName() const { return m_project.displayName; }
    QString projectPath() const { return m_projectPath; }
    bool dirty() const { return m_dirty; }
    int port() const { return m_project.server.endpoint.port; }

    // Editing accessors.
    QObject *nodeModel() const;
    QStringList dataTypeNames() const;
    QStringList simulationKindNames() const;
    QStringList enumTypeNames() const;
    QVariantList enumTypes() const;
    QString selectedNodeId() const { return m_selectedNodeId; }
    void setSelectedNodeId(const QString &nodeId);
    QVariantMap selectedNode() const;

    // Project lifecycle.
    /** Creates a new empty project named \a displayName with one namespace. */
    Q_INVOKABLE void newProject(const QString &displayName);

    /** Opens the .uaserver project at \a path (a local path or file URL). */
    Q_INVOKABLE bool openProject(const QString &path);

    /** Saves the project to its current path; false when it has no path yet. */
    Q_INVOKABLE bool saveProject();

    /** Saves the project to \a path (a local path or file URL) and adopts it. */
    Q_INVOKABLE bool saveProjectAs(const QString &path);

    /** Closes the project, stopping the runtime if it is running. */
    Q_INVOKABLE void closeProject();

    /** Exports the current address space to \a path as NodeSet2 XML. */
    Q_INVOKABLE bool exportNodeSet(const QString &path);

    /** Imports a NodeSet2 XML file at \a path into the project's address space. */
    Q_INVOKABLE bool importNodeSet(const QString &path);

    /**
     * Clones the currently browsed address space of the connected client into a
     * new server project (structure and data types; values default). The user
     * browses the areas of interest in the client first.
     */
    Q_INVOKABLE bool cloneFromClient();

    // Address-space editing.
    /** Adds a folder under \a parentNodeId (empty = Objects root); returns its id. */
    Q_INVOKABLE QString addFolder(const QString &parentNodeId, const QString &browseName);

    /** Adds an object under \a parentNodeId; returns its id. */
    Q_INVOKABLE QString addObject(const QString &parentNodeId, const QString &browseName);

    /**
     * Adds a variable under \a parentNodeId with the given \a dataType and
     * \a valueRank (-1 scalar, 1 array), writable per \a writable; returns its id.
     */
    Q_INVOKABLE QString addVariable(const QString &parentNodeId, const QString &browseName,
                                    const QString &dataType, int valueRank, bool writable);

    /**
     * Applies \a fields to the node \a nodeId. Recognized keys: displayName,
     * description, dataType, valueRank, writable, initialValue (a string parsed
     * according to the data type and value rank).
     */
    Q_INVOKABLE void updateNode(const QString &nodeId, const QVariantMap &fields);

    /** Removes \a nodeId and all of its descendants. */
    Q_INVOKABLE void removeNode(const QString &nodeId);

    // Enumeration types.
    /** Adds a new empty enumeration type named \a name; returns its node id. */
    Q_INVOKABLE QString addEnumType(const QString &name);

    /** Adds an entry (\a value, \a entryName) to the enum named \a enumName. */
    Q_INVOKABLE void addEnumEntry(const QString &enumName, int value, const QString &entryName);

    /** Removes the enum type \a enumName and clears variables that referenced it. */
    Q_INVOKABLE void removeEnumType(const QString &enumName);

    // Security editing.
    /** Returns the security configuration as a map for the security panel. */
    QVariantMap security() const;

    /** Sets the endpoint/authentication flags. */
    Q_INVOKABLE void setSecurityFlags(bool allowAnonymous, bool allowNone, bool enableSecurity);

    /**
     * Sets whether the server accepts any client certificate. When false, the
     * runtime enforces a real trust list from the server PKI.
     */
    Q_INVOKABLE void setAcceptAllClientCerts(bool acceptAll);

    /** Adds or updates a username/password test login. */
    Q_INVOKABLE void addUser(const QString &username, const QString &password);

    /** Removes the test login \a username. */
    Q_INVOKABLE void removeUser(const QString &username);

    /** Returns the file names of certificates the server has rejected. */
    Q_INVOKABLE QStringList rejectedCertificates() const;

    /**
     * Moves the rejected certificate \a fileName into the trusted store, so the
     * next connection from that client is accepted. Returns whether it moved.
     */
    Q_INVOKABLE bool trustRejectedCertificate(const QString &fileName);

    // Runtime control.
    /** Starts the runtime serving the current project (auto-saving a snapshot). */
    Q_INVOKABLE void startServer();

    /** Requests a graceful runtime shutdown. */
    Q_INVOKABLE void stop();

    /** Restarts the runtime on the same project. */
    Q_INVOKABLE void restart();

    /** Kills the runtime process immediately (for failure testing). */
    Q_INVOKABLE void kill();

    /** Connects the existing OPC UA client to the running local endpoint. */
    Q_INVOKABLE void openInClient();

signals:
    void stateChanged();
    void endpointUrlChanged();
    void restartRequiredChanged();
    void diagnosticsChanged();
    void projectChanged();
    void dirtyChanged();
    void selectedNodeChanged();
    void securityChanged();
    void enumsChanged();
    void notification(int level, const QString &message);
    void openInClientRequested();

private:
    /** Marks the project modified and notifies observers. */
    void setDirty(bool dirty);

    /** Rebuilds the tree model from the current node list. */
    void refreshModel();

    /** Returns a unique node id in the first custom namespace derived from \a base. */
    QString makeUniqueNodeId(const QString &base) const;

    /** Returns a pointer to the node with \a nodeId, or nullptr. */
    const ServerProject::Node *findNode(const QString &nodeId) const;

    /** Converts \a text to a value of \a dataType with \a valueRank. */
    static QVariant parseInitialValue(const QString &dataType, int valueRank, const QString &text);

    /** Converts \a value to its editable string form. */
    static QString initialValueToText(const QVariant &value);

    /** Writes the current project to a snapshot file for the runtime; empty on error. */
    QString writeRuntimeSnapshot();

    /** Returns the independent server PKI directory used by the runtime. */
    QString serverPkiDir() const;

    /** Supervises the headless runtime process; owned by this facade. */
    ServerRuntimeController m_controller;

    /** Design-time address-space tree model; owned by this facade. */
    ServerNodeModel *m_nodeModel = nullptr;

    /** Client facade used to connect on openInClient(); not owned. */
    OpcUaManager *m_opcUaManager = nullptr;

    /** The in-memory project being edited. */
    ServerProject::ProjectData m_project;

    /** Whether a project is currently open. */
    bool m_hasProject = false;

    /** On-disk path of the project, empty when never saved. */
    QString m_projectPath;

    /** Whether the project has unsaved changes. */
    bool m_dirty = false;

    /**
     * Whether the project was edited since the running server last (re)started.
     * Combined with the running state to drive restartRequired().
     */
    bool m_configChangedSinceStart = false;

    /** Node id currently selected in the editor. */
    QString m_selectedNodeId;
};

#endif // SERVERSTUDIO_H
