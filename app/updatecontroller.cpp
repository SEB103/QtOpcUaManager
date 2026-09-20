// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "updatecontroller.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

#include "productinfo.h"

namespace {
/*! \internal Settings key holding the automatic startup-check preference. */
constexpr auto kCheckAutomaticallyKey = "updates/checkAutomatically";

/*! \internal Maximum time a release query may take before it is aborted. */
constexpr int kRequestTimeoutMs = 10000;
} // namespace

/*!
 * \brief Creates the controller and loads the persisted startup-check preference.
 * \param settings Non-owning INI store for the preference, or null to disable
 *        persistence.
 * \param parent Optional QObject parent.
 *
 * The network manager is created lazily on the first checkNow() so that a run
 * that never checks for updates allocates nothing.
 */
UpdateController::UpdateController(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (m_settings)
        m_checkAutomatically = m_settings->value(QLatin1String(kCheckAutomaticallyKey), false).toBool();
}

UpdateController::~UpdateController() = default;

/*!
 * \brief Returns the current state of the update check.
 */
UpdateController::Status UpdateController::status() const
{
    return m_status;
}

/*!
 * \brief Returns whether a check is currently in progress.
 */
bool UpdateController::isChecking() const
{
    return m_status == Status::Checking;
}

/*!
 * \brief Returns whether a newer version is available.
 */
bool UpdateController::isUpdateAvailable() const
{
    return m_status == Status::UpdateAvailable;
}

/*!
 * \brief Returns whether the in-app update check is enabled in the configuration.
 *
 * The value comes from the compile-time PRODUCT_UPDATE_ENABLED flag, which is
 * generated from the \c update.enabled field of packaging/product.json.
 */
bool UpdateController::featureEnabled() const
{
    return PRODUCT_UPDATE_ENABLED;
}

/*!
 * \brief Returns whether an update check runs automatically at startup.
 */
bool UpdateController::checkAutomatically() const
{
    return m_checkAutomatically;
}

/*!
 * \brief Returns the running application version.
 */
QString UpdateController::currentVersion() const
{
    return QCoreApplication::applicationVersion();
}

/*!
 * \brief Returns the latest version reported by the release server, or empty.
 */
QString UpdateController::latestVersion() const
{
    return m_latestVersion;
}

/*!
 * \brief Returns the release web page to open, or an empty string.
 */
QString UpdateController::releaseUrl() const
{
    return m_releaseUrl;
}

/*!
 * \brief Returns a human-readable message describing the current status.
 */
QString UpdateController::statusMessage() const
{
    return m_statusMessage;
}

/*!
 * \brief Persists and applies whether the startup check runs.
 *
 * Ignores an unchanged value. The preference is written through the injected
 * settings store, mirroring how the UI language is persisted.
 * \param enabled Whether the automatic startup update check is turned on.
 */
void UpdateController::setCheckAutomatically(bool enabled)
{
    if (enabled == m_checkAutomatically)
        return;

    m_checkAutomatically = enabled;

    if (m_settings) {
        m_settings->setValue(QLatin1String(kCheckAutomaticallyKey), enabled);
        m_settings->sync();
    }

    emit checkAutomaticallyChanged();
}

/*!
 * \brief Starts an update check against the configured release server.
 *
 * When the feature is disabled the check resolves immediately to
 * Status::NotConfigured without any network access. A check already in progress
 * is left running rather than restarted. The request carries a User-Agent and a
 * transfer timeout because the GitHub API rejects requests without the former and
 * a stalled connection must not leave the dialog spinning forever.
 */
void UpdateController::checkNow()
{
    if (!featureEnabled()) {
        setStatus(Status::NotConfigured,
                  tr("Update checking is not yet available."));
        return;
    }

    if (m_reply)
        return;

    setStatus(Status::Checking, tr("Checking for updates…"));

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request(QUrl(QStringLiteral(PRODUCT_UPDATE_RELEASES_API)));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OpcUaManager/%1").arg(currentVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(kRequestTimeoutMs);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        handleReply(reply);
    });
}

/*!
 * \internal
 * \brief Parses the finished GitHub Releases \a reply and updates the state.
 *
 * A network error (including an aborted transfer or a 404 when no release exists
 * yet) becomes Status::Error. Otherwise the \c tag_name field is compared against
 * the running version to choose between Status::UpToDate and
 * Status::UpdateAvailable; the release page comes from the reply's \c html_url,
 * falling back to the configured releases page.
 */
void UpdateController::handleReply(QNetworkReply *reply)
{
    if (!reply)
        return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        setStatus(Status::Error,
                  tr("Could not check for updates: %1").arg(reply->errorString()));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject release = doc.object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        setStatus(Status::Error, tr("Could not read the latest version."));
        return;
    }

    if (compareVersions(tag, currentVersion()) > 0) {
        m_latestVersion = tag;
        const QString htmlUrl = release.value(QStringLiteral("html_url")).toString();
        m_releaseUrl = htmlUrl.isEmpty()
                           ? QStringLiteral(PRODUCT_UPDATE_RELEASES_PAGE)
                           : htmlUrl;
        emit latestVersionChanged();
        emit releaseUrlChanged();
        setStatus(Status::UpdateAvailable,
                  tr("A new version (%1) is available.").arg(m_latestVersion));
    } else {
        m_latestVersion = tag;
        emit latestVersionChanged();
        setStatus(Status::UpToDate,
                  tr("You are running the latest version."));
    }
}

/*!
 * \internal
 * \brief Sets status() and statusMessage() together and notifies once.
 */
void UpdateController::setStatus(Status status, const QString &message)
{
    m_status = status;
    m_statusMessage = message;
    emit statusChanged();
}

/*!
 * \brief Compares dotted numeric versions, ignoring a leading "v".
 *
 * Only the numeric MAJOR.MINOR.PATCH segments are compared; a missing segment
 * counts as zero and any trailing pre-release suffix is ignored. This is enough
 * for the semantic versions the packaging layer produces.
 * \param lhs Left-hand version string.
 * \param rhs Right-hand version string.
 * \return A negative value if \a lhs precedes \a rhs, zero if they are equal, and
 *         a positive value if \a lhs follows \a rhs.
 */
int UpdateController::compareVersions(const QString &lhs, const QString &rhs)
{
    const auto parse = [](const QString &value) {
        QString trimmed = value.trimmed();
        if (trimmed.startsWith(QLatin1Char('v')) || trimmed.startsWith(QLatin1Char('V')))
            trimmed.remove(0, 1);
        QList<int> parts;
        const QStringList tokens = trimmed.split(QLatin1Char('.'));
        for (const QString &token : tokens) {
            int digits = 0;
            while (digits < token.size() && token.at(digits).isDigit())
                ++digits;
            parts.append(token.left(digits).toInt());
        }
        return parts;
    };

    const QList<int> a = parse(lhs);
    const QList<int> b = parse(rhs);
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int av = i < a.size() ? a.at(i) : 0;
        const int bv = i < b.size() ? b.at(i) : 0;
        if (av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}
