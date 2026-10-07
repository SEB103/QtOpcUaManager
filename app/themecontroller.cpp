// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "themecontroller.h"

#include <QSettings>

namespace {
/*!
 * \internal
 * \brief Settings key holding the UI theme mode.
 */
constexpr auto kThemeSettingsKey = "ui/theme";
} // namespace

/*!
 * \brief Creates the controller and restores the saved theme mode.
 * \param settings Non-owning INI store for the theme preference, or null to
 *        disable persistence.
 * \param parent Optional QObject parent.
 *
 * A missing or unknown saved value (for example edited by hand) selects the
 * \c "system" mode, which keeps the behavior of releases without a stored theme.
 */
ThemeController::ThemeController(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (m_settings) {
        const QString saved = m_settings->value(QLatin1String(kThemeSettingsKey))
                                  .toString().trimmed().toLower();
        if (modes().contains(saved))
            m_mode = saved;
    }
}

/*!
 * \brief Returns the active theme mode.
 */
QString ThemeController::mode() const
{
    return m_mode;
}

/*!
 * \brief Applies \a mode and persists it.
 * \param mode One of \c "system", \c "light", or \c "dark".
 *
 * Unknown values are ignored so a QML typo cannot store an unusable preference.
 * The value is written immediately, so the choice survives an abnormal exit.
 */
void ThemeController::setMode(const QString &mode)
{
    if (!modes().contains(mode) || mode == m_mode)
        return;

    m_mode = mode;
    if (m_settings)
        m_settings->setValue(QLatin1String(kThemeSettingsKey), m_mode);
    emit modeChanged();
}

/*!
 * \brief Returns the supported theme modes in display order.
 */
QStringList ThemeController::modes()
{
    return {SystemMode, LightMode, DarkMode};
}
