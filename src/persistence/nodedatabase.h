#ifndef NODEDATABASE_H
#define NODEDATABASE_H

#include <QList>
#include <QSqlDatabase>
#include <QString>

/**
 * Persistent record describing one node the user added to the Data Access View.
 *
 * The record stores node identity and display metadata only. Live values,
 * timestamps, and status codes are never persisted; they are obtained from the
 * server at runtime.
 */
struct MonitoredNodeRecord
{
    /** Server identity the node belongs to (endpoint URL or server display row). */
    QString server;

    /** OPC UA node id string. */
    QString nodeId;

    /** Human-readable browse path shown in the table. */
    QString nodePath;

    /** Localized display name shown in the table. */
    QString displayName;

    /** Cached data type text shown before the first live value arrives. */
    QString dataType;
};

/**
 * SQLite-backed store for the set of monitored nodes shown in the Data Access View.
 *
 * The database file lives in a \c db directory next to the executable so that it
 * ships with the application. The store runs entirely in the GUI thread; SQLite
 * access is short and synchronous, so no worker thread is required.
 */
class NodeDatabase
{
public:
    /** Creates the store using \a connectionName for the underlying QSqlDatabase. */
    explicit NodeDatabase(const QString &connectionName = QStringLiteral("opcua_nodes"));

    /** Closes and removes the underlying database connection. */
    ~NodeDatabase();

    /**
     * Opens (creating if needed) the database at \a databaseFilePath and ensures
     * the schema exists. Returns \c true on success.
     */
    bool open(const QString &databaseFilePath);

    /** Returns whether the database connection is open. */
    bool isOpen() const;

    /** Returns all persisted records in insertion order. */
    QList<MonitoredNodeRecord> loadAll() const;

    /**
     * Inserts \a record, or updates its metadata when the same (server, nodeId)
     * pair already exists. Returns \c true on success.
     */
    bool insert(const MonitoredNodeRecord &record);

    /** Removes the record identified by \a server and \a nodeId. Returns \c true on success. */
    bool remove(const QString &server, const QString &nodeId);

private:
    /** Creates the \c monitored_nodes table when it does not exist yet. */
    bool ensureSchema();

    /** Connection name used for the owned QSqlDatabase handle. */
    QString m_connectionName;

    /** Owned SQLite database handle. */
    QSqlDatabase m_database;
};

#endif // NODEDATABASE_H
