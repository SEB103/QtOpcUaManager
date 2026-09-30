#include "networkscancontroller.h"

#include "core/iprange.h"
#include "core/networkscanner.h"
#include "models/networkscanmodel.h"

#include <QSettings>

namespace {

/*!
 * \internal
 * \brief QSettings key storing the range of the last started scan.
 */
constexpr auto kLastRangeSettingsKey = "scanner/lastRange";

/*!
 * \internal
 * \brief QSettings key storing the ports of the last started scan.
 */
constexpr auto kPortsSettingsKey = "scanner/ports";

} // namespace

/*!
 * \brief Creates the controller, its scanner, and its model, owned by \a parent.
 */
NetworkScanController::NetworkScanController(QObject *parent)
    : QObject(parent)
    , m_scanner(new NetworkScanner(this))
    , m_model(new NetworkScanModel(this))
{
    connect(m_scanner, &NetworkScanner::resultChanged, m_model, &NetworkScanModel::upsert);
    connect(m_scanner, &NetworkScanner::stateChanged, this, &NetworkScanController::stateChanged);
    connect(m_scanner, &NetworkScanner::progressChanged, this,
            &NetworkScanController::progressChanged);
}

NetworkScanModel *NetworkScanController::model() const
{
    return m_model;
}

int NetworkScanController::state() const
{
    return int(m_scanner->state());
}

int NetworkScanController::totalTargets() const
{
    return m_scanner->totalTargets();
}

int NetworkScanController::probedTargets() const
{
    return m_scanner->probedTargets();
}

int NetworkScanController::openPorts() const
{
    return m_scanner->openPorts();
}

int NetworkScanController::opcUaServers() const
{
    return m_scanner->opcUaServers();
}

int NetworkScanController::elapsedMs() const
{
    return int(m_scanner->elapsedMs());
}

QString NetworkScanController::lastRange() const
{
    return QSettings().value(QLatin1String(kLastRangeSettingsKey)).toString();
}

QString NetworkScanController::lastPorts() const
{
    return QSettings().value(QLatin1String(kPortsSettingsKey), QStringLiteral("4840")).toString();
}

/*!
 * \brief Returns labels of the PC's IPv4 subnets for the range selector.
 */
QStringList NetworkScanController::localSubnets() const
{
    QStringList labels;
    const QList<IpRange::Subnet> subnets = IpRange::localSubnets();
    for (const IpRange::Subnet &subnet : subnets)
        labels << subnet.label;
    return labels;
}

/*!
 * \brief Returns an error text for the range \a text, or an empty string when valid.
 */
QString NetworkScanController::validateRange(const QString &text) const
{
    return IpRange::parseRange(text).error;
}

/*!
 * \brief Returns an error text for the ports \a text, or an empty string when valid.
 */
QString NetworkScanController::validatePorts(const QString &text) const
{
    return IpRange::parsePorts(text).error;
}

/*!
 * \brief Starts a scan of \a range on \a ports.
 * \param range Range input as accepted by IpRange::parseRange().
 * \param ports Port input as accepted by IpRange::parsePorts().
 *
 * The model is cleared before the scan starts. The input is persisted only
 * when it is valid.
 */
bool NetworkScanController::start(const QString &range, const QString &ports)
{
    const IpRange::AddressList addresses = IpRange::parseRange(range);
    const IpRange::PortList portList = IpRange::parsePorts(ports);
    if (!addresses.error.isEmpty() || !portList.error.isEmpty())
        return false;

    QSettings settings;
    settings.setValue(QLatin1String(kLastRangeSettingsKey), range.trimmed());
    settings.setValue(QLatin1String(kPortsSettingsKey), ports.trimmed());
    emit lastInputChanged();

    m_model->clear();
    m_scanner->start(addresses.addresses, portList.ports);
    return true;
}

/*!
 * \brief Stops the running scan.
 */
void NetworkScanController::cancel()
{
    m_scanner->cancel();
}
