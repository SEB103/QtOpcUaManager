#include "iprange.h"

#include <QCoreApplication>
#include <QNetworkInterface>
#include <QRegularExpression>

namespace {

/*!
 * \internal
 * \brief Parses \a text as a dotted-quad IPv4 address into \a value.
 *
 * Only the four-part form is accepted; QHostAddress alone would also accept
 * shortened forms such as "10.1".
 */
bool parseIpv4(const QString &text, quint32 *value)
{
    static const QRegularExpression dottedQuad(
        QStringLiteral("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$"));
    if (!dottedQuad.match(text).hasMatch())
        return false;
    const QHostAddress address(text);
    if (address.protocol() != QAbstractSocket::IPv4Protocol)
        return false;
    *value = address.toIPv4Address();
    return true;
}

/*!
 * \internal
 * \brief Returns the IPv4 network mask for \a prefixLength (0-32).
 */
quint32 maskFor(int prefixLength)
{
    return prefixLength == 0 ? 0u : ~quint32(0) << (32 - prefixLength);
}

/*!
 * \internal
 * \brief Returns the addresses from \a first to \a last inclusive.
 *
 * The count is checked before any address is created, so a /0 subnet costs
 * nothing and yields the limit error.
 */
IpRange::AddressList expand(quint32 first, quint32 last)
{
    IpRange::AddressList result;
    const quint64 count = quint64(last) - quint64(first) + 1;
    if (count > quint64(IpRange::kMaxAddresses)) {
        result.error = QCoreApplication::translate(
                           "IpRange", "The range contains %1 addresses; the maximum is %2.")
                           .arg(count)
                           .arg(IpRange::kMaxAddresses);
        return result;
    }
    result.addresses.reserve(qsizetype(count));
    for (quint64 value = first; value <= last; ++value)
        result.addresses << QHostAddress(quint32(value));
    return result;
}

} // namespace

namespace IpRange {

/*!
 * \brief Parses \a text as a CIDR subnet, an address range, or a single IPv4 address.
 * \param text The user input, optionally followed by a label in parentheses.
 */
AddressList parseRange(const QString &text)
{
    static const QRegularExpression trailingLabel(QStringLiteral("\\s*\\(.*\\)\\s*$"));
    QString input = text;
    input.remove(trailingLabel);
    input.remove(QLatin1Char(' '));

    AddressList result;
    if (input.isEmpty()) {
        result.error = QCoreApplication::translate("IpRange",
                                                   "Enter an IPv4 range, e.g. 10.10.1.0/24.");
        return result;
    }
    const QString invalid =
        QCoreApplication::translate("IpRange", "Not a valid IPv4 range: %1").arg(text.trimmed());

    const qsizetype slash = input.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        quint32 base = 0;
        bool prefixOk = false;
        const int prefix = input.mid(slash + 1).toInt(&prefixOk);
        if (!parseIpv4(input.left(slash), &base) || !prefixOk || prefix < 0 || prefix > 32) {
            result.error = invalid;
            return result;
        }
        const quint32 network = base & maskFor(prefix);
        const quint32 broadcast = network | ~maskFor(prefix);
        // Network and broadcast addresses are not hosts for /30 and shorter.
        if (prefix <= 30)
            return expand(network + 1, broadcast - 1);
        return expand(network, broadcast);
    }

    const qsizetype dash = input.indexOf(QLatin1Char('-'));
    if (dash >= 0) {
        quint32 first = 0;
        if (!parseIpv4(input.left(dash), &first)) {
            result.error = invalid;
            return result;
        }
        const QString endText = input.mid(dash + 1);
        quint32 last = 0;
        bool octetOk = false;
        const uint octet = endText.toUInt(&octetOk);
        if (octetOk && !endText.contains(QLatin1Char('.'))) {
            // "10.10.1.1-254" ends at the given last octet of the same /24.
            if (octet > 255) {
                result.error = invalid;
                return result;
            }
            last = (first & 0xFFFFFF00u) | octet;
        } else if (!parseIpv4(endText, &last)) {
            result.error = invalid;
            return result;
        }
        if (last < first) {
            result.error = QCoreApplication::translate("IpRange",
                                                       "The range end is before its start.");
            return result;
        }
        return expand(first, last);
    }

    quint32 single = 0;
    if (!parseIpv4(input, &single)) {
        result.error = invalid;
        return result;
    }
    return expand(single, single);
}

/*!
 * \brief Parses \a text as comma-separated ports and port ranges.
 * \param text The user input, e.g. "4840, 48010-48012".
 */
PortList parsePorts(const QString &text)
{
    PortList result;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty())
            continue;

        const qsizetype dash = part.indexOf(QLatin1Char('-'));
        bool firstOk = false;
        bool lastOk = false;
        const uint first = (dash >= 0 ? part.left(dash) : part).trimmed().toUInt(&firstOk);
        const uint last = dash >= 0 ? part.mid(dash + 1).trimmed().toUInt(&lastOk) : first;
        if (dash < 0)
            lastOk = firstOk;
        if (!firstOk || !lastOk || first < 1 || last > 65535 || last < first) {
            result.ports.clear();
            result.error = QCoreApplication::translate("IpRange", "Not a valid port: %1").arg(part);
            return result;
        }
        for (uint port = first; port <= last; ++port) {
            if (!result.ports.contains(quint16(port)))
                result.ports << quint16(port);
        }
    }

    if (result.ports.isEmpty()) {
        result.error = QCoreApplication::translate("IpRange", "Enter at least one port.");
    } else if (result.ports.size() > kMaxPorts) {
        result.error = QCoreApplication::translate("IpRange",
                                                   "Too many ports (%1); the maximum is %2.")
                           .arg(result.ports.size())
                           .arg(kMaxPorts);
        result.ports.clear();
    }
    return result;
}

/*!
 * \brief Returns scan suggestions for \a entries.
 * \param entries Interface addresses, typically from localSubnets().
 */
QList<Subnet> subnetsFromInterfaces(const QList<InterfaceAddress> &entries)
{
    QList<Subnet> result;
    QStringList seen;
    for (const InterfaceAddress &entry : entries) {
        if (entry.address.protocol() != QAbstractSocket::IPv4Protocol
            || entry.address.isLoopback() || entry.address.isLinkLocal()
            || entry.prefixLength < 1 || entry.prefixLength > 32) {
            continue;
        }
        // Wider networks are narrowed to the PC's own /24 to respect the scan limit.
        const int prefix = qMax(entry.prefixLength, 24);
        const quint32 network = entry.address.toIPv4Address() & maskFor(prefix);
        const QString cidr = QStringLiteral("%1/%2").arg(QHostAddress(network).toString()).arg(prefix);
        if (seen.contains(cidr))
            continue;
        seen << cidr;
        result.append({cidr, QStringLiteral("%1 (%2)").arg(cidr, entry.interfaceName)});
    }
    return result;
}

/*!
 * \brief Returns scan suggestions for the PC's running network interfaces.
 */
QList<Subnet> localSubnets()
{
    QList<InterfaceAddress> entries;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &networkInterface : interfaces) {
        const auto flags = networkInterface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning))
            continue;
        const QList<QNetworkAddressEntry> addressEntries = networkInterface.addressEntries();
        for (const QNetworkAddressEntry &entry : addressEntries)
            entries.append({networkInterface.humanReadableName(), entry.ip(), entry.prefixLength()});
    }
    return subnetsFromInterfaces(entries);
}

} // namespace IpRange
