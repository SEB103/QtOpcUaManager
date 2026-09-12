#include "serverprojectdata.h"

#include <QStringView>

namespace ServerProject {

/*!
 * \brief Parses a canonical node id "ns=<n>;s=<id>" or "ns=<n>;i=<num>".
 */
ParsedNodeId parseNodeId(const QString &nodeId)
{
    ParsedNodeId parsed;
    if (!nodeId.startsWith(QLatin1String("ns=")))
        return parsed;

    const int separator = nodeId.indexOf(QLatin1Char(';'));
    if (separator < 0)
        return parsed;

    bool nsOk = false;
    const uint ns = QStringView(nodeId).mid(3, separator - 3).toString().toUInt(&nsOk);
    if (!nsOk || ns > 65535)
        return parsed;

    const QString body = nodeId.mid(separator + 1);
    if (body.startsWith(QLatin1String("s="))) {
        parsed.numeric = false;
        parsed.identifier = body.mid(2);
        if (parsed.identifier.isEmpty())
            return parsed;
    } else if (body.startsWith(QLatin1String("i="))) {
        parsed.numeric = true;
        parsed.identifier = body.mid(2);
        bool idOk = false;
        parsed.identifier.toUInt(&idOk);
        if (!idOk || parsed.identifier.isEmpty())
            return parsed;
    } else {
        return parsed;
    }

    parsed.ns = static_cast<quint16>(ns);
    parsed.valid = true;
    return parsed;
}

/*!
 * \brief Returns the canonical string for \a kind.
 */
QString nodeKindToString(NodeKind kind)
{
    switch (kind) {
    case NodeKind::Folder:
        return QStringLiteral("Folder");
    case NodeKind::Object:
        return QStringLiteral("Object");
    case NodeKind::Variable:
        return QStringLiteral("Variable");
    }
    return QStringLiteral("Folder");
}

/*!
 * \brief Parses \a text into a NodeKind, defaulting to Folder.
 */
NodeKind nodeKindFromString(const QString &text)
{
    if (text == QLatin1String("Variable"))
        return NodeKind::Variable;
    if (text == QLatin1String("Object"))
        return NodeKind::Object;
    return NodeKind::Folder;
}

/*!
 * \brief Returns the canonical string for \a kind.
 */
QString simulationKindToString(SimulationKind kind)
{
    switch (kind) {
    case SimulationKind::Manual:
        return QStringLiteral("Manual");
    case SimulationKind::Constant:
        return QStringLiteral("Constant");
    case SimulationKind::Counter:
        return QStringLiteral("Counter");
    case SimulationKind::Toggle:
        return QStringLiteral("Toggle");
    case SimulationKind::Random:
        return QStringLiteral("Random");
    case SimulationKind::Sine:
        return QStringLiteral("Sine");
    case SimulationKind::Ramp:
        return QStringLiteral("Ramp");
    }
    return QStringLiteral("Manual");
}

/*!
 * \brief Parses \a text into a SimulationKind, defaulting to Manual.
 */
SimulationKind simulationKindFromString(const QString &text)
{
    if (text == QLatin1String("Constant"))
        return SimulationKind::Constant;
    if (text == QLatin1String("Counter"))
        return SimulationKind::Counter;
    if (text == QLatin1String("Toggle"))
        return SimulationKind::Toggle;
    if (text == QLatin1String("Random"))
        return SimulationKind::Random;
    if (text == QLatin1String("Sine"))
        return SimulationKind::Sine;
    if (text == QLatin1String("Ramp"))
        return SimulationKind::Ramp;
    return SimulationKind::Manual;
}

/*!
 * \brief Returns the supported simulation kind names, Manual first.
 */
QStringList simulationKindNames()
{
    return {
        QStringLiteral("Manual"),  QStringLiteral("Constant"), QStringLiteral("Counter"),
        QStringLiteral("Toggle"),  QStringLiteral("Random"),   QStringLiteral("Sine"),
        QStringLiteral("Ramp"),
    };
}

/*!
 * \brief Returns the supported built-in scalar data type names.
 */
QStringList builtinDataTypeNames()
{
    return {
        QStringLiteral("Boolean"),
        QStringLiteral("SByte"),
        QStringLiteral("Byte"),
        QStringLiteral("Int16"),
        QStringLiteral("UInt16"),
        QStringLiteral("Int32"),
        QStringLiteral("UInt32"),
        QStringLiteral("Int64"),
        QStringLiteral("UInt64"),
        QStringLiteral("Float"),
        QStringLiteral("Double"),
        QStringLiteral("String"),
    };
}

} // namespace ServerProject
