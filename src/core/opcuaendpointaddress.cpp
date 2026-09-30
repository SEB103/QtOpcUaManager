#include "opcuaendpointaddress.h"

#include <QHostAddress>
#include <QOpcUaUserTokenPolicy>
#include <QSet>

#include <algorithm>

namespace {

/*!
 * \internal
 * \brief Returns a key that identifies an endpoint by URL, security, and token types.
 * \param endpoint The endpoint to identify.
 *
 * Two endpoints with the same key offer the same connection to the client, so
 * only one of them has to be shown. Token types are sorted so that their order
 * in the server response does not matter.
 */
QString endpointKey(const QOpcUaEndpointDescription &endpoint)
{
    QList<int> tokenTypes;
    for (const auto &token : endpoint.userIdentityTokens())
        tokenTypes << int(token.tokenType());
    std::sort(tokenTypes.begin(), tokenTypes.end());

    QStringList tokenParts;
    for (const int type : std::as_const(tokenTypes))
        tokenParts << QString::number(type);

    return QStringLiteral("%1|%2|%3|%4")
        .arg(endpoint.endpointUrl().toLower(),
             endpoint.securityPolicy(),
             QString::number(int(endpoint.securityMode())),
             tokenParts.join(QLatin1Char(',')));
}

} // namespace

namespace OpcUaEndpointAddress {

/*!
 * \brief Normalizes \a hostOrUrl into an opc.tcp discovery URL.
 * \param hostOrUrl A host, an IP address, or a URL typed by the user.
 *
 * Missing schemes are treated as \c opc.tcp and missing ports default to
 * \c 4840, the standard OPC UA port.
 */
QUrl normalizeDiscoveryUrl(const QString &hostOrUrl)
{
    QString text = hostOrUrl.trimmed();
    if (text.isEmpty())
        return {};
    if (!text.contains(QLatin1String("://")))
        text.prepend(QLatin1String("opc.tcp://"));
    QUrl url(text);
    if (!url.isValid())
        return {};
    if (url.port() == -1)
        url.setPort(4840);
    return url;
}

/*!
 * \brief Returns \a advertised with its host replaced by the host of \a reached.
 * \param advertised The URL reported by the server in FindServers or GetEndpoints.
 * \param reached The URL the client successfully sent its request to.
 *
 * Only the host is replaced. The advertised port and path stay, because a
 * discovery server may list an application on another port of the same machine.
 */
QUrl reachableUrl(const QUrl &advertised, const QUrl &reached)
{
    if (!advertised.isValid() || advertised.host().isEmpty())
        return reached;
    if (!reached.isValid() || reached.host().isEmpty())
        return advertised;
    if (advertised.host().compare(reached.host(), Qt::CaseInsensitive) == 0)
        return advertised;

    QUrl redirected = advertised;
    redirected.setHost(reached.host());
    return redirected;
}

/*!
 * \brief Returns \a endpoints redirected to the host of \a reached.
 * \param endpoints The endpoints returned by GetEndpoints.
 * \param reached The URL the GetEndpoints request was sent to.
 *
 * Servers frequently offer the same endpoint once per network name (host name
 * and IP address). After the redirect these entries are identical, so only the
 * first one is kept to avoid listing the same connection twice.
 */
QList<QOpcUaEndpointDescription> reachableEndpoints(
    const QList<QOpcUaEndpointDescription> &endpoints,
    const QUrl &reached)
{
    QList<QOpcUaEndpointDescription> result;
    result.reserve(endpoints.size());
    QSet<QString> seenKeys;

    for (const auto &endpoint : endpoints) {
        QOpcUaEndpointDescription redirected = endpoint;
        const QUrl endpointUrl(endpoint.endpointUrl());
        if (endpointUrl.isValid() && !endpointUrl.host().isEmpty())
            redirected.setEndpointUrl(reachableUrl(endpointUrl, reached).toString());

        const QString key = endpointKey(redirected);
        if (seenKeys.contains(key))
            continue;
        seenKeys.insert(key);
        result << redirected;
    }
    return result;
}

/*!
 * \brief Returns whether \a host is a name that needs DNS resolution.
 * \param host The host part of a URL.
 */
bool isHostName(const QString &host)
{
    if (host.isEmpty())
        return false;
    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return false;
    return QHostAddress(host).isNull();
}

} // namespace OpcUaEndpointAddress
