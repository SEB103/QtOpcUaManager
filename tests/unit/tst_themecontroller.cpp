// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include "themecontroller.h"

/*!
 * \internal
 * \brief Unit tests for ThemeController: default mode, validation, and
 *        persistence of the UI theme choice.
 */
class TestThemeController : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a controller without saved state follows the system theme. */
    void defaultsToSystem();

    /*! Verifies that valid modes apply once and unknown modes are ignored. */
    void setModeValidates();

    /*! Verifies that the mode round-trips through QSettings and bad values fall back. */
    void persistsThroughSettings();

private:
    /*! Creates an INI settings store inside \a dir. */
    static std::unique_ptr<QSettings> createSettings(const QTemporaryDir &dir);
};

std::unique_ptr<QSettings> TestThemeController::createSettings(const QTemporaryDir &dir)
{
    return std::make_unique<QSettings>(dir.filePath(QStringLiteral("theme.ini")),
                                       QSettings::IniFormat);
}

void TestThemeController::defaultsToSystem()
{
    ThemeController controller(nullptr);
    QCOMPARE(controller.mode(), ThemeController::SystemMode);
    QCOMPARE(ThemeController::modes(),
             QStringList({QStringLiteral("system"), QStringLiteral("light"),
                          QStringLiteral("dark")}));
}

void TestThemeController::setModeValidates()
{
    ThemeController controller(nullptr);
    QSignalSpy spy(&controller, &ThemeController::modeChanged);

    controller.setMode(ThemeController::LightMode);
    QCOMPARE(controller.mode(), ThemeController::LightMode);
    QCOMPARE(spy.count(), 1);

    // Re-applying the active mode does not report a change.
    controller.setMode(ThemeController::LightMode);
    QCOMPARE(spy.count(), 1);

    controller.setMode(QStringLiteral("purple"));
    QCOMPARE(controller.mode(), ThemeController::LightMode);
    QCOMPARE(spy.count(), 1);

    controller.setMode(ThemeController::DarkMode);
    QCOMPARE(controller.mode(), ThemeController::DarkMode);
    QCOMPARE(spy.count(), 2);
}

void TestThemeController::persistsThroughSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        auto settings = createSettings(dir);
        ThemeController controller(settings.get());
        QCOMPARE(controller.mode(), ThemeController::SystemMode);
        controller.setMode(ThemeController::LightMode);
    }
    {
        auto settings = createSettings(dir);
        ThemeController controller(settings.get());
        QCOMPARE(controller.mode(), ThemeController::LightMode);

        // A hand-edited value is accepted regardless of case and whitespace.
        settings->setValue(QStringLiteral("ui/theme"), QStringLiteral(" Dark "));
    }
    {
        auto settings = createSettings(dir);
        ThemeController controller(settings.get());
        QCOMPARE(controller.mode(), ThemeController::DarkMode);

        settings->setValue(QStringLiteral("ui/theme"), QStringLiteral("garbage"));
    }
    {
        auto settings = createSettings(dir);
        ThemeController controller(settings.get());
        QCOMPARE(controller.mode(), ThemeController::SystemMode);
    }
}

QTEST_GUILESS_MAIN(TestThemeController)

#include "tst_themecontroller.moc"
