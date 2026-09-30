#ifndef OPCUAENDPOINTADDRESS_H
#define OPCUAENDPOINTADDRESS_H

#include <QList>
#include <QOpcUaEndpointDescription>
#include <QString>
#include <QUrl>

/**
 * Keeps OPC UA requests on the host that actually answered.
 *
 * Servers advertise their own discovery and endpoint URLs, typically with their
 * own host name (for example \c opc.tcp://AOPT690:4840). A client on another
 * machine often cannot resolve that name, so following the advertised URL turns
 * a reachable server into an empty endpoint list. These helpers redirect the
 * advertised URLs to the host the client reached, keeping port and path.
 */
namespace OpcUaEndpointAddress {

/**
 * Returns \a advertised with its host replaced by the host of \a reached.
 *
 * Port and path of \a advertised are kept. Returns \a advertised unchanged when
 * the hosts already match (case-insensitively) or \a reached has no host, and
 * \a reached when \a advertised has no host.
 */
QUrl reachableUrl(const QUrl &advertised, const QUrl &reached);

/**
 * Returns \a endpoints redirected to the host of \a reached, in their original order.
 *
 * Endpoints that become identical after the redirect (same URL, security policy,
 * security mode, and user token types) are reduced to their first occurrence.
 */
QList<QOpcUaEndpointDescription> reachableEndpoints(
    const QList<QOpcUaEndpointDescription> &endpoints,
    const QUrl &reached);

/**
 * Returns whether \a host is a name that needs DNS resolution.
 *
 * IP address literals, \c localhost, and empty strings return false.
 */
bool isHostName(const QString &host);

} // namespace OpcUaEndpointAddress

#endif // OPCUAENDPOINTADDRESS_H
