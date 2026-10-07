// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef THEMECONTROLLER_H
#define THEMECONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

/**
 * Selects and persists the UI color theme exposed to QML as \c cppTheme.
 *
 * The theme mode is one of \c "system" (follow the operating system color
 * scheme), \c "light", or \c "dark". The main window resolves the mode into the
 * Material theme, so the start page and the workspace share one persisted choice.
 */
class ThemeController : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ThemeController)

    /** Active theme mode: \c "system", \c "light", or \c "dark". */
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    /** Mode that follows the operating system color scheme. */
    static inline const QString SystemMode = QStringLiteral("system");

    /** Mode that always uses the light theme. */
    static inline const QString LightMode = QStringLiteral("light");

    /** Mode that always uses the dark theme. */
    static inline const QString DarkMode = QStringLiteral("dark");

    /** Creates the controller and restores the saved mode from \a settings (may be null). */
    explicit ThemeController(QSettings *settings, QObject *parent = nullptr);

    /** Returns the active theme mode. */
    QString mode() const;

    /** Applies \a mode and persists it; unknown values are ignored. */
    void setMode(const QString &mode);

    /** Returns the supported theme modes. */
    static QStringList modes();

signals:
    /** Emitted when the active theme mode changes. */
    void modeChanged();

private:
    /** Non-owning INI settings store used to persist the mode. */
    QSettings *m_settings {nullptr};

    /** Active theme mode. */
    QString m_mode {SystemMode};
};

#endif // THEMECONTROLLER_H
