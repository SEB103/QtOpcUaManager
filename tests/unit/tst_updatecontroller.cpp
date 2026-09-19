// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "updatecontroller.h"

/*!
 * \internal
 * \brief Unit tests for UpdateController version comparison and preference storage.
 *
 * The network path is not exercised here: it requires a reachable release server
 * and the feature is gated off by default. These tests cover the deterministic,
 * offline behaviour — semantic version ordering and the persisted
 * automatic-check preference.
 */
class TestUpdateController : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies the semantic version ordering used to decide UpToDate vs UpdateAvailable. */
    void compareVersions_data();
    void compareVersions();

    /*! Verifies that the automatic-check preference round-trips through QSettings. */
    void checkAutomaticallyPersists();

    /*! Verifies that a disabled feature reports NotConfigured without networking. */
    void disabledFeatureReportsNotConfigured();
};

void TestUpdateController::compareVersions_data()
{
    QTest::addColumn<QString>("lhs");
    QTest::addColumn<QString>("rhs");
    QTest::addColumn<int>("sign");

    QTest::newRow("equal") << "1.0.0" << "1.0.0" << 0;
    QTest::newRow("patch-greater") << "1.0.1" << "1.0.0" << 1;
    QTest::newRow("patch-less") << "1.0.0" << "1.0.1" << -1;
    QTest::newRow("minor-greater") << "1.2.0" << "1.1.9" << 1;
    QTest::newRow("major-greater") << "2.0.0" << "1.9.9" << 1;
    QTest::newRow("leading-v") << "v1.2.0" << "1.2.0" << 0;
    QTest::newRow("missing-segment-equal") << "1.0" << "1.0.0" << 0;
    QTest::newRow("missing-segment-less") << "1.0" << "1.0.1" << -1;
    QTest::newRow("prerelease-suffix") << "1.2.0-rc1" << "1.2.0" << 0;
}

void TestUpdateController::compareVersions()
{
    QFETCH(QString, lhs);
    QFETCH(QString, rhs);
    QFETCH(int, sign);

    const int result = UpdateController::compareVersions(lhs, rhs);
    if (sign == 0)
        QCOMPARE(result, 0);
    else if (sign < 0)
        QVERIFY(result < 0);
    else
        QVERIFY(result > 0);
}

void TestUpdateController::checkAutomaticallyPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("settings.ini"));

    {
        QSettings settings(iniPath, QSettings::IniFormat);
        UpdateController controller(&settings);
        QCOMPARE(controller.checkAutomatically(), false);

        QSignalSpy spy(&controller, &UpdateController::checkAutomaticallyChanged);
        controller.setCheckAutomatically(true);
        QCOMPARE(controller.checkAutomatically(), true);
        QCOMPARE(spy.count(), 1);

        // Setting the same value again must not emit a redundant change.
        controller.setCheckAutomatically(true);
        QCOMPARE(spy.count(), 1);
    }

    // A fresh controller reads the persisted preference back.
    QSettings reopened(iniPath, QSettings::IniFormat);
    UpdateController restored(&reopened);
    QCOMPARE(restored.checkAutomatically(), true);
}

void TestUpdateController::disabledFeatureReportsNotConfigured()
{
    UpdateController controller(nullptr);
    if (controller.featureEnabled())
        QSKIP("Update feature is enabled in this build; network path is not unit-tested.");

    controller.checkNow();
    QCOMPARE(controller.status(), UpdateController::Status::NotConfigured);
    QVERIFY(!controller.statusMessage().isEmpty());
}

QTEST_GUILESS_MAIN(TestUpdateController)

#include "tst_updatecontroller.moc"
