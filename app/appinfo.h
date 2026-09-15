// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef APPINFO_H
#define APPINFO_H

#include <QObject>
#include <QString>
#include <QUrl>

class HelpServer;

/**
 * Read-only application, build, and environment metadata exposed to QML.
 *
 * The About dialog binds to these properties to show the application version,
 * license, and the versions of the toolchain and third-party components the
 * application was built against. readText() loads a bundled UTF-8 document (a
 * license or notice file) for display. All properties are constant for the
 * lifetime of the process.
 */
class AppInfo : public QObject
{
    Q_OBJECT

    /** Human-readable application name. */
    Q_PROPERTY(QString appName READ appName CONSTANT)
    /** Application version string. */
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    /** Organization the application reports itself under. */
    Q_PROPERTY(QString organization READ organization CONSTANT)
    /** Copyright line for the application's own source code. */
    Q_PROPERTY(QString copyright READ copyright CONSTANT)
    /** SPDX identifier of the application's own license. */
    Q_PROPERTY(QString licenseName READ licenseName CONSTANT)
    /** One-line description of the application. */
    Q_PROPERTY(QString description READ description CONSTANT)
    /** Project homepage URL. */
    Q_PROPERTY(QString homepageUrl READ homepageUrl CONSTANT)
    /** Qt runtime version the application is linked against. */
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    /** Version of open62541 bundled by the Qt OPC UA backend. */
    Q_PROPERTY(QString open62541Version READ open62541Version CONSTANT)
    /** Compiler and version the application was built with. */
    Q_PROPERTY(QString compiler READ compiler CONSTANT)
    /** C++ language standard the application was built with. */
    Q_PROPERTY(QString cxxStandard READ cxxStandard CONSTANT)
    /** Build configuration, "Debug" or "Release". */
    Q_PROPERTY(QString buildType READ buildType CONSTANT)
    /** Compile date and time of this build. */
    Q_PROPERTY(QString buildTimestamp READ buildTimestamp CONSTANT)
    /** Pretty name of the operating system the application runs on. */
    Q_PROPERTY(QString operatingSystem READ operatingSystem CONSTANT)
    /** CPU architecture the application runs on. */
    Q_PROPERTY(QString cpuArchitecture READ cpuArchitecture CONSTANT)
    /** Resource root holding the bundled license and notice documents. */
    Q_PROPERTY(QString licensesRoot READ licensesRoot CONSTANT)

public:
    /** Creates the metadata object. */
    explicit AppInfo(QObject *parent = nullptr);

    /** Returns the human-readable application name. */
    QString appName() const;
    /** Returns the application version string. */
    QString appVersion() const;
    /** Returns the organization the application reports itself under. */
    QString organization() const;
    /** Returns the copyright line for the application's own source code. */
    QString copyright() const;
    /** Returns the SPDX identifier of the application's own license. */
    QString licenseName() const;
    /** Returns a one-line description of the application. */
    QString description() const;
    /** Returns the project homepage URL. */
    QString homepageUrl() const;
    /** Returns the Qt runtime version the application is linked against. */
    QString qtVersion() const;
    /** Returns the version of open62541 bundled by the Qt OPC UA backend. */
    QString open62541Version() const;
    /** Returns the compiler and version the application was built with. */
    QString compiler() const;
    /** Returns the C++ language standard the application was built with. */
    QString cxxStandard() const;
    /** Returns the build configuration, "Debug" or "Release". */
    QString buildType() const;
    /** Returns the compile date and time of this build. */
    QString buildTimestamp() const;
    /** Returns the pretty name of the operating system. */
    QString operatingSystem() const;
    /** Returns the CPU architecture the application runs on. */
    QString cpuArchitecture() const;
    /** Returns the resource root holding the bundled license documents. */
    QString licensesRoot() const;

    /**
     * Reads the UTF-8 text file at \a path for display in the About dialog.
     * \param path Filesystem path or "qrc:/" resource path.
     * \return The file contents, or an empty string when the file cannot be read.
     */
    Q_INVOKABLE QString readText(const QString &path) const;

    /**
     * Returns a loopback http:// URL to the offline documentation for a UI language.
     * The bundled documentation site (<app>/doc/site) is served over a local
     * HTTP server because the WebView2 backend does not load file:// URLs; the
     * requested language is preferred, then English, then the language chooser.
     * Starts the server lazily on first use.
     * \param language UI locale code such as "de_DE"; only the language part is used.
     * \param darkTheme When true, the documentation is served with the dark stylesheet so it matches the application theme.
     * \return An http://127.0.0.1 URL, or an empty/invalid URL when no documentation is installed.
     */
    Q_INVOKABLE QUrl helpIndexUrl(const QString &language, bool darkTheme);

    /**
     * Updates the documentation colour theme on the running help server so an
     * already-open viewer can reload into the matching (dark/light) stylesheet.
     * \param darkTheme Whether the dark documentation stylesheet is served.
     */
    Q_INVOKABLE void setHelpDarkTheme(bool darkTheme);

    /** Returns whether any offline documentation is installed next to the application. */
    Q_INVOKABLE bool helpAvailable() const;

private:
    /** Absolute path of the bundled documentation site root (<app>/doc/site). */
    QString docSiteRoot() const;

    /** Returns the site-relative index path for \a language (language, then English, then root), or empty when none exists. */
    QString resolveDocRelativePath(const QString &language) const;

    /** Loopback HTTP server for the documentation; created and started on first use. */
    HelpServer *m_helpServer {nullptr};
};

#endif // APPINFO_H
