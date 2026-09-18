#ifndef RULEENGINE_H
#define RULEENGINE_H

#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

#include <open62541/server.h>

#include "serverproject/serverprojectdata.h"

/**
 * Evaluates the project's behavior rules on a running open62541 server.
 *
 * For every rule, the engine installs an OPC UA write value-callback on the
 * trigger variable. When a client writes the trigger, each action writes a
 * value to its target variable: either a fixed literal or the value just
 * written to the trigger (so PageRequest=X yields PageResponse=X). A non-zero
 * delay defers the write through a single-shot timer, which models a momentary
 * pulse such as a button acknowledgement.
 *
 * The callback, the delayed writes and the SimulationEngine all run on the same
 * event-loop thread that drives UA_Server_run_iterate, so no locking is needed.
 * The engine reproduces the request/response handshakes an HMI expects from a
 * PLC, which a static address-space snapshot cannot.
 */
class RuleEngine : public QObject
{
    Q_OBJECT

public:
    /** Compiles \a project's rules and installs the trigger callbacks on \a server. */
    RuleEngine(UA_Server *server, const ServerProject::ProjectData &project,
               QObject *parent = nullptr);
    ~RuleEngine() override;

    /** Returns whether any rule was installed. */
    bool hasWork() const { return !m_rules.isEmpty(); }

    /**
     * Routes a client write of \a nodeId (carrying \a data) to the matching
     * rules. Public because the open62541 C write-callback trampoline invokes it.
     */
    void onTriggerWrite(const UA_NodeId *nodeId, const UA_DataValue *data);

private:
    /** One compiled action with an owned runtime target node id. */
    struct CompiledAction {
        UA_NodeId targetNodeId;                  /**< Owned runtime node id of the target. */
        QString targetDataType;                  /**< Built-in data type name of the target. */
        ServerProject::RuleValueMode mode =
            ServerProject::RuleValueMode::Literal; /**< Literal or copy-of-trigger. */
        QVariant literalValue;                   /**< Literal to write in Literal mode. */
        double delayMs = 0.0;                    /**< Delay before the write; 0 = immediate. */
    };

    /** One compiled rule with an owned runtime trigger node id. */
    struct CompiledRule {
        UA_NodeId triggerNodeId;         /**< Owned runtime node id of the trigger. */
        QList<CompiledAction> actions;   /**< Actions performed when the trigger is written. */
    };

    /** Performs \a action, using \a triggerData for a copy-of-trigger value. */
    void performAction(const CompiledAction &action, const UA_DataValue *triggerData);

    /**
     * Builds \a out as the value \a action writes, using \a triggerData in
     * copy-of-trigger mode. Returns whether a value could be produced; the
     * caller owns \a out and must clear it.
     */
    bool buildValue(const CompiledAction &action, const UA_DataValue *triggerData,
                    UA_Variant &out) const;

    /** The server whose values are written; not owned. */
    UA_Server *m_server = nullptr;

    /** Compiled rules keyed by their trigger node. */
    QList<CompiledRule> m_rules;

    /**
     * Suppresses rule evaluation while the engine performs its own writes.
     *
     * open62541 invokes the write callback for internal writes too, so a rule
     * whose action targets a trigger would recurse. Rules therefore fire only on
     * external (client) writes, not on the engine's own writes.
     */
    bool m_suppress = false;
};

#endif // RULEENGINE_H
