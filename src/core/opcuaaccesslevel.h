#ifndef OPCUAACCESSLEVEL_H
#define OPCUAACCESSLEVEL_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

/**
 * Interpretation of the OPC UA AccessLevel bit mask (OPC UA part 3).
 *
 * The mask decides whether a write can succeed at all, so the Data Access View
 * consults it before offering the value editor rather than letting the server
 * reject the write afterwards.
 */
namespace OpcUaAccessLevel {

/** Bits of the AccessLevel and UserAccessLevel attributes. */
enum Bit {
    /** The current value can be read. */
    CurrentRead = 0x01,
    /** The current value can be written. */
    CurrentWrite = 0x02,
    /** History can be read. */
    HistoryRead = 0x04,
    /** History can be written. */
    HistoryWrite = 0x08
};

/** Sentinel used while the attribute has not been reported by the server. */
inline constexpr int Unknown = -1;

/** Returns whether \a accessLevel was reported by the server. */
inline bool isKnown(int accessLevel)
{
    return accessLevel >= 0;
}

/** Returns whether \a accessLevel permits writing the current value. */
inline bool allowsWrite(int accessLevel)
{
    return isKnown(accessLevel) && (accessLevel & CurrentWrite) != 0;
}

/**
 * Returns a readable description of \a accessLevel, for example
 * "CurrentRead | CurrentWrite (3)", or an empty string when it is unknown.
 */
inline QString toString(int accessLevel)
{
    if (!isKnown(accessLevel))
        return {};

    QStringList flags;
    if (accessLevel & CurrentRead)
        flags.append(QStringLiteral("CurrentRead"));
    if (accessLevel & CurrentWrite)
        flags.append(QStringLiteral("CurrentWrite"));
    if (accessLevel & HistoryRead)
        flags.append(QStringLiteral("HistoryRead"));
    if (accessLevel & HistoryWrite)
        flags.append(QStringLiteral("HistoryWrite"));

    if (flags.isEmpty()) {
        return QCoreApplication::translate("OpcUaAccessLevel", "No access (%1)")
            .arg(accessLevel);
    }

    return QStringLiteral("%1 (%2)").arg(flags.join(QStringLiteral(" | ")))
        .arg(accessLevel);
}

} // namespace OpcUaAccessLevel

#endif // OPCUAACCESSLEVEL_H
