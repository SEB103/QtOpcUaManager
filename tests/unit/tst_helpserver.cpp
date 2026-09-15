// SPDX-FileCopyrightText: Copyright (C) 2025-2026 OpcUaManager Project
// SPDX-License-Identifier: GPL-3.0-or-later

/*!
 * \file
 * \brief Unit tests for the loopback documentation HTTP server (HelpServer).
 */

#include "helpserver.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

/*!
 * \internal
 * \brief One HTTP response split into its status line, content type and body.
 */
struct HttpReply
{
    QByteArray statusLine;
    QByteArray contentType;
    QByteArray body;
};

/*!
 * \internal
 * \brief Verifies that HelpServer serves bundled files and rejects escapes.
 */
class TstHelpServer : public QObject
{
    Q_OBJECT

private:
    /*! \internal Writes \a content to \a path, creating parent directories. */
    static void writeFile(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    /*! \internal Performs a raw GET for \a target against the loopback \a port.
     *
     * The event loop is pumped with processEvents() rather than the socket's
     * blocking waitFor*() calls, because the server runs in this same thread and
     * its readyRead slot would not be dispatched while a blocking wait holds the
     * thread. */
    static HttpReply httpGet(quint16 port, const QByteArray &target)
    {
        HttpReply reply;
        QByteArray raw;
        bool finished = false;

        QTcpSocket socket;
        QObject::connect(&socket, &QTcpSocket::readyRead, [&]() { raw += socket.readAll(); });
        QObject::connect(&socket, &QTcpSocket::disconnected, [&]() { finished = true; });
        QObject::connect(&socket, &QTcpSocket::connected, [&]() {
            socket.write("GET " + target + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        });

        socket.connectToHost(QHostAddress::LocalHost, port);

        QElapsedTimer timer;
        timer.start();
        while (!finished && timer.elapsed() < 3000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        raw += socket.readAll();

        const int headerEnd = raw.indexOf("\r\n\r\n");
        const QByteArray head = headerEnd >= 0 ? raw.left(headerEnd) : raw;
        reply.body = headerEnd >= 0 ? raw.mid(headerEnd + 4) : QByteArray();

        const QList<QByteArray> lines = head.split('\n');
        if (!lines.isEmpty())
            reply.statusLine = lines.first().trimmed();
        for (const QByteArray &line : lines) {
            if (line.toLower().startsWith("content-type:"))
                reply.contentType = line.mid(line.indexOf(':') + 1).trimmed();
        }
        return reply;
    }

private slots:
    /*! Serves files, maps directories to index.html, sets types, and blocks traversal. */
    void servesAndGuards()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("site"));
        writeFile(root + QStringLiteral("/en/index.html"), "<html>hello-en</html>");
        writeFile(root + QStringLiteral("/en/style.css"), "body{color:red}");
        writeFile(root + QStringLiteral("/index.html"), "<html>chooser</html>");
        writeFile(tmp.filePath(QStringLiteral("secret.txt")), "TOPSECRET");

        HelpServer server;
        QVERIFY(server.start(root));
        QVERIFY(server.isRunning());
        const quint16 port = server.port();
        QVERIFY(port != 0);

        // A file is served with its HTML content type.
        const HttpReply page = httpGet(port, "/en/index.html");
        QVERIFY2(page.statusLine.contains("200"), page.statusLine.constData());
        QVERIFY(page.contentType.contains("text/html"));
        QVERIFY(page.body.contains("hello-en"));

        // CSS gets the right content type.
        const HttpReply css = httpGet(port, "/en/style.css");
        QVERIFY(css.statusLine.contains("200"));
        QVERIFY(css.contentType.contains("text/css"));

        // A directory maps to its index.html.
        const HttpReply dir = httpGet(port, "/en/");
        QVERIFY(dir.statusLine.contains("200"));
        QVERIFY(dir.body.contains("hello-en"));

        // The root maps to the top-level index.html.
        const HttpReply rootIndex = httpGet(port, "/");
        QVERIFY(rootIndex.statusLine.contains("200"));
        QVERIFY(rootIndex.body.contains("chooser"));

        // A missing file is a 404.
        const HttpReply missing = httpGet(port, "/en/nope.html");
        QVERIFY(missing.statusLine.contains("404"));

        // Path traversal outside the root is rejected and never leaks the file.
        const HttpReply escape = httpGet(port, "/../secret.txt");
        QVERIFY(!escape.body.contains("TOPSECRET"));
        QVERIFY(escape.statusLine.contains("404"));
    }

    /*! Dark theme rewrites both the link and the JS-swapped stylesheet to the dark one. */
    void darkThemeRewritesStylesheet()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("site"));
        // Mirror the Qt offline template: a light link plus a script that swaps it
        // to the full light stylesheet at runtime. Both must become dark.
        writeFile(root + QStringLiteral("/index.html"),
                  "<html><head>"
                  "<link rel=\"stylesheet\" href=\"style/offline-simple.css\">"
                  "<script>document.getElementsByTagName(\"link\").item(0)"
                  ".setAttribute(\"href\", \"style/offline.css\");</script>"
                  "</head></html>");

        HelpServer server;
        QVERIFY(server.start(root));

        // Light (default): both light stylesheets are kept, with the accent override.
        const HttpReply light = httpGet(server.port(), "/index.html");
        QVERIFY(light.body.contains("style/offline-simple.css"));
        QVERIFY(light.body.contains("style/offline.css"));
        QVERIFY(!light.body.contains("offline-dark.css"));
        QVERIFY(light.body.contains("#00695c")); // project Teal accent (light shade)

        // Dark: the link AND the script target are rewritten to the dark stylesheet,
        // so the runtime script cannot put the light theme back.
        server.setDarkTheme(true);
        const HttpReply dark = httpGet(server.port(), "/index.html");
        QVERIFY(dark.body.contains("style/offline-dark.css"));
        QVERIFY(!dark.body.contains("offline-simple.css"));
        QVERIFY(!dark.body.contains("\"style/offline.css\""));
        QVERIFY(dark.body.contains("#4db6ac")); // project Teal accent (dark shade)
    }

    /*! start() is idempotent for the same root and reports a stable port. */
    void startIsIdempotent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.filePath(QStringLiteral("site"));
        writeFile(root + QStringLiteral("/index.html"), "<html>x</html>");

        HelpServer server;
        QVERIFY(server.start(root));
        const quint16 first = server.port();
        QVERIFY(server.start(root));
        QCOMPARE(server.port(), first);
    }
};

QTEST_MAIN(TstHelpServer)
#include "tst_helpserver.moc"
