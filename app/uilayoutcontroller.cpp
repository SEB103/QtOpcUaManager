// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "uilayoutcontroller.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QWindow>

#include <algorithm>

namespace {
/*!
 * \internal
 * \brief Settings key holding the normal (restored) window geometry.
 */
constexpr auto kGeometryKey = "ui/window/geometry";

/*!
 * \internal
 * \brief Settings key holding the window visibility: "normal", "maximized", or "fullscreen".
 */
constexpr auto kVisibilityKey = "ui/window/visibility";

/*!
 * \internal
 * \brief Settings group holding the saved SplitView states, one key per layout name.
 */
constexpr auto kSplitGroup = "ui/splitters";

/*!
 * \internal
 * \brief Returns whether \a key can be stored as a single settings key.
 *
 * QSettings treats slashes as group separators, so they are rejected to keep
 * every split state directly inside the splitter group.
 */
bool isValidSplitKey(const QString &key)
{
    return !key.isEmpty() && !key.contains(QLatin1Char('/'))
           && !key.contains(QLatin1Char('\\'));
}
} // namespace

/*!
 * \brief Creates the controller.
 * \param settings Non-owning INI store for the layout, or null to disable persistence.
 * \param parent Optional QObject parent.
 */
UiLayoutController::UiLayoutController(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

/*!
 * \brief Returns the saved normal window geometry fitted to the current screens.
 *
 * The geometry is in device-independent pixels and describes the client area,
 * as the QML Window \c x, \c y, \c width, and \c height properties do. A saved
 * window that is no longer on any screen (for example after a monitor was
 * disconnected) is moved onto the primary screen; one larger than its screen is
 * shrunk to fit. Returns an empty rect when nothing usable is saved, so the
 * window keeps its default size and the platform placement.
 */
QRectF UiLayoutController::restoredWindowGeometry() const
{
    if (!m_settings)
        return {};

    const QRect saved = m_settings->value(QLatin1String(kGeometryKey)).toRect();

    QList<QRect> areas;
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        if (QScreen *primary = QGuiApplication::primaryScreen())
            areas.append(primary->availableGeometry());
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (const QScreen *screen : screens) {
            if (screen != QGuiApplication::primaryScreen())
                areas.append(screen->availableGeometry());
        }
    }

    return QRectF(fitToScreens(saved, areas));
}

/*!
 * \brief Returns the saved window visibility as a QWindow::Visibility value.
 *
 * Only \c Windowed, \c Maximized, and \c FullScreen are restored; a missing or
 * unknown value selects \c Windowed. A minimized window is never restored as
 * minimized, because the QML side stores the last visible state instead.
 */
int UiLayoutController::restoredWindowVisibility() const
{
    const QString saved = m_settings
                              ? m_settings->value(QLatin1String(kVisibilityKey)).toString()
                              : QString();
    if (saved == QLatin1String("maximized"))
        return QWindow::Maximized;
    if (saved == QLatin1String("fullscreen"))
        return QWindow::FullScreen;
    return QWindow::Windowed;
}

/*!
 * \brief Saves the normal window geometry and the window visibility.
 * \param geometry Normal (not maximized) client-area geometry; an empty rect
 *        keeps the previously saved geometry.
 * \param visibility QWindow::Visibility value; values other than \c Maximized
 *        and \c FullScreen are stored as normal.
 */
void UiLayoutController::saveWindowState(const QRectF &geometry, int visibility)
{
    if (!m_settings)
        return;

    const QRect rect = geometry.toRect();
    if (rect.width() > 0 && rect.height() > 0)
        m_settings->setValue(QLatin1String(kGeometryKey), rect);

    QString value = QStringLiteral("normal");
    if (visibility == QWindow::Maximized)
        value = QStringLiteral("maximized");
    else if (visibility == QWindow::FullScreen)
        value = QStringLiteral("fullscreen");
    m_settings->setValue(QLatin1String(kVisibilityKey), value);
}

/*!
 * \brief Returns the saved SplitView states keyed by layout name.
 *
 * Each value is the QByteArray produced by SplitView.saveState(); QML receives
 * it as an ArrayBuffer that SplitView.restoreState() accepts unchanged.
 */
QVariantMap UiLayoutController::splitStates() const
{
    QVariantMap states;
    if (!m_settings)
        return states;

    m_settings->beginGroup(QLatin1String(kSplitGroup));
    const QStringList keys = m_settings->childKeys();
    for (const QString &key : keys) {
        const QByteArray state = m_settings->value(key).toByteArray();
        if (!state.isEmpty())
            states.insert(key, state);
    }
    m_settings->endGroup();
    return states;
}

/*!
 * \brief Saves the SplitView states keyed by layout name.
 * \param states Map from layout name to the SplitView.saveState() result.
 *
 * Entries with an invalid name or a value that is not binary state data are
 * skipped; states of layouts not listed in \a states are kept unchanged.
 */
void UiLayoutController::saveSplitStates(const QVariantMap &states)
{
    if (!m_settings)
        return;

    m_settings->beginGroup(QLatin1String(kSplitGroup));
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        if (!isValidSplitKey(it.key()) || it.value().typeId() != QMetaType::QByteArray)
            continue;
        const QByteArray state = it.value().toByteArray();
        if (!state.isEmpty())
            m_settings->setValue(it.key(), state);
    }
    m_settings->endGroup();
}

/*!
 * \brief Fits a saved window geometry onto the available screen areas.
 * \param geometry Saved client-area geometry.
 * \param availableAreas Available geometries of the screens, primary first.
 * \param minimumSize Smallest size the result is grown to before fitting.
 * \return The fitted geometry, or an empty rect when \a geometry has no area.
 *
 * The target screen is the one that shows the largest part of the window. When
 * no screen shows any of it, the window is centered on the first (primary)
 * area. The size is grown to \a minimumSize, shrunk to the target area, and the
 * window is then moved so it lies completely inside that area. Without any
 * area the size-adjusted geometry is returned unchanged.
 */
QRect UiLayoutController::fitToScreens(const QRect &geometry,
                                       const QList<QRect> &availableAreas,
                                       const QSize &minimumSize)
{
    if (geometry.width() <= 0 || geometry.height() <= 0)
        return {};

    QRect rect(geometry.topLeft(), geometry.size().expandedTo(minimumSize));
    if (availableAreas.isEmpty())
        return rect;

    const QRect *target = nullptr;
    qint64 bestArea = 0;
    for (const QRect &area : availableAreas) {
        const QRect visible = area.intersected(rect);
        const qint64 visibleArea = qint64(visible.width()) * visible.height();
        if (visibleArea > bestArea) {
            bestArea = visibleArea;
            target = &area;
        }
    }

    if (!target) {
        target = &availableAreas.constFirst();
        rect.setSize(rect.size().boundedTo(target->size()));
        rect.moveCenter(target->center());
    } else {
        rect.setSize(rect.size().boundedTo(target->size()));
    }

    rect.moveLeft(std::clamp(rect.x(), target->x(), target->x() + target->width() - rect.width()));
    rect.moveTop(std::clamp(rect.y(), target->y(), target->y() + target->height() - rect.height()));
    return rect;
}
