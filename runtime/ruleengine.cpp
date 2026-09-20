#include "ruleengine.h"

#include <QByteArray>
#include <QHash>
#include <QTimer>

#include "projectbuilder.h"

using ServerProject::Node;
using ServerProject::NodeKind;
using ServerProject::ProjectData;
using ServerProject::Rule;
using ServerProject::RuleAction;
using ServerProject::RuleValueMode;

namespace {

/*!
 * \internal
 * \brief The single active engine, used by the C write-callback trampoline.
 *
 * The runtime hosts exactly one RuleEngine and everything runs on one thread,
 * so a file-static pointer is a safe way to reach the instance from the
 * open62541 C callback.
 */
RuleEngine *s_activeEngine = nullptr;

/*!
 * \internal
 * \brief Fills \a value with one scalar of built-in \a type from \a qv.
 *
 * Returns whether \a type is a supported built-in scalar. Uses copy semantics so
 * the server owns the data; the caller clears \a value.
 */
bool setScalarVariant(UA_Variant &value, const QString &type, const QVariant &qv)
{
    if (type == QLatin1String("Boolean")) {
        UA_Boolean x = qv.toBool();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_BOOLEAN]);
    } else if (type == QLatin1String("SByte")) {
        UA_SByte x = static_cast<UA_SByte>(qv.toInt());
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_SBYTE]);
    } else if (type == QLatin1String("Byte")) {
        UA_Byte x = static_cast<UA_Byte>(qv.toUInt());
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_BYTE]);
    } else if (type == QLatin1String("Int16")) {
        UA_Int16 x = static_cast<UA_Int16>(qv.toInt());
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_INT16]);
    } else if (type == QLatin1String("UInt16")) {
        UA_UInt16 x = static_cast<UA_UInt16>(qv.toUInt());
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_UINT16]);
    } else if (type == QLatin1String("Int32")) {
        UA_Int32 x = qv.toInt();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_INT32]);
    } else if (type == QLatin1String("UInt32")) {
        UA_UInt32 x = qv.toUInt();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_UINT32]);
    } else if (type == QLatin1String("Int64")) {
        UA_Int64 x = qv.toLongLong();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_INT64]);
    } else if (type == QLatin1String("UInt64")) {
        UA_UInt64 x = qv.toULongLong();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_UINT64]);
    } else if (type == QLatin1String("Float")) {
        UA_Float x = qv.toFloat();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_FLOAT]);
    } else if (type == QLatin1String("Double")) {
        UA_Double x = qv.toDouble();
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_DOUBLE]);
    } else if (type == QLatin1String("String")) {
        const QByteArray utf8 = qv.toString().toUtf8();
        UA_String x = UA_STRING_ALLOC(utf8.constData());
        UA_Variant_setScalarCopy(&value, &x, &UA_TYPES[UA_TYPES_STRING]);
        UA_String_clear(&x);
    } else {
        return false;
    }
    return true;
}

/*!
 * \internal
 * \brief C trampoline installed as the trigger nodes' write value-callback.
 */
void onWriteTrampoline(UA_Server * /*server*/, const UA_NodeId * /*sessionId*/,
                       void * /*sessionContext*/, const UA_NodeId *nodeId, void * /*nodeContext*/,
                       const UA_NumericRange * /*range*/, const UA_DataValue *data)
{
    if (s_activeEngine)
        s_activeEngine->onTriggerWrite(nodeId, data);
}

} // namespace

/*!
 * \brief Compiles the rules and installs the trigger write callbacks.
 * \param server Running open62541 server whose values the rules read and write; not owned.
 * \param project Server project whose \c rules are compiled and installed.
 * \param parent Optional QObject parent.
 */
RuleEngine::RuleEngine(UA_Server *server, const ServerProject::ProjectData &project, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    const QHash<quint16, UA_UInt16> nsMap = ProjectBuilder::registerNamespaces(server, project);

    // The built-in data type of every variable, so literal actions can coerce
    // their value to the target's type.
    QHash<QString, QString> variableDataTypes;
    for (const Node &node : project.nodes) {
        if (node.kind == NodeKind::Variable)
            variableDataTypes.insert(node.nodeId, node.dataType);
    }

    UA_ValueCallback callback;
    callback.onRead = nullptr;
    callback.onWrite = onWriteTrampoline;

    for (const Rule &rule : project.rules) {
        if (!variableDataTypes.contains(rule.triggerNodeId))
            continue; // A malformed rule is dropped; the validator reports it.

        CompiledRule compiled;
        compiled.triggerNodeId = ProjectBuilder::toRuntimeNodeId(rule.triggerNodeId, nsMap);

        for (const RuleAction &action : rule.actions) {
            if (!variableDataTypes.contains(action.targetNodeId))
                continue;
            CompiledAction compiledAction;
            compiledAction.targetNodeId = ProjectBuilder::toRuntimeNodeId(action.targetNodeId, nsMap);
            compiledAction.targetDataType = variableDataTypes.value(action.targetNodeId);
            compiledAction.mode = action.valueMode;
            compiledAction.literalValue = action.literalValue;
            compiledAction.delayMs = action.delayMs;
            compiled.actions.append(compiledAction);
        }

        if (compiled.actions.isEmpty()) {
            UA_NodeId_clear(&compiled.triggerNodeId);
            continue;
        }

        UA_Server_setVariableNode_valueCallback(server, compiled.triggerNodeId, callback);
        m_rules.append(compiled);
    }

    if (!m_rules.isEmpty())
        s_activeEngine = this;
}

/*!
 * \brief Clears the owned runtime node ids.
 */
RuleEngine::~RuleEngine()
{
    if (s_activeEngine == this)
        s_activeEngine = nullptr;

    for (CompiledRule &rule : m_rules) {
        for (CompiledAction &action : rule.actions)
            UA_NodeId_clear(&action.targetNodeId);
        UA_NodeId_clear(&rule.triggerNodeId);
    }
}

/*!
 * \brief Routes a client write of \a nodeId to the matching rules.
 * \param data Value just written, used by copy-trigger actions.
 */
void RuleEngine::onTriggerWrite(const UA_NodeId *nodeId, const UA_DataValue *data)
{
    if (m_suppress)
        return; // The engine's own writes must not re-trigger rules.

    for (const CompiledRule &rule : m_rules) {
        if (!UA_NodeId_equal(nodeId, &rule.triggerNodeId))
            continue;
        for (const CompiledAction &action : rule.actions)
            performAction(action, data);
    }
}

/*!
 * \brief Builds the value \a action writes, using \a triggerData when copying.
 */
bool RuleEngine::buildValue(const CompiledAction &action, const UA_DataValue *triggerData,
                            UA_Variant &out) const
{
    UA_Variant_init(&out);
    if (action.mode == RuleValueMode::CopyTrigger) {
        if (!triggerData || !triggerData->hasValue)
            return false;
        return UA_Variant_copy(&triggerData->value, &out) == UA_STATUSCODE_GOOD;
    }
    return setScalarVariant(out, action.targetDataType, action.literalValue);
}

/*!
 * \brief Performs \a action immediately or after its delay.
 */
void RuleEngine::performAction(const CompiledAction &action, const UA_DataValue *triggerData)
{
    UA_Variant value;
    if (!buildValue(action, triggerData, value))
        return;

    if (action.delayMs <= 0.0) {
        m_suppress = true;
        UA_Server_writeValue(m_server, action.targetNodeId, value);
        m_suppress = false;
        UA_Variant_clear(&value);
        return;
    }

    // Defer the write. Own a copy of the target id and the prepared value until
    // the timer fires; everything runs on this (the event-loop) thread.
    UA_NodeId target;
    UA_NodeId_copy(&action.targetNodeId, &target);
    UA_Variant *pendingValue = UA_Variant_new();
    *pendingValue = value; // move ownership; value's data is now owned by pendingValue

    QTimer::singleShot(static_cast<int>(action.delayMs), this, [this, target, pendingValue]() {
        m_suppress = true;
        UA_Server_writeValue(m_server, target, *pendingValue);
        m_suppress = false;
        UA_Variant_delete(pendingValue);
        UA_NodeId owned = target;
        UA_NodeId_clear(&owned);
    });
}
