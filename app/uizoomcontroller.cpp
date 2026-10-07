// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "uizoomcontroller.h"

#include <QSettings>

#include <cstdlib>

namespace {
/*!
 * \internal
 * \brief Settings key holding the UI zoom in percent.
 */
constexpr auto kZoomSettingsKey = "ui/zoomPercent";
} // namespace

/*!
 * \brief Creates the controller and restores the saved zoom.
 * \param settings Non-owning INI store for the zoom preference, or null to
 *        disable persistence.
 * \param parent Optional QObject parent.
 *
 * A saved value that is not one of the zoom steps (for example edited by hand)
 * is snapped to the nearest step; a missing or unreadable value selects 100 %.
 */
UiZoomController::UiZoomController(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (m_settings) {
        bool ok = false;
        const int saved = m_settings->value(QLatin1String(kZoomSettingsKey)).toInt(&ok);
        if (ok)
            m_zoomPercent = nearestStep(saved);
    }
}

/*!
 * \brief Returns the active zoom in percent.
 */
int UiZoomController::zoomPercent() const
{
    return m_zoomPercent;
}

/*!
 * \brief Returns the active zoom as a scale factor, for example 1.25 for 125 %.
 */
qreal UiZoomController::zoomFactor() const
{
    return m_zoomPercent / 100.0;
}

/*!
 * \brief Returns whether a larger zoom step is available.
 */
bool UiZoomController::canZoomIn() const
{
    return m_zoomPercent < zoomSteps().constLast();
}

/*!
 * \brief Returns whether a smaller zoom step is available.
 */
bool UiZoomController::canZoomOut() const
{
    return m_zoomPercent > zoomSteps().constFirst();
}

/*!
 * \brief Returns whether the zoom is at the default 100 %.
 */
bool UiZoomController::isDefaultZoom() const
{
    return m_zoomPercent == DefaultZoomPercent;
}

/*!
 * \brief Returns the selectable zoom steps in percent, in ascending order.
 *
 * The ladder follows the familiar browser zoom levels: fine steps around 100 %
 * and coarser steps towards both ends.
 */
QList<int> UiZoomController::zoomSteps()
{
    return {50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200};
}

/*!
 * \brief Returns the zoom step nearest to \a percent.
 * \param percent Requested zoom in percent; values outside the ladder are
 *        clamped to its first or last step.
 *
 * On a tie between two steps the smaller step wins.
 */
int UiZoomController::nearestStep(int percent)
{
    const QList<int> steps = zoomSteps();
    int best = steps.constFirst();
    for (const int step : steps) {
        if (std::abs(step - percent) < std::abs(best - percent))
            best = step;
    }
    return best;
}

/*!
 * \brief Applies the step nearest to \a percent and persists it.
 * \param percent Requested zoom in percent.
 *
 * Emits zoomChanged() only when the effective step changes.
 */
void UiZoomController::setZoomPercent(int percent)
{
    const int step = nearestStep(percent);
    if (step == m_zoomPercent)
        return;

    m_zoomPercent = step;
    if (m_settings)
        m_settings->setValue(QLatin1String(kZoomSettingsKey), m_zoomPercent);
    emit zoomChanged();
}

/*!
 * \brief Switches to the next larger zoom step.
 */
void UiZoomController::zoomIn()
{
    for (const int step : zoomSteps()) {
        if (step > m_zoomPercent) {
            setZoomPercent(step);
            return;
        }
    }
}

/*!
 * \brief Switches to the next smaller zoom step.
 */
void UiZoomController::zoomOut()
{
    const QList<int> steps = zoomSteps();
    for (auto it = steps.crbegin(); it != steps.crend(); ++it) {
        if (*it < m_zoomPercent) {
            setZoomPercent(*it);
            return;
        }
    }
}

/*!
 * \brief Restores the default 100 % zoom.
 */
void UiZoomController::resetZoom()
{
    setZoomPercent(DefaultZoomPercent);
}
