// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QSettings>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest>

#include <memory>

#include "uilayoutcontroller.h"

/*!
 * \internal
 * \brief Unit tests for UiLayoutController: window state and splitter layout
 *        persistence, and fitting a saved geometry onto the available screens.
 */
class TestUiLayoutController : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that nothing is restored without saved state. */
    void defaultsWithoutSavedState();

    /*! Verifies that geometry and visibility round-trip through QSettings. */
    void persistsWindowState();

    /*! Verifies the stored visibility for each QWindow::Visibility value. */
    void visibilityMapping_data();
    void visibilityMapping();

    /*! Verifies that split states round-trip and invalid entries are skipped. */
    void persistsSplitStates();

    /*! Verifies how saved geometries are fitted onto the screen areas. */
    void fitToScreens_data();
    void fitToScreens();

private:
    /*! Creates an INI settings store inside \a dir. */
    static std::unique_ptr<QSettings> createSettings(const QTemporaryDir &dir);
};

std::unique_ptr<QSettings> TestUiLayoutController::createSettings(const QTemporaryDir &dir)
{
    return std::make_unique<QSettings>(dir.filePath(QStringLiteral("layout.ini")),
                                       QSettings::IniFormat);
}

void TestUiLayoutController::defaultsWithoutSavedState()
{
    UiLayoutController withoutStore(nullptr);
    QVERIFY(withoutStore.restoredWindowGeometry().isEmpty());
    QCOMPARE(withoutStore.restoredWindowVisibility(), int(QWindow::Windowed));
    QVERIFY(withoutStore.splitStates().isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto settings = createSettings(dir);
    UiLayoutController controller(settings.get());
    QVERIFY(controller.restoredWindowGeometry().isEmpty());
    QCOMPARE(controller.restoredWindowVisibility(), int(QWindow::Windowed));
    QVERIFY(controller.splitStates().isEmpty());
}

void TestUiLayoutController::persistsWindowState()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        auto settings = createSettings(dir);
        UiLayoutController controller(settings.get());
        controller.saveWindowState(QRectF(120, 80, 1400, 900), QWindow::Maximized);
    }
    {
        auto settings = createSettings(dir);
        UiLayoutController controller(settings.get());
        // The GUI-less test has no screens, so the geometry comes back unfitted.
        QCOMPARE(controller.restoredWindowGeometry(), QRectF(120, 80, 1400, 900));
        QCOMPARE(controller.restoredWindowVisibility(), int(QWindow::Maximized));

        // An empty geometry keeps the saved one but still updates the visibility.
        controller.saveWindowState(QRectF(), QWindow::Windowed);
    }
    {
        auto settings = createSettings(dir);
        UiLayoutController controller(settings.get());
        QCOMPARE(controller.restoredWindowGeometry(), QRectF(120, 80, 1400, 900));
        QCOMPARE(controller.restoredWindowVisibility(), int(QWindow::Windowed));

        settings->setValue(QStringLiteral("ui/window/visibility"), QStringLiteral("garbage"));
        QCOMPARE(controller.restoredWindowVisibility(), int(QWindow::Windowed));
    }
}

void TestUiLayoutController::visibilityMapping_data()
{
    QTest::addColumn<int>("saved");
    QTest::addColumn<int>("restored");

    QTest::newRow("windowed") << int(QWindow::Windowed) << int(QWindow::Windowed);
    QTest::newRow("maximized") << int(QWindow::Maximized) << int(QWindow::Maximized);
    QTest::newRow("fullscreen") << int(QWindow::FullScreen) << int(QWindow::FullScreen);
    QTest::newRow("minimized") << int(QWindow::Minimized) << int(QWindow::Windowed);
    QTest::newRow("hidden") << int(QWindow::Hidden) << int(QWindow::Windowed);
}

void TestUiLayoutController::visibilityMapping()
{
    QFETCH(int, saved);
    QFETCH(int, restored);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto settings = createSettings(dir);
    UiLayoutController controller(settings.get());
    controller.saveWindowState(QRectF(0, 0, 1000, 700), saved);
    QCOMPARE(controller.restoredWindowVisibility(), restored);
}

void TestUiLayoutController::persistsSplitStates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    {
        auto settings = createSettings(dir);
        UiLayoutController controller(settings.get());
        controller.saveSplitStates({
            {QStringLiteral("browserColumns"), QByteArray("columns-state")},
            {QStringLiteral("addressSpaceSegments"), QByteArray("segments-state")},
            {QStringLiteral("bad/key"), QByteArray("ignored")},
            {QStringLiteral("notBinary"), QStringLiteral("ignored")},
            {QStringLiteral("empty"), QByteArray()},
        });
    }
    {
        auto settings = createSettings(dir);
        UiLayoutController controller(settings.get());
        const QVariantMap states = controller.splitStates();
        QCOMPARE(states.size(), 2);
        QCOMPARE(states.value(QStringLiteral("browserColumns")).toByteArray(),
                 QByteArray("columns-state"));
        QCOMPARE(states.value(QStringLiteral("addressSpaceSegments")).toByteArray(),
                 QByteArray("segments-state"));

        // Saving a subset updates it and keeps the other layouts.
        controller.saveSplitStates({{QStringLiteral("browserColumns"), QByteArray("updated")}});
    }
    {
        auto settings = createSettings(dir);
        UiLayoutController controller(settings.get());
        const QVariantMap states = controller.splitStates();
        QCOMPARE(states.size(), 2);
        QCOMPARE(states.value(QStringLiteral("browserColumns")).toByteArray(),
                 QByteArray("updated"));
        QCOMPARE(states.value(QStringLiteral("addressSpaceSegments")).toByteArray(),
                 QByteArray("segments-state"));
    }
}

void TestUiLayoutController::fitToScreens_data()
{
    QTest::addColumn<QRect>("saved");
    QTest::addColumn<QList<QRect>>("areas");
    QTest::addColumn<QRect>("expected");

    const QRect primary(0, 0, 1920, 1040);
    const QRect secondary(1920, 0, 2560, 1400);

    QTest::newRow("inside primary")
        << QRect(100, 100, 1200, 800) << QList<QRect>{primary}
        << QRect(100, 100, 1200, 800);
    QTest::newRow("inside secondary")
        << QRect(2200, 200, 1600, 1000) << QList<QRect>{primary, secondary}
        << QRect(2200, 200, 1600, 1000);
    QTest::newRow("partly off the right edge")
        << QRect(1500, 100, 1200, 800) << QList<QRect>{primary}
        << QRect(720, 100, 1200, 800);
    QTest::newRow("disconnected monitor")
        << QRect(2200, 200, 1200, 800) << QList<QRect>{primary}
        << QRect(360, 120, 1200, 800);
    QTest::newRow("larger than the screen")
        << QRect(-50, -50, 2500, 1500) << QList<QRect>{primary}
        << QRect(0, 0, 1920, 1040);
    QTest::newRow("smaller than the minimum")
        << QRect(100, 100, 300, 200) << QList<QRect>{primary}
        << QRect(100, 100, 800, 600);
    QTest::newRow("no screens")
        << QRect(5000, 5000, 1200, 800) << QList<QRect>{}
        << QRect(5000, 5000, 1200, 800);
    QTest::newRow("empty geometry")
        << QRect() << QList<QRect>{primary}
        << QRect();
}

void TestUiLayoutController::fitToScreens()
{
    QFETCH(QRect, saved);
    QFETCH(QList<QRect>, areas);
    QFETCH(QRect, expected);

    QCOMPARE(UiLayoutController::fitToScreens(saved, areas), expected);
}

QTEST_GUILESS_MAIN(TestUiLayoutController)

#include "tst_uilayoutcontroller.moc"
