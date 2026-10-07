// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UILAYOUTCONTROLLER_H
#define UILAYOUTCONTROLLER_H

#include <QList>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

/**
 * Persists the main window state and the splitter layout, exposed to QML as \c cppUiLayout.
 *
 * Stores the normal (restored) window geometry, the window visibility (normal,
 * maximized, or full screen), and the saved states of the resizable SplitView
 * panes, so the next start reopens the last user layout.
 */
class UiLayoutController : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UiLayoutController)

public:
    /** Smallest window size accepted when a saved geometry is restored. */
    static constexpr QSize MinimumWindowSize {800, 600};

    /** Creates the controller on top of the non-owning \a settings store (may be null). */
    explicit UiLayoutController(QSettings *settings, QObject *parent = nullptr);

    /** Returns the saved normal window geometry fitted to the current screens, or an empty rect. */
    Q_INVOKABLE QRectF restoredWindowGeometry() const;

    /** Returns the saved window visibility as a QWindow::Visibility value (Windowed by default). */
    Q_INVOKABLE int restoredWindowVisibility() const;

    /** Saves the normal window \a geometry and the window \a visibility. */
    Q_INVOKABLE void saveWindowState(const QRectF &geometry, int visibility);

    /** Returns the saved SplitView states keyed by layout name. */
    Q_INVOKABLE QVariantMap splitStates() const;

    /** Saves the SplitView \a states keyed by layout name; non-binary values are skipped. */
    Q_INVOKABLE void saveSplitStates(const QVariantMap &states);

    /**
     * Fits \a geometry onto one of the \a availableAreas: shrinks it to the area,
     * then moves it fully inside. Returns an empty rect for an unusable geometry.
     */
    static QRect fitToScreens(const QRect &geometry, const QList<QRect> &availableAreas,
                              const QSize &minimumSize = MinimumWindowSize);

private:
    /** Non-owning INI settings store used for persistence. */
    QSettings *m_settings {nullptr};
};

#endif // UILAYOUTCONTROLLER_H
