// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

/*!
 * \file
 * \brief Read-only application, build, and environment metadata for QML.
 */

#include "appinfo.h"

#include "helpserver.h"
#include "productinfo.h"

#include "core/apppaths.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

/*!
 * \brief Creates the metadata object.
 */
AppInfo::AppInfo(QObject *parent)
    : QObject(parent)
{}

/*!
 * \brief Returns the human-readable application name.
 */
QString AppInfo::appName() const
{
    return QStringLiteral(PRODUCT_DISPLAY_NAME);
}

/*!
 * \brief Returns the application version string.
 *
 * The value comes from QCoreApplication, which main() sets from the CMake
 * project version.
 */
QString AppInfo::appVersion() const
{
    return QCoreApplication::applicationVersion();
}

/*!
 * \brief Returns the organization the application reports itself under.
 */
QString AppInfo::organization() const
{
    return QStringLiteral(PRODUCT_ORGANIZATION);
}

/*!
 * \brief Returns the copyright line for the application's own source code.
 */
QString AppInfo::copyright() const
{
    return QStringLiteral(PRODUCT_COPYRIGHT);
}

/*!
 * \brief Returns the SPDX identifier of the application's own license.
 */
QString AppInfo::licenseName() const
{
    return QStringLiteral("GPL-3.0-or-later");
}

/*!
 * \brief Returns a one-line description of the application.
 */
QString AppInfo::description() const
{
    return QStringLiteral(
        "An OPC UA engineering client for browsing, monitoring, and testing "
        "OPC UA servers, specialised for IEC 61131 / CODESYS controllers.");
}

/*!
 * \brief Returns the project homepage URL.
 */
QString AppInfo::homepageUrl() const
{
    return QStringLiteral(PRODUCT_HOMEPAGE);
}

/*!
 * \brief Returns the Qt runtime version the application is linked against.
 */
QString AppInfo::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

/*!
 * \brief Returns the version of open62541 bundled by the Qt OPC UA backend.
 *
 * open62541 ships inside the Qt OPC UA open62541 backend plugin, so the version
 * is tied to the Qt kit rather than to this build. Update this value when the
 * Qt OPC UA module is updated.
 */
QString AppInfo::open62541Version() const
{
    return QStringLiteral("1.4.14");
}

/*!
 * \brief Returns the compiler and version the application was built with.
 */
QString AppInfo::compiler() const
{
#if defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#elif defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3")
        .arg(__clang_major__)
        .arg(__clang_minor__)
        .arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3")
        .arg(__GNUC__)
        .arg(__GNUC_MINOR__)
        .arg(__GNUC_PATCHLEVEL__);
#else
    return QStringLiteral("Unknown compiler");
#endif
}

/*!
 * \brief Returns the C++ language standard the application was built with.
 *
 * The project baseline fixes the standard at C++20 in CMake. The value is
 * reported from that baseline rather than from __cplusplus, which MSVC does not
 * report accurately without an extra compiler switch.
 */
QString AppInfo::cxxStandard() const
{
    return QStringLiteral("C++20");
}

/*!
 * \brief Returns the build configuration, "Debug" or "Release".
 */
QString AppInfo::buildType() const
{
#ifdef QT_NO_DEBUG
    return QStringLiteral("Release");
#else
    return QStringLiteral("Debug");
#endif
}

/*!
 * \brief Returns the compile date and time of this build.
 */
QString AppInfo::buildTimestamp() const
{
    return QStringLiteral(__DATE__ " " __TIME__);
}

/*!
 * \brief Returns the pretty name of the operating system.
 */
QString AppInfo::operatingSystem() const
{
    return QSysInfo::prettyProductName();
}

/*!
 * \brief Returns the CPU architecture the application runs on.
 */
QString AppInfo::cpuArchitecture() const
{
    return QSysInfo::currentCpuArchitecture();
}

/*!
 * \brief Returns the resource root holding the bundled license documents.
 */
QString AppInfo::licensesRoot() const
{
    return QStringLiteral("qrc:/licenses");
}

/*!
 * \brief Reads the UTF-8 text file at \a path for display in the About dialog.
 *
 * A "qrc:/" prefix is rewritten to the ":/" resource root so the same call works
 * for embedded resources and for a filesystem path. Returns an empty string when
 * the file cannot be opened.
 */
QString AppInfo::readText(const QString &path) const
{
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("qrc:/")))
        localPath.remove(0, 3); // "qrc:/..." -> ":/..."

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    return QString::fromUtf8(file.readAll());
}

/*!
 * \internal
 * \brief Returns the absolute path of the bundled documentation site root.
 */
QString AppInfo::docSiteRoot() const
{
    return AppPaths::instance().seedDir() + QStringLiteral("/doc/site");
}

/*!
 * \internal
 * \brief Returns the site-relative index path for \a language.
 *
 * Prefers the requested language folder, then English, then the top-level
 * language chooser, so the Help entry still works when a translation is missing.
 * Only the language part of the locale code is used, so \c "de_DE" and \c "de"
 * both resolve to \c de/index.html. Returns an empty string when no documentation
 * is present.
 */
QString AppInfo::resolveDocRelativePath(const QString &language) const
{
    const QString base = docSiteRoot();
    const QString code = language.left(2).toLower();

    const QStringList candidates {
        code + QStringLiteral("/index.html"),
        QStringLiteral("en/index.html"),
        QStringLiteral("index.html"),
    };

    for (const QString &relative : candidates) {
        if (QFileInfo::exists(base + QLatin1Char('/') + relative))
            return relative;
    }

    return {};
}

/*!
 * \brief Returns a loopback http:// URL to the offline documentation for \a language.
 *
 * The documentation site ships next to the executable under \c doc/site (see the
 * install rules and the release pipeline). Because the Windows WebView2 backend
 * refuses to load top-level \c file:// URLs, the site is served over a local
 * HTTP server (started lazily here) and the viewer loads it over
 * \c http://127.0.0.1. The same viewer follows the online Qt reference link in
 * the same view.
 *
 * \param language UI locale code such as \c "de_DE".
 * \param darkTheme When true, the docs are served with the dark stylesheet.
 * \return An \c http://127.0.0.1 URL, or an empty URL when no documentation is installed.
 */
QUrl AppInfo::helpIndexUrl(const QString &language, bool darkTheme)
{
    const QString relative = resolveDocRelativePath(language);
    if (relative.isEmpty())
        return {};

    if (!m_helpServer)
        m_helpServer = new HelpServer(this);

    m_helpServer->setDarkTheme(darkTheme);

    if (!m_helpServer->start(docSiteRoot()))
        return {};

    return QUrl(QStringLiteral("http://127.0.0.1:%1/%2")
                    .arg(m_helpServer->port())
                    .arg(relative));
}

/*!
 * \brief Updates the documentation colour theme on the running help server.
 *
 * A no-op until the server exists (created on the first helpIndexUrl() call). An
 * open viewer reloads the current page to pick up the new stylesheet.
 */
void AppInfo::setHelpDarkTheme(bool darkTheme)
{
    if (m_helpServer)
        m_helpServer->setDarkTheme(darkTheme);
}

/*!
 * \brief Returns whether any offline documentation is installed next to the application.
 */
bool AppInfo::helpAvailable() const
{
    return !resolveDocRelativePath(QStringLiteral("en")).isEmpty();
}
