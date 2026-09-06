#include "opcuastatushint.h"

#include <QCoreApplication>
#include <QList>
#include <QPair>

namespace {

/*!
 * \internal
 * \brief Returns the status name to hint pairs, longest name first.
 *
 * The table is ordered so that a specific name is matched before a shorter name
 * it contains, which matters for the Bad* family where several codes share a
 * prefix. Only codes that this application can actually surface are listed; an
 * unknown code deliberately yields no hint rather than a guess.
 */
const QList<QPair<QString, QString>> &hintTable()
{
    static const QList<QPair<QString, QString>> table = {
        {QStringLiteral("BadCertificateUriInvalid"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the application URI in the client certificate does not match the one the "
             "client reports")},
        {QStringLiteral("BadCertificateTimeInvalid"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the certificate has expired or is not valid yet")},
        {QStringLiteral("BadCertificateUntrusted"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the certificate is not in the trust list; review it and copy it into the "
             "PKI trusted/certs directory")},
        {QStringLiteral("BadSecurityChecksFailed"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the server rejected the client certificate; it usually has to be moved "
             "from the server's rejected directory into its trusted list")},
        {QStringLiteral("BadIdentityTokenRejected"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the server rejected the supplied credentials")},
        {QStringLiteral("BadIdentityTokenInvalid"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the selected authentication mode is not accepted by this endpoint")},
        {QStringLiteral("BadUserAccessDenied"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the signed-in user is not allowed to perform this operation")},
        {QStringLiteral("BadNodeIdUnknown"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the server does not know this node id")},
        {QStringLiteral("BadNotWritable"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the node does not allow writing its value")},
        {QStringLiteral("BadWriteNotSupported"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the server does not support writing this value")},
        {QStringLiteral("BadTypeMismatch"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the written value does not match the data type of the node")},
        {QStringLiteral("BadOutOfRange"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the written value is outside the range the node accepts")},
        {QStringLiteral("BadNotConnected"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "there is no active session with the server")},
        {QStringLiteral("BadConnectionRejected"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the server refused the connection; check the endpoint URL, the port, and "
             "whether the server is running")},
        {QStringLiteral("BadTcpEndpointUrlInvalid"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the endpoint URL is malformed or names an unknown host")},
        {QStringLiteral("BadTimeout"),
         QCoreApplication::translate(
             "OpcUaStatusHint",
             "the server did not answer in time; check the network path and any firewall")},
        {QStringLiteral("BadSessionClosed"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the session was closed by the server")},
        {QStringLiteral("BadSessionIdInvalid"),
         QCoreApplication::translate("OpcUaStatusHint",
                                     "the session is no longer valid; reconnect to continue")}
    };
    return table;
}

} // namespace

/*!
 * \brief Returns a short explanation for the OPC UA status \a statusText.
 */
QString OpcUaStatusHint::hintForStatus(const QString &statusText)
{
    if (statusText.isEmpty())
        return {};

    for (const auto &entry : hintTable()) {
        if (statusText.contains(entry.first, Qt::CaseInsensitive))
            return entry.second;
    }

    return {};
}

/*!
 * \brief Returns \a message with its status-code hint appended.
 */
QString OpcUaStatusHint::describe(const QString &message)
{
    const QString hint = hintForStatus(message);
    if (hint.isEmpty())
        return message;

    return QCoreApplication::translate("OpcUaStatusHint", "%1 — %2").arg(message, hint);
}
