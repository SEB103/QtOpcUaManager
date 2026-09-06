#ifndef OPCUASTATUSHINT_H
#define OPCUASTATUSHINT_H

#include <QString>

/**
 * Plain-language hints for the OPC UA status codes this application produces.
 *
 * A raw status name such as \c BadUserAccessDenied says nothing to a
 * commissioning engineer, and the resulting guesswork is the most-cited
 * weakness of OPC UA clients. The helpers below turn the codes that actually
 * occur in this application's connect and write paths into one short sentence
 * that names the likely cause.
 */
namespace OpcUaStatusHint {

/**
 * Returns a short explanation for the OPC UA status \a statusText, or an empty
 * string when the code is unknown or needs no explanation.
 *
 * \a statusText is matched against the status name produced by
 * QOpcUa::statusToString(); a longer message containing the name is matched as
 * well, so a backend error string can be passed unchanged.
 */
QString hintForStatus(const QString &statusText);

/**
 * Returns \a message with the hint for its status code appended in parentheses,
 * or \a message unchanged when no hint applies.
 */
QString describe(const QString &message);

} // namespace OpcUaStatusHint

#endif // OPCUASTATUSHINT_H
