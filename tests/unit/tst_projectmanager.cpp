#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtTest>

#include "project/projectserializer.h"
#include "qmlapi/opcuamanager.h"
#include "qmlapi/projectmanager.h"

/*!
 * \internal
 * \brief Test-only subclass exposing the protected recent-list and active-state API.
 */
class TestableProjectManager : public ProjectManager
{
public:
    using ProjectManager::addOrUpdateRecent;
    using ProjectManager::clearActiveProject;
    using ProjectManager::setActiveProject;
    using ProjectManager::setDirty;
};

/*! Verifies ProjectManager recent-list and active-project behavior. */
class ProjectManagerTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies newest-first ordering and case-insensitive de-duplication. */
    void recentListOrdersNewestFirstAndDeduplicates();

    /*! Verifies that the recent list is capped at ten entries. */
    void recentListIsCapped();

    /*! Verifies that a missing file is reported as unavailable. */
    void recentRowReportsAvailability();

    /*! Verifies that the recent list survives a settings reload. */
    void recentListPersistsAcrossReload();

    /*! Verifies active-project state and dirty transitions. */
    void activeProjectStateTracksSelection();

    /*! Verifies that creating a project writes a valid file and activates it. */
    void createProjectWritesFileAndActivates();

    /*! Verifies that opening an existing project loads and activates it. */
    void openProjectLoadsAndActivates();

    /*! Verifies that opening a missing project reports an error and stays inactive. */
    void openMissingProjectReportsError();

    /*! Verifies that a facade change marks dirty and saving clears it and persists. */
    void saveClearsDirtyAndPersistsChanges();

private:
    /*! Builds a settings store inside \a dir for one test. */
    static QSettings *makeSettings(QTemporaryDir &dir, QObject *parent);
};

QSettings *ProjectManagerTest::makeSettings(QTemporaryDir &dir, QObject *parent)
{
    return new QSettings(dir.filePath(QStringLiteral("settings.ini")),
                         QSettings::IniFormat,
                         parent);
}

/*!
 * \brief Verifies newest-first ordering and case-insensitive de-duplication.
 */
void ProjectManagerTest::recentListOrdersNewestFirstAndDeduplicates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));

    manager.addOrUpdateRecent(dir.filePath(QStringLiteral("a.uaproj")),
                              QStringLiteral("A"), QStringLiteral("opc.tcp://a"));
    manager.addOrUpdateRecent(dir.filePath(QStringLiteral("b.uaproj")),
                              QStringLiteral("B"), QStringLiteral("opc.tcp://b"));

    QVariantList rows = manager.recentProjects();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("displayName")).toString(),
             QStringLiteral("B"));

    // Re-adding "a" moves it to the front and refreshes its metadata without duplicating.
    manager.addOrUpdateRecent(dir.filePath(QStringLiteral("a.uaproj")),
                              QStringLiteral("A2"), QStringLiteral("opc.tcp://a2"));
    rows = manager.recentProjects();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("displayName")).toString(),
             QStringLiteral("A2"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("opc.tcp://a2"));
}

/*!
 * \brief Verifies that the recent list is capped at ten entries.
 */
void ProjectManagerTest::recentListIsCapped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));

    for (int i = 0; i < 15; ++i) {
        manager.addOrUpdateRecent(dir.filePath(QStringLiteral("p%1.uaproj").arg(i)),
                                  QStringLiteral("P%1").arg(i),
                                  QString());
    }

    const QVariantList rows = manager.recentProjects();
    QCOMPARE(rows.size(), 10);
    // The most recently added entry is at the front.
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("displayName")).toString(),
             QStringLiteral("P14"));
}

/*!
 * \brief Verifies that a missing file is reported as unavailable.
 */
void ProjectManagerTest::recentRowReportsAvailability()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));

    const QString existingPath = dir.filePath(QStringLiteral("real.uaproj"));
    QFile file(existingPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{}");
    file.close();

    manager.addOrUpdateRecent(existingPath, QStringLiteral("Real"), QString());
    manager.addOrUpdateRecent(dir.filePath(QStringLiteral("ghost.uaproj")),
                              QStringLiteral("Ghost"), QString());

    const QVariantList rows = manager.recentProjects();
    QCOMPARE(rows.size(), 2);
    // Ghost was added last, so it is at the front and must be unavailable.
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("displayName")).toString(),
             QStringLiteral("Ghost"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("available")).toBool(), false);
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("available")).toBool(), true);
}

/*!
 * \brief Verifies that the recent list survives a settings reload.
 */
void ProjectManagerTest::recentListPersistsAcrossReload()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString settingsPath = dir.filePath(QStringLiteral("settings.ini"));

    {
        TestableProjectManager manager;
        auto *settings = new QSettings(settingsPath, QSettings::IniFormat, &manager);
        manager.setSettings(settings);
        manager.addOrUpdateRecent(dir.filePath(QStringLiteral("keep.uaproj")),
                                  QStringLiteral("Keep"), QStringLiteral("opc.tcp://keep"));
    }

    TestableProjectManager reloaded;
    auto *settings = new QSettings(settingsPath, QSettings::IniFormat, &reloaded);
    reloaded.setSettings(settings);

    const QVariantList rows = reloaded.recentProjects();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("displayName")).toString(),
             QStringLiteral("Keep"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("opc.tcp://keep"));
}

/*!
 * \brief Verifies active-project state and dirty transitions.
 */
void ProjectManagerTest::activeProjectStateTracksSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));
    QVERIFY(!manager.hasActiveProject());

    QSignalSpy activeSpy(&manager, &ProjectManager::activeProjectChanged);
    QSignalSpy dirtySpy(&manager, &ProjectManager::dirtyChanged);

    manager.setActiveProject(dir.filePath(QStringLiteral("x.uaproj")), QStringLiteral("X"));
    QVERIFY(manager.hasActiveProject());
    QCOMPARE(manager.activeProjectName(), QStringLiteral("X"));
    QCOMPARE(activeSpy.count(), 1);
    QVERIFY(!manager.dirty());

    manager.setDirty(true);
    QVERIFY(manager.dirty());
    QCOMPARE(dirtySpy.count(), 1);

    manager.clearActiveProject();
    QVERIFY(!manager.hasActiveProject());
    QCOMPARE(manager.activeProjectName(), QString());
    QVERIFY(!manager.dirty());
}

/*!
 * \brief Verifies that creating a project writes a valid file and activates it.
 */
void ProjectManagerTest::createProjectWritesFileAndActivates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));
    QSignalSpy errorSpy(&manager, &ProjectManager::projectError);

    const QString path = dir.filePath(QStringLiteral("New.uaproj"));
    QVERIFY(manager.createProjectAtPath(path));
    QCOMPARE(errorSpy.count(), 0);

    QVERIFY(QFileInfo::exists(path));
    QVERIFY(manager.hasActiveProject());
    QCOMPARE(manager.activeProjectName(), QStringLiteral("New"));
    QCOMPARE(manager.recentProjects().size(), 1);

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(result.ok);
    QCOMPARE(result.data.displayName, QStringLiteral("New"));
}

/*!
 * \brief Verifies that opening an existing project loads and activates it.
 */
void ProjectManagerTest::openProjectLoadsAndActivates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("Loaded.uaproj"));
    ProjectData data;
    data.displayName = QStringLiteral("Loaded Project");
    data.connection.discoveryUrl = QStringLiteral("opc.tcp://host:4840");
    QString error;
    QVERIFY2(ProjectSerializer::save(path, data, &error), qPrintable(error));

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));

    QVERIFY(manager.openProject(path));
    QVERIFY(manager.hasActiveProject());
    QCOMPARE(manager.activeProjectName(), QStringLiteral("Loaded Project"));

    const QVariantList rows = manager.recentProjects();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("endpoint")).toString(),
             QStringLiteral("opc.tcp://host:4840"));
}

/*!
 * \brief Verifies that opening a missing project reports an error and stays inactive.
 */
void ProjectManagerTest::openMissingProjectReportsError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TestableProjectManager manager;
    manager.setSettings(makeSettings(dir, &manager));
    QSignalSpy errorSpy(&manager, &ProjectManager::projectError);

    QVERIFY(!manager.openProject(dir.filePath(QStringLiteral("ghost.uaproj"))));
    QVERIFY(!manager.hasActiveProject());
    QCOMPARE(errorSpy.count(), 1);
}

/*!
 * \brief Verifies that a facade change marks dirty and saving clears it and persists.
 */
void ProjectManagerTest::saveClearsDirtyAndPersistsChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    OpcUaManager opcUaManager;
    TestableProjectManager manager;
    manager.setOpcUaManager(&opcUaManager);
    manager.setSettings(makeSettings(dir, &manager));

    const QString path = dir.filePath(QStringLiteral("Dirty.uaproj"));
    QVERIFY(manager.createProjectAtPath(path));
    QVERIFY(!manager.dirty());

    // A user-driven change in the facade marks the active project dirty.
    opcUaManager.setValueFormat(OpcUaManager::FormatXml);
    QVERIFY(manager.dirty());

    QVERIFY(manager.saveProject());
    QVERIFY(!manager.dirty());

    const ProjectSerializer::LoadResult result = ProjectSerializer::load(path);
    QVERIFY(result.ok);
    QCOMPARE(result.data.settings.valueFormat, int(OpcUaManager::FormatXml));
}

QTEST_MAIN(ProjectManagerTest)

#include "tst_projectmanager.moc"
