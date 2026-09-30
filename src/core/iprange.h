#ifndef IPRANGE_H
#define IPRANGE_H

#include <QHostAddress>
#include <QList>
#include <QString>

/**
 * Parsing and subnet helpers for the network scanner.
 *
 * All functions are pure except localSubnets(), which reads the PC's network
 * interfaces. Error texts are translated and meant for display.
 */
namespace IpRange {

/** Maximum number of addresses one scan may cover. */
inline constexpr int kMaxAddresses = 1024;

/** Maximum number of ports one scan may cover. */
inline constexpr int kMaxPorts = 16;

/** One IPv4 address assigned to a network interface. */
struct InterfaceAddress
{
    /** Human-readable interface name, e.g. "Ethernet 3". */
    QString interfaceName;
    /** Address assigned to the interface. */
    QHostAddress address;
    /** Network prefix length; -1 when unknown. */
    int prefixLength = -1;
};

/** A subnet suggested for scanning. */
struct Subnet
{
    /** Subnet in CIDR notation, e.g. "10.10.1.0/24". */
    QString cidr;
    /** Display text, e.g. "10.10.1.0/24 (Ethernet 3)". */
    QString label;
};

/** Result of parsing a range; \c error is empty on success. */
struct AddressList
{
    /** Addresses in ascending order. */
    QList<QHostAddress> addresses;
    /** Translated error text; empty when parsing succeeded. */
    QString error;
};

/** Result of parsing a port list; \c error is empty on success. */
struct PortList
{
    /** Ports in input order without duplicates. */
    QList<quint16> ports;
    /** Translated error text; empty when parsing succeeded. */
    QString error;
};

/**
 * Parses \a text as a CIDR subnet, an address range, or a single IPv4 address.
 *
 * Accepted forms: "10.10.1.0/24", "10.10.1.1-254", "10.10.1.5-10.10.1.40",
 * "10.10.1.2". A trailing label in parentheses, as produced by localSubnets(),
 * is ignored. Subnets with a prefix of /30 or shorter exclude their network
 * and broadcast addresses.
 */
AddressList parseRange(const QString &text);

/** Parses \a text as comma-separated ports and port ranges, e.g. "4840, 48010-48012". */
PortList parsePorts(const QString &text);

/**
 * Returns scan suggestions for \a entries.
 *
 * Skips non-IPv4, loopback, link-local, and prefix-less entries, narrows
 * prefixes shorter than /24 to the /24 containing the address, and removes
 * duplicate subnets (the first interface wins).
 */
QList<Subnet> subnetsFromInterfaces(const QList<InterfaceAddress> &entries);

/** Returns scan suggestions for the PC's running network interfaces. */
QList<Subnet> localSubnets();

} // namespace IpRange

#endif // IPRANGE_H
