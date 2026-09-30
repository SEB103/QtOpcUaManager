#ifndef NETWORKSCANCONTROLLER_H
#define NETWORKSCANCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "models/networkscanmodel.h"

class NetworkScanner;

/**
 * QML facade of the network scanner, published as \c cppNetworkScanner.
 *
 * Validates the dialog input, starts and cancels scans, feeds the result model,
 * and persists the last range and ports in QSettings.
 */
class NetworkScanController : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NetworkScanController)

    /** Result rows of the current or last scan; owned by this controller. */
    Q_PROPERTY(NetworkScanModel *model READ model CONSTANT)
    /** Scan state: 0 idle, 1 scanning, 2 finished, 3 cancelled. */
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    /** Number of address/port pairs of the current scan. */
    Q_PROPERTY(int totalTargets READ totalTargets NOTIFY progressChanged)
    /** Number of pairs whose TCP probe has completed. */
    Q_PROPERTY(int probedTargets READ probedTargets NOTIFY progressChanged)
    /** Number of pairs that accepted a TCP connection. */
    Q_PROPERTY(int openPorts READ openPorts NOTIFY progressChanged)
    /** Number of pairs where an OPC UA server answered. */
    Q_PROPERTY(int opcUaServers READ opcUaServers NOTIFY progressChanged)
    /** Scan duration in milliseconds. */
    Q_PROPERTY(int elapsedMs READ elapsedMs NOTIFY progressChanged)
    /** Range of the last started scan; empty before the first scan. */
    Q_PROPERTY(QString lastRange READ lastRange NOTIFY lastInputChanged)
    /** Ports of the last started scan; "4840" before the first scan. */
    Q_PROPERTY(QString lastPorts READ lastPorts NOTIFY lastInputChanged)

public:
    /** Creates the controller, its scanner, and its model, owned by \a parent. */
    explicit NetworkScanController(QObject *parent = nullptr);

    /** Returns the result model. */
    NetworkScanModel *model() const;
    /** Returns the scan state as an integer. */
    int state() const;
    /** Returns the number of address/port pairs. */
    int totalTargets() const;
    /** Returns the number of completed probes. */
    int probedTargets() const;
    /** Returns the number of open ports. */
    int openPorts() const;
    /** Returns the number of OPC UA servers found. */
    int opcUaServers() const;
    /** Returns the scan duration in milliseconds. */
    int elapsedMs() const;
    /** Returns the persisted range. */
    QString lastRange() const;
    /** Returns the persisted ports. */
    QString lastPorts() const;

    /** Returns labels of the PC's IPv4 subnets, e.g. "10.10.1.0/24 (Ethernet 3)". */
    Q_INVOKABLE QStringList localSubnets() const;
    /** Returns an error text for the range \a text, or an empty string when valid. */
    Q_INVOKABLE QString validateRange(const QString &text) const;
    /** Returns an error text for the ports \a text, or an empty string when valid. */
    Q_INVOKABLE QString validatePorts(const QString &text) const;
    /** Starts a scan of \a range on \a ports; returns false without side effects when invalid. */
    Q_INVOKABLE bool start(const QString &range, const QString &ports);
    /** Stops the running scan. */
    Q_INVOKABLE void cancel();

signals:
    /** Emitted when the scan state changes. */
    void stateChanged();
    /** Emitted when any progress counter changes. */
    void progressChanged();
    /** Emitted when the persisted range or ports change. */
    void lastInputChanged();

private:
    /** Scan engine; owned by this controller. */
    NetworkScanner *m_scanner = nullptr;
    /** Result rows; owned by this controller. */
    NetworkScanModel *m_model = nullptr;
};

#endif // NETWORKSCANCONTROLLER_H
