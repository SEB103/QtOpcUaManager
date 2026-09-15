// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

/*!
 * \file
 * \brief Loopback HTTP server for the bundled offline documentation.
 */

#include "helpserver.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace {

/*!
 * \internal
 * \brief Returns the MIME type for the file name \a path by extension.
 *
 * Covers the file types QDoc emits (HTML, CSS, JS, SVG, images, fonts); anything
 * else falls back to a generic binary type.
 */
QByteArray contentTypeFor(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        return "text/html; charset=utf-8";
    if (suffix == QLatin1String("css"))
        return "text/css; charset=utf-8";
    if (suffix == QLatin1String("js") || suffix == QLatin1String("mjs"))
        return "application/javascript; charset=utf-8";
    if (suffix == QLatin1String("json"))
        return "application/json; charset=utf-8";
    if (suffix == QLatin1String("svg"))
        return "image/svg+xml";
    if (suffix == QLatin1String("png"))
        return "image/png";
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return "image/jpeg";
    if (suffix == QLatin1String("gif"))
        return "image/gif";
    if (suffix == QLatin1String("ico"))
        return "image/x-icon";
    if (suffix == QLatin1String("woff2"))
        return "font/woff2";
    if (suffix == QLatin1String("woff"))
        return "font/woff";
    if (suffix == QLatin1String("ttf"))
        return "font/ttf";
    if (suffix == QLatin1String("txt"))
        return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

/*!
 * \internal
 * \brief Writes a minimal HTTP response with \a status, \a contentType and \a body, then closes.
 */
void writeResponse(QTcpSocket *socket,
                   const QByteArray &status,
                   const QByteArray &contentType,
                   const QByteArray &body)
{
    QByteArray header;
    header += "HTTP/1.1 " + status + "\r\n";
    header += "Content-Type: " + contentType + "\r\n";
    header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    // Never let the WebView cache a page: the same URL is re-served with the
    // dark or light stylesheet depending on the current application theme.
    header += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    header += "Pragma: no-cache\r\n";
    header += "Expires: 0\r\n";
    header += "Connection: close\r\n\r\n";
    socket->write(header);
    socket->write(body);
    socket->disconnectFromHost();
}

} // namespace

/*!
 * \class HelpServer
 * \inmodule OpcUaManager
 * \brief Read-only loopback HTTP server for the bundled offline documentation.
 */

/*!
 * \brief Creates an unstarted server.
 */
HelpServer::HelpServer(QObject *parent)
    : QObject(parent)
{}

HelpServer::~HelpServer() = default;

/*!
 * \brief Starts serving files under \a rootDir on the loopback interface.
 *
 * A no-op that returns true when already listening for the same root. The server
 * listens on an OS-assigned port of QHostAddress::LocalHost, so it is reachable
 * only from this machine.
 */
bool HelpServer::start(const QString &rootDir)
{
    const QString canonicalRoot = QFileInfo(rootDir).canonicalFilePath();
    if (canonicalRoot.isEmpty())
        return false;

    if (isRunning() && m_root == canonicalRoot)
        return true;

    if (!m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &HelpServer::onNewConnection);
    }

    m_root = canonicalRoot;

    if (m_server->isListening())
        return true;

    return m_server->listen(QHostAddress::LocalHost);
}

/*!
 * \brief Returns whether the server is currently listening.
 */
bool HelpServer::isRunning() const
{
    return m_server && m_server->isListening();
}

/*!
 * \brief Returns the loopback port the server listens on, or 0 when not running.
 */
quint16 HelpServer::port() const
{
    return isRunning() ? m_server->serverPort() : quint16(0);
}

/*!
 * \brief Selects the documentation colour theme served for HTML pages.
 */
void HelpServer::setDarkTheme(bool dark)
{
    m_darkTheme = dark;
}

/*!
 * \brief Accepts a pending connection and serves its request when the headers arrive.
 */
void HelpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            // The request line is all that is needed and always arrives in the
            // first packet on the loopback interface; wait for the header terminator.
            if (!socket->peek(socket->bytesAvailable()).contains("\r\n\r\n")
                && socket->bytesAvailable() < 8192) {
                return;
            }
            serveRequest(socket);
        });
    }
}

/*!
 * \brief Parses the GET request on \a socket and writes the requested file.
 *
 * Only GET is supported. The request target is percent-decoded, stripped of any
 * query/fragment, resolved against the served root, and rejected when it escapes
 * the root (path traversal). A directory maps to its \c index.html.
 */
void HelpServer::serveRequest(QTcpSocket *socket)
{
    const QByteArray request = socket->readAll();
    const int lineEnd = request.indexOf("\r\n");
    const QByteArray requestLine = lineEnd >= 0 ? request.left(lineEnd) : request;
    const QList<QByteArray> parts = requestLine.split(' ');

    if (parts.size() < 2 || parts.at(0) != "GET") {
        writeResponse(socket, "405 Method Not Allowed", "text/plain; charset=utf-8",
                      "Only GET is supported.");
        return;
    }

    // Decode the target and drop any query string or fragment.
    QByteArray target = parts.at(1);
    const int cut = target.indexOf('?');
    if (cut >= 0)
        target = target.left(cut);
    const int hash = target.indexOf('#');
    if (hash >= 0)
        target = target.left(hash);
    QString path = QString::fromUtf8(QByteArray::fromPercentEncoding(target));

    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    if (path.isEmpty())
        path = QStringLiteral("index.html");

    QString fullPath = QDir(m_root).filePath(path);
    if (QFileInfo(fullPath).isDir())
        fullPath = QDir(fullPath).filePath(QStringLiteral("index.html"));

    // Reject anything that resolves outside the served root.
    const QString canonical = QFileInfo(fullPath).canonicalFilePath();
    const QString rootWithSep = m_root.endsWith(QLatin1Char('/')) ? m_root : m_root + QLatin1Char('/');
    if (canonical.isEmpty() || (canonical != m_root && !canonical.startsWith(rootWithSep))) {
        writeResponse(socket, "404 Not Found", "text/plain; charset=utf-8", "Not found.");
        return;
    }

    QFile file(canonical);
    if (!file.open(QIODevice::ReadOnly)) {
        writeResponse(socket, "404 Not Found", "text/plain; charset=utf-8", "Not found.");
        return;
    }

    const QByteArray type = contentTypeFor(canonical);
    QByteArray body = file.readAll();

    if (type.startsWith("text/html")) {
        // Follow the application theme. The Qt offline template links the light
        // stylesheet (offline-simple.css) and then a small inline script swaps it
        // to the full light stylesheet (offline.css) at runtime, so both references
        // must be rewritten to the dark one, otherwise the script would put the
        // light theme back.
        if (m_darkTheme) {
            body.replace("style/offline-simple.css", "style/offline-dark.css");
            body.replace("style/offline.css", "style/offline-dark.css");
        }

        // Recolour the Qt-green links and breadcrumb to the application accent
        // (Material Teal), so the docs match this application rather than looking
        // like generic Qt help. Injected after the template stylesheet so it wins;
        // the shade is theme-aware for readable contrast.
        const QByteArray link = m_darkTheme ? "#4db6ac" : "#00695c";
        const QByteArray hover = m_darkTheme ? "#80cbc4" : "#00897b";
        const QByteArray accent =
            "<style>\n"
            "a:link,a:visited{color:" + link + " !important}\n"
            "a:hover,a:visited:hover{color:" + hover + " !important}\n"
            ".navigationbar a,.navigationbar td a,#buildversion a{color:" + link + " !important}\n"
            "</style>\n";
        const int headEnd = body.indexOf("</head>");
        if (headEnd >= 0)
            body.insert(headEnd, accent);
    }

    writeResponse(socket, "200 OK", type, body);
}
