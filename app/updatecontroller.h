// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UPDATECONTROLLER_H
#define UPDATECONTROLLER_H

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
QT_END_NAMESPACE

/**
 * Checks for application updates and exposes the result to QML as \c cppUpdate.
 *
 * The check queries the GitHub Releases API for the latest published version and
 * compares its tag against the running application version. The whole feature is
 * gated by featureEnabled(): when it is false (the default until a release server
 * exists) checkNow() reports Status::NotConfigured without any network access, so
 * the client can ship complete and be switched on later from packaging/product.json.
 *
 * The manual "Check for updates" menu item calls checkNow() and shows the result
 * dialog. When checkAutomatically() is set, main() triggers a silent check at
 * startup that surfaces the dialog only when an update is available. The
 * preference is persisted through the injected QSettings store under
 * \c updates/checkAutomatically.
 */
class UpdateController : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UpdateController)

    /** Current state of the update check. */
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)

    /** Whether a check is currently in progress (convenience over status). */
    Q_PROPERTY(bool checking READ isChecking NOTIFY statusChanged)

    /** Whether a newer version is available (convenience over status). */
    Q_PROPERTY(bool updateAvailable READ isUpdateAvailable NOTIFY statusChanged)

    /** Whether the in-app update check is enabled in the product configuration. */
    Q_PROPERTY(bool featureEnabled READ featureEnabled CONSTANT)

    /** Whether an update check runs automatically at startup; persisted. */
    Q_PROPERTY(bool checkAutomatically READ checkAutomatically WRITE setCheckAutomatically NOTIFY checkAutomaticallyChanged)

    /** Running application version, for example \c "1.0.0". */
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)

    /** Latest version reported by the release server; empty until known. */
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)

    /** Web page to open for a newer release; empty unless an update is available. */
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY releaseUrlChanged)

    /** Human-readable message describing the current status. */
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)

public:
    /** Lifecycle of a single update check, mirrored by the result dialog. */
    enum class Status
    {
        Idle,            /**< No check has run yet. */
        Checking,        /**< A check is in progress. */
        UpToDate,        /**< The running version is the latest. */
        UpdateAvailable, /**< A newer version is available. */
        NotConfigured,   /**< The update feature is disabled in the configuration. */
        Error            /**< The check failed (network or parse error). */
    };
    Q_ENUM(Status)

    /** Creates the controller using \a settings for persistence (may be null). */
    explicit UpdateController(QSettings *settings, QObject *parent = nullptr);

    /** Destroys the controller and its owned network manager. */
    ~UpdateController() override;

    /** Returns the current state of the update check. */
    Status status() const;

    /** Returns whether a check is currently in progress. */
    bool isChecking() const;

    /** Returns whether a newer version is available. */
    bool isUpdateAvailable() const;

    /** Returns whether the in-app update check is enabled in the configuration. */
    bool featureEnabled() const;

    /** Returns whether an update check runs automatically at startup. */
    bool checkAutomatically() const;

    /** Returns the running application version. */
    QString currentVersion() const;

    /** Returns the latest version reported by the release server, or an empty string. */
    QString latestVersion() const;

    /** Returns the release web page to open, or an empty string. */
    QString releaseUrl() const;

    /** Returns a human-readable message describing the current status. */
    QString statusMessage() const;

    /** Persists and applies whether the startup check runs; ignores an unchanged value. */
    void setCheckAutomatically(bool enabled);

    /** Starts an update check; reports NotConfigured immediately when disabled. */
    Q_INVOKABLE void checkNow();

    /**
     * Compares dotted numeric versions \a lhs and \a rhs, ignoring a leading "v".
     * Only the numeric MAJOR.MINOR.PATCH segments are compared; a missing segment
     * counts as zero and any trailing pre-release suffix is ignored.
     * \return A negative value when lhs < rhs, zero when equal, positive otherwise.
     */
    static int compareVersions(const QString &lhs, const QString &rhs);

signals:
    /** Emitted when status() or statusMessage() changes. */
    void statusChanged();

    /** Emitted when the persisted startup-check preference changes. */
    void checkAutomaticallyChanged();

    /** Emitted when the latest reported version changes. */
    void latestVersionChanged();

    /** Emitted when the release page URL changes. */
    void releaseUrlChanged();

private:
    /** Parses the finished GitHub Releases \a reply and updates the state. */
    void handleReply(QNetworkReply *reply);

    /** Sets status()/statusMessage() together and notifies once. */
    void setStatus(Status status, const QString &message);

    /** Non-owning INI settings store used to persist the preference. */
    QSettings *m_settings {nullptr};

    /** Network manager for the release query; created on first use, owned. */
    QNetworkAccessManager *m_network {nullptr};

    /** In-flight release query, or null when no check is running. */
    QNetworkReply *m_reply {nullptr};

    /** Current state of the update check. */
    Status m_status {Status::Idle};

    /** Human-readable message for the current status. */
    QString m_statusMessage;

    /** Latest version reported by the release server. */
    QString m_latestVersion;

    /** Release page to open for a newer version. */
    QString m_releaseUrl;

    /** Whether the startup check is enabled, loaded from settings. */
    bool m_checkAutomatically {false};
};

#endif // UPDATECONTROLLER_H
