#ifndef NETWORKSCANRESULT_H
#define NETWORKSCANRESULT_H

#include <QMetaType>
#include <QString>

/** Outcome of probing one address and port; the integer values are used by QML. */
enum class NetworkScanStatus {
    PortOpen = 0,       /**< The TCP port accepted a connection; OPC UA not queried yet. */
    OpcUa = 1,          /**< An OPC UA server answered FindServers. */
    NoOpcUaResponse = 2 /**< The port is open, but no OPC UA server answered. */
};

/** One row of a network scan: a reachable TCP port and what answered on it. */
struct NetworkScanResult
{
    /** IPv4 address in dotted notation. */
    QString address;
    /** TCP port that accepted the connection. */
    quint16 port = 0;
    /** TCP connect time in milliseconds; -1 when unknown. */
    int responseMs = -1;
    /** Probe outcome. */
    NetworkScanStatus status = NetworkScanStatus::PortOpen;
    /** Application name reported by FindServers. */
    QString applicationName;
    /** Application URI reported by FindServers. */
    QString applicationUri;
    /** Product URI reported by FindServers. */
    QString productUri;
    /** Distinct security policy names, e.g. "None, Basic256Sha256". */
    QString securitySummary;
    /** Distinct user token types, e.g. "Anonymous, Username". */
    QString authSummary;
    /** Status code name of a failed OPC UA request; empty on success. */
    QString errorText;

    /** Returns the opc.tcp discovery URL for this address and port. */
    QString url() const { return QStringLiteral("opc.tcp://%1:%2").arg(address).arg(port); }
};

Q_DECLARE_METATYPE(NetworkScanResult)

#endif // NETWORKSCANRESULT_H
