#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "nodedatabase.h"

/*!
 * \brief Constructs the store without opening a database yet.
 * \param connectionName The unique QSqlDatabase connection name to use.
 */
NodeDatabase::NodeDatabase(const QString &connectionName)
    : m_connectionName(connectionName)
{}

/*!
 * \brief Closes the database and removes the named connection.
 *
 * The QSqlDatabase handle is released before QSqlDatabase::removeDatabase() so
 * that Qt does not warn about an in-use connection.
 */
NodeDatabase::~NodeDatabase()
{
    if (m_database.isOpen())
        m_database.close();
    m_database = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
}

/*!
 * \brief Opens the SQLite database at \a databaseFilePath and ensures the schema.
 *
 * The parent directory is created when missing so that a fresh installation can
 * create the database next to the executable. Returns \c true when the database
 * is open and the schema is present.
 */
bool NodeDatabase::open(const QString &databaseFilePath)
{
    const QFileInfo fileInfo(databaseFilePath);
    const QDir parentDir = fileInfo.absoluteDir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        qWarning() << "NodeDatabase: failed to create directory" << parentDir.absolutePath();
        return false;
    }

    if (QSqlDatabase::contains(m_connectionName))
        m_database = QSqlDatabase::database(m_connectionName);
    else
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);

    m_database.setDatabaseName(databaseFilePath);
    if (!m_database.open()) {
        qWarning() << "NodeDatabase: failed to open" << databaseFilePath
                   << m_database.lastError().text();
        return false;
    }

    return ensureSchema();
}

/*!
 * \brief Returns whether the underlying database connection is open.
 */
bool NodeDatabase::isOpen() const
{
    return m_database.isOpen();
}

/*!
 * \brief Creates the monitored_nodes table when it does not exist.
 *
 * The (server, node_id) pair is unique so that the same node cannot be stored
 * twice for one server.
 */
bool NodeDatabase::ensureSchema()
{
    QSqlQuery query(m_database);
    const bool ok = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS monitored_nodes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "server TEXT NOT NULL, "
        "node_id TEXT NOT NULL, "
        "node_path TEXT, "
        "display_name TEXT, "
        "data_type TEXT, "
        "added_at TEXT, "
        "UNIQUE(server, node_id))"));
    if (!ok)
        qWarning() << "NodeDatabase: failed to create schema" << query.lastError().text();
    return ok;
}

/*!
 * \brief Returns all persisted records ordered by insertion.
 */
QList<MonitoredNodeRecord> NodeDatabase::loadAll() const
{
    QList<MonitoredNodeRecord> records;
    if (!m_database.isOpen())
        return records;

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT server, node_id, node_path, display_name, data_type "
            "FROM monitored_nodes ORDER BY id"))) {
        qWarning() << "NodeDatabase: loadAll failed" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        MonitoredNodeRecord record;
        record.server = query.value(0).toString();
        record.nodeId = query.value(1).toString();
        record.nodePath = query.value(2).toString();
        record.displayName = query.value(3).toString();
        record.dataType = query.value(4).toString();
        records.push_back(record);
    }
    return records;
}

/*!
 * \brief Inserts \a record or refreshes its metadata when it already exists.
 *
 * The ON CONFLICT clause keeps the original row while updating display metadata,
 * so re-adding a node does not create a duplicate.
 */
bool NodeDatabase::insert(const MonitoredNodeRecord &record)
{
    if (!m_database.isOpen())
        return false;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO monitored_nodes "
        "(server, node_id, node_path, display_name, data_type, added_at) "
        "VALUES (:server, :node_id, :node_path, :display_name, :data_type, :added_at) "
        "ON CONFLICT(server, node_id) DO UPDATE SET "
        "node_path = excluded.node_path, "
        "display_name = excluded.display_name, "
        "data_type = excluded.data_type"));
    query.bindValue(QStringLiteral(":server"), record.server);
    query.bindValue(QStringLiteral(":node_id"), record.nodeId);
    query.bindValue(QStringLiteral(":node_path"), record.nodePath);
    query.bindValue(QStringLiteral(":display_name"), record.displayName);
    query.bindValue(QStringLiteral(":data_type"), record.dataType);
    query.bindValue(QStringLiteral(":added_at"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "NodeDatabase: insert failed" << query.lastError().text();
        return false;
    }
    return true;
}

/*!
 * \brief Removes the record identified by \a server and \a nodeId.
 */
bool NodeDatabase::remove(const QString &server, const QString &nodeId)
{
    if (!m_database.isOpen())
        return false;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "DELETE FROM monitored_nodes WHERE server = :server AND node_id = :node_id"));
    query.bindValue(QStringLiteral(":server"), server);
    query.bindValue(QStringLiteral(":node_id"), nodeId);

    if (!query.exec()) {
        qWarning() << "NodeDatabase: remove failed" << query.lastError().text();
        return false;
    }
    return true;
}
