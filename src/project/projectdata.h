#ifndef PROJECTDATA_H
#define PROJECTDATA_H

#include <QList>
#include <QString>
#include <QVariantMap>

#include "persistence/nodedatabase.h"

/**
 * Current .uaproj JSON schema version written by this build.
 *
 * Version 2 added the per-node \c interval field and the \c settings.dataView
 * object. Older version 1 files still load; their missing fields fall back to
 * the defaults below.
 */
inline constexpr int kProjectFormatVersion = 2;

/** OPC UA connection configuration persisted inside a project file. */
struct ProjectConnectionConfig
{
    /** Discovery URL used to find the OPC UA server. */
    QString discoveryUrl;
    /** Selected Qt OPC UA backend plugin name. */
    QString backend;
    /** Server identity display string chosen during discovery. */
    QString server;
    /** Endpoint display string chosen for the session. */
    QString endpoint;
    /** Authentication token mode (QOpcUaUserTokenPolicy::TokenType) as an integer. */
    int authMode {0};
    /** User name for username authentication; the password is never persisted. */
    QString userName;
    /** Whether advertised endpoint URLs are rewritten to the discovery host and port. */
    bool endpointUrlRewriteEnabled {false};

    /** Returns whether a discovery URL is present, i.e. the project can connect. */
    bool isConfigured() const { return !discoveryUrl.isEmpty(); }

    /** Returns whether every connection field equals the corresponding field of \a other. */
    bool operator==(const ProjectConnectionConfig &other) const
    {
        return discoveryUrl == other.discoveryUrl && backend == other.backend
               && server == other.server && endpoint == other.endpoint
               && authMode == other.authMode && userName == other.userName
               && endpointUrlRewriteEnabled == other.endpointUrlRewriteEnabled;
    }

    /** Returns whether any connection field differs from \a other. */
    bool operator!=(const ProjectConnectionConfig &other) const { return !(*this == other); }
};

/** Pinned focus node persisted inside a project file. */
struct ProjectFocusNode
{
    /** Node id of the pinned focus node; empty when none is pinned. */
    QString nodeId;
    /** Absolute display path of the pinned focus node. */
    QString path;
    /** Display name of the pinned focus node. */
    QString displayName;

    /** Returns whether a focus node is pinned. */
    bool isSet() const { return !nodeId.isEmpty(); }
};

/** Miscellaneous per-project view settings persisted inside a project file. */
struct ProjectSettings
{
    /** Structured-value output format (OpcUaManager::ValueFormat) as an integer. */
    int valueFormat {0};

    /**
     * Opaque Data Access View layout state (column widths, visibility, sorting).
     *
     * The map is written and read by the QML table, which owns the layout
     * vocabulary; the project layer only persists it verbatim so the table can
     * evolve without a schema change here. An empty map selects the defaults.
     */
    QVariantMap dataView;
};

/**
 * In-memory representation of one OPC UA workspace project (.uaproj).
 *
 * A project file is the single source of truth for project-specific state:
 * the OPC UA connection configuration, the pinned focus node, the set of
 * monitored nodes, and per-project view settings.
 */
struct ProjectData
{
    /** Schema version of the file this data was read from or is written to. */
    int formatVersion {kProjectFormatVersion};
    /** Human-readable project name shown in the launcher and window title. */
    QString displayName;
    /** OPC UA connection configuration for the project. */
    ProjectConnectionConfig connection;
    /** Pinned focus node for the project. */
    ProjectFocusNode focusNode;
    /** Monitored nodes shown in the Data Access View for the project. */
    QList<MonitoredNodeRecord> monitoredNodes;
    /** Per-project view settings. */
    ProjectSettings settings;
};

#endif // PROJECTDATA_H
