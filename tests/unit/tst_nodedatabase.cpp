#include <QTemporaryDir>
#include <QtTest>

#include "persistence/nodedatabase.h"

/*!
 * \internal
 * \brief Builds a monitored-node record for tests.
 */
static MonitoredNodeRecord makeRecord(const QString &nodeId, const QString &displayName)
{
    MonitoredNodeRecord record;
    record.server = QStringLiteral("opc.tcp://127.0.0.1:4840");
    record.nodeId = nodeId;
    record.nodePath = QStringLiteral("Objects/") + displayName;
    record.displayName = displayName;
    record.dataType = QStringLiteral("String");
    return record;
}

/*! Verifies NodeDatabase insert, load, upsert, and remove behavior. */
class NodeDatabaseTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a fresh database loads no records. */
    void freshDatabaseIsEmpty();

    /*! Verifies that inserted records are returned by loadAll() in order. */
    void insertPersistsRecords();

    /*! Verifies that inserting a duplicate node id updates metadata instead of duplicating. */
    void insertDuplicateUpdatesMetadata();

    /*! Verifies that remove() deletes the matching record. */
    void removeDeletesRecord();
};

/*!
 * \brief Verifies that a fresh database loads no records.
 */
void NodeDatabaseTest::freshDatabaseIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    NodeDatabase database(QStringLiteral("test_empty"));
    QVERIFY(database.open(dir.filePath(QStringLiteral("nodes.db"))));
    QVERIFY(database.isOpen());
    QVERIFY(database.loadAll().isEmpty());
}

/*!
 * \brief Verifies that inserted records are returned by loadAll() in order.
 */
void NodeDatabaseTest::insertPersistsRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    NodeDatabase database(QStringLiteral("test_insert"));
    QVERIFY(database.open(dir.filePath(QStringLiteral("nodes.db"))));

    QVERIFY(database.insert(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("A"))));
    QVERIFY(database.insert(makeRecord(QStringLiteral("ns=1;s=B"), QStringLiteral("B"))));

    const QList<MonitoredNodeRecord> records = database.loadAll();
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(0).nodeId, QStringLiteral("ns=1;s=A"));
    QCOMPARE(records.at(1).displayName, QStringLiteral("B"));
}

/*!
 * \brief Verifies that inserting a duplicate node id updates metadata.
 */
void NodeDatabaseTest::insertDuplicateUpdatesMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    NodeDatabase database(QStringLiteral("test_duplicate"));
    QVERIFY(database.open(dir.filePath(QStringLiteral("nodes.db"))));

    QVERIFY(database.insert(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("Old"))));
    QVERIFY(database.insert(makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("New"))));

    const QList<MonitoredNodeRecord> records = database.loadAll();
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.at(0).displayName, QStringLiteral("New"));
}

/*!
 * \brief Verifies that remove() deletes the matching record.
 */
void NodeDatabaseTest::removeDeletesRecord()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    NodeDatabase database(QStringLiteral("test_remove"));
    QVERIFY(database.open(dir.filePath(QStringLiteral("nodes.db"))));

    const MonitoredNodeRecord record = makeRecord(QStringLiteral("ns=1;s=A"), QStringLiteral("A"));
    QVERIFY(database.insert(record));
    QVERIFY(database.remove(record.server, record.nodeId));
    QVERIFY(database.loadAll().isEmpty());
}

QTEST_MAIN(NodeDatabaseTest)

#include "tst_nodedatabase.moc"
