// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HELPSERVER_H
#define HELPSERVER_H

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QTcpServer;
class QTcpSocket;
QT_END_NAMESPACE

/**
 * Minimal read-only loopback HTTP server for the bundled offline documentation.
 *
 * The Windows WebView2 backend used by Qt WebView refuses to load top-level
 * file:// URLs, so the local documentation site is served over
 * http://127.0.0.1:<port>/ instead, which the same WebView loads like any other
 * web page (it also loads the online Qt reference in the same view).
 *
 * The server binds to the loopback interface only, answers GET requests for
 * files below a single root directory, and rejects path traversal outside it. It
 * is intended solely for serving the application's own bundled documentation.
 */
class HelpServer : public QObject
{
    Q_OBJECT
public:
    /** Creates an unstarted server. */
    explicit HelpServer(QObject *parent = nullptr);

    /** Destroys the server and stops listening. */
    ~HelpServer() override;

    /**
     * Starts serving files under \a rootDir on the loopback interface.
     * \param rootDir Absolute path of the directory to serve (the documentation site root).
     * \return true when the server is listening for that root (including when it was already running).
     */
    bool start(const QString &rootDir);

    /** Returns whether the server is currently listening. */
    bool isRunning() const;

    /** Returns the loopback port the server listens on, or 0 when not running. */
    quint16 port() const;

    /**
     * Selects the documentation colour theme served for HTML pages.
     * When \a dark is true, the light QDoc stylesheet link (offline-simple.css)
     * is rewritten to the dark one (offline-dark.css) on the fly, so the docs
     * follow the application theme. Applies to every page served afterwards.
     */
    void setDarkTheme(bool dark);

private:
    /** Accepts a pending connection and wires it for request handling. */
    void onNewConnection();

    /** Parses the request on \a socket and writes the response. */
    void serveRequest(QTcpSocket *socket);

    /** Non-owning listening server; parented to this. */
    QTcpServer *m_server {nullptr};

    /** Canonical root directory whose files are served. */
    QString m_root;

    /** Whether HTML pages are rewritten to the dark QDoc stylesheet. */
    bool m_darkTheme {false};
};

#endif // HELPSERVER_H
