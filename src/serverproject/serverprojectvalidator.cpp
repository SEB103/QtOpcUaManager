#include "serverprojectvalidator.h"

#include <QHash>
#include <QSet>

namespace ServerProject {

/*!
 * \brief Validates \a data and collects human-readable errors.
 */
Validator::Result Validator::validate(const ProjectData &data)
{
    Result result;
    const QStringList knownTypes = builtinDataTypeNames();
    const int namespaceCount = data.namespaces.size();

    QSet<QString> seenNodeIds;
    for (const Node &node : data.nodes) {
        const QString label = node.nodeId.isEmpty()
                                  ? QStringLiteral("<node with empty id>")
                                  : node.nodeId;

        const ParsedNodeId parsed = parseNodeId(node.nodeId);
        if (!parsed.valid) {
            result.errors.append(
                QStringLiteral("Node '%1' has a malformed node id.").arg(label));
            continue;
        }

        // ns=0 is the base namespace; ns=1..N map to the project namespaces.
        if (parsed.ns != 0 && parsed.ns > namespaceCount) {
            result.errors.append(
                QStringLiteral("Node '%1' references namespace index %2 but the project "
                               "declares only %3 custom namespace(s).")
                    .arg(label)
                    .arg(parsed.ns)
                    .arg(namespaceCount));
        }

        if (seenNodeIds.contains(node.nodeId)) {
            result.errors.append(QStringLiteral("Duplicate node id '%1'.").arg(label));
        }
        seenNodeIds.insert(node.nodeId);

        if (node.browseName.trimmed().isEmpty())
            result.errors.append(QStringLiteral("Node '%1' has an empty browse name.").arg(label));

        if (node.kind == NodeKind::Variable) {
            if (!knownTypes.contains(node.dataType)) {
                result.errors.append(
                    QStringLiteral("Variable '%1' has an unsupported data type '%2'.")
                        .arg(label, node.dataType));
            }
            if (node.valueRank != -1 && node.valueRank != 1) {
                result.errors.append(
                    QStringLiteral("Variable '%1' has an unsupported value rank %2 "
                                   "(only -1 scalar and 1 one-dimensional array are supported).")
                        .arg(label)
                        .arg(node.valueRank));
            }
        }
    }

    // Every non-empty parent reference must point at an existing node.
    for (const Node &node : data.nodes) {
        if (node.parentNodeId.isEmpty())
            continue;
        if (!seenNodeIds.contains(node.parentNodeId)) {
            result.errors.append(
                QStringLiteral("Node '%1' references a missing parent '%2'.")
                    .arg(node.nodeId.isEmpty() ? QStringLiteral("<node with empty id>")
                                               : node.nodeId,
                         node.parentNodeId));
        }
    }

    result.ok = result.errors.isEmpty();
    return result;
}

} // namespace ServerProject
