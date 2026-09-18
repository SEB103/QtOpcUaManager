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

    // Validate enumeration types and collect their ids for variable references.
    QSet<QString> enumTypeIds;
    for (const EnumType &enumType : data.enumTypes) {
        const QString label = enumType.name.isEmpty() ? QStringLiteral("<unnamed enum>")
                                                      : enumType.name;
        if (!parseNodeId(enumType.nodeId).valid)
            result.errors.append(QStringLiteral("Enum '%1' has a malformed node id.").arg(label));
        if (enumTypeIds.contains(enumType.nodeId))
            result.errors.append(QStringLiteral("Duplicate enum node id '%1'.").arg(enumType.nodeId));
        enumTypeIds.insert(enumType.nodeId);
        if (enumType.entries.isEmpty())
            result.errors.append(QStringLiteral("Enum '%1' has no values.").arg(label));
    }

    QSet<QString> seenNodeIds;
    QSet<QString> variableNodeIds;
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
            variableNodeIds.insert(node.nodeId);
            if (!node.enumTypeId.isEmpty()) {
                if (!enumTypeIds.contains(node.enumTypeId)) {
                    result.errors.append(
                        QStringLiteral("Variable '%1' references a missing enum type '%2'.")
                            .arg(label, node.enumTypeId));
                }
            } else if (!knownTypes.contains(node.dataType)) {
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
            if (node.simulation.kind != SimulationKind::Manual) {
                if (node.simulation.intervalMs <= 0.0) {
                    result.errors.append(
                        QStringLiteral("Variable '%1' has a non-positive simulation interval.")
                            .arg(label));
                }
                if ((node.simulation.kind == SimulationKind::Sine
                     || node.simulation.kind == SimulationKind::Ramp)
                    && node.simulation.periodMs <= 0.0) {
                    result.errors.append(
                        QStringLiteral("Variable '%1' has a non-positive simulation period.")
                            .arg(label));
                }
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

    // Behavior rules: the trigger and every action target must be an existing
    // variable node (values are only written to variables), and a delay cannot
    // be negative.
    for (int i = 0; i < data.rules.size(); ++i) {
        const Rule &rule = data.rules.at(i);
        const QString ruleLabel = rule.triggerNodeId.isEmpty()
                                      ? QStringLiteral("<rule %1>").arg(i + 1)
                                      : rule.triggerNodeId;
        if (!variableNodeIds.contains(rule.triggerNodeId)) {
            result.errors.append(
                QStringLiteral("Rule '%1' triggers on '%2', which is not a variable node.")
                    .arg(ruleLabel, rule.triggerNodeId));
        }
        if (rule.actions.isEmpty())
            result.errors.append(QStringLiteral("Rule '%1' has no actions.").arg(ruleLabel));
        for (const RuleAction &action : rule.actions) {
            if (!variableNodeIds.contains(action.targetNodeId)) {
                result.errors.append(
                    QStringLiteral("Rule '%1' writes '%2', which is not a variable node.")
                        .arg(ruleLabel, action.targetNodeId));
            }
            if (action.delayMs < 0.0) {
                result.errors.append(
                    QStringLiteral("Rule '%1' has an action with a negative delay.").arg(ruleLabel));
            }
        }
    }

    // Security: the server must offer at least one endpoint and at least one
    // way to authenticate.
    if (!data.security.allowNone && !data.security.enableSecurity) {
        result.errors.append(QStringLiteral(
            "Security: no endpoint is offered (enable the None endpoint or encryption)."));
    }
    if (!data.security.allowAnonymous && data.security.users.isEmpty()) {
        result.errors.append(QStringLiteral(
            "Security: anonymous access is disabled but no user accounts are defined."));
    }
    for (const UserCredential &user : data.security.users) {
        if (user.username.trimmed().isEmpty()) {
            result.errors.append(QStringLiteral("Security: a user account has an empty user name."));
            break;
        }
    }

    result.ok = result.errors.isEmpty();
    return result;
}

} // namespace ServerProject
