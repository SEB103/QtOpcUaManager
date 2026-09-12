#include "projectbuilder.h"

#include <memory>
#include <vector>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVariant>

#include <open62541/server_config_default.h>

using ServerProject::Node;
using ServerProject::NodeKind;
using ServerProject::ParsedNodeId;
using ServerProject::ProjectData;

namespace {

/*!
 * \internal
 * \brief Maps a built-in data type name to its UA_TYPES index, or -1 if unknown.
 */
int uaTypeIndexFor(const QString &name)
{
    static const QHash<QString, int> map = {
        {QStringLiteral("Boolean"), UA_TYPES_BOOLEAN},
        {QStringLiteral("SByte"), UA_TYPES_SBYTE},
        {QStringLiteral("Byte"), UA_TYPES_BYTE},
        {QStringLiteral("Int16"), UA_TYPES_INT16},
        {QStringLiteral("UInt16"), UA_TYPES_UINT16},
        {QStringLiteral("Int32"), UA_TYPES_INT32},
        {QStringLiteral("UInt32"), UA_TYPES_UINT32},
        {QStringLiteral("Int64"), UA_TYPES_INT64},
        {QStringLiteral("UInt64"), UA_TYPES_UINT64},
        {QStringLiteral("Float"), UA_TYPES_FLOAT},
        {QStringLiteral("Double"), UA_TYPES_DOUBLE},
        {QStringLiteral("String"), UA_TYPES_STRING},
    };
    return map.value(name, -1);
}

/*!
 * \internal
 * \brief Fills \a value with one scalar of \a type converted from \a qv.
 *
 * Uses copy semantics so the server owns the data; string temporaries are
 * cleared here.
 */
void setScalar(UA_Variant &value, const QString &type, const QVariant &qv)
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
    }
}

/*!
 * \internal
 * \brief Fills \a value with an array of \a type converted from \a list.
 */
template <typename T>
void setNumericArray(UA_Variant &value, int typeIndex, const QVariantList &list,
                     T (*convert)(const QVariant &))
{
    // A plain heap buffer, not std::vector, because std::vector<bool> is the
    // specialized bit container and has no data() for UA_Boolean arrays.
    const size_t count = static_cast<size_t>(list.size());
    auto buffer = std::make_unique<T[]>(count == 0 ? 1 : count);
    for (size_t i = 0; i < count; ++i)
        buffer[i] = convert(list.at(int(i)));
    UA_Variant_setArrayCopy(&value, buffer.get(), count, &UA_TYPES[typeIndex]);
}

/*!
 * \internal
 * \brief Fills \a value with an array of \a type converted from \a list.
 */
void setArray(UA_Variant &value, const QString &type, const QVariantList &list)
{
    if (type == QLatin1String("Boolean"))
        setNumericArray<UA_Boolean>(value, UA_TYPES_BOOLEAN, list,
                                    [](const QVariant &v) { return UA_Boolean(v.toBool()); });
    else if (type == QLatin1String("SByte"))
        setNumericArray<UA_SByte>(value, UA_TYPES_SBYTE, list,
                                  [](const QVariant &v) { return UA_SByte(v.toInt()); });
    else if (type == QLatin1String("Byte"))
        setNumericArray<UA_Byte>(value, UA_TYPES_BYTE, list,
                                 [](const QVariant &v) { return UA_Byte(v.toUInt()); });
    else if (type == QLatin1String("Int16"))
        setNumericArray<UA_Int16>(value, UA_TYPES_INT16, list,
                                  [](const QVariant &v) { return UA_Int16(v.toInt()); });
    else if (type == QLatin1String("UInt16"))
        setNumericArray<UA_UInt16>(value, UA_TYPES_UINT16, list,
                                   [](const QVariant &v) { return UA_UInt16(v.toUInt()); });
    else if (type == QLatin1String("Int32"))
        setNumericArray<UA_Int32>(value, UA_TYPES_INT32, list,
                                  [](const QVariant &v) { return UA_Int32(v.toInt()); });
    else if (type == QLatin1String("UInt32"))
        setNumericArray<UA_UInt32>(value, UA_TYPES_UINT32, list,
                                   [](const QVariant &v) { return UA_UInt32(v.toUInt()); });
    else if (type == QLatin1String("Int64"))
        setNumericArray<UA_Int64>(value, UA_TYPES_INT64, list,
                                  [](const QVariant &v) { return UA_Int64(v.toLongLong()); });
    else if (type == QLatin1String("UInt64"))
        setNumericArray<UA_UInt64>(value, UA_TYPES_UINT64, list,
                                   [](const QVariant &v) { return UA_UInt64(v.toULongLong()); });
    else if (type == QLatin1String("Float"))
        setNumericArray<UA_Float>(value, UA_TYPES_FLOAT, list,
                                  [](const QVariant &v) { return UA_Float(v.toFloat()); });
    else if (type == QLatin1String("Double"))
        setNumericArray<UA_Double>(value, UA_TYPES_DOUBLE, list,
                                   [](const QVariant &v) { return UA_Double(v.toDouble()); });
    else if (type == QLatin1String("String")) {
        std::vector<UA_String> buffer;
        buffer.reserve(list.size());
        for (const QVariant &item : list)
            buffer.push_back(UA_STRING_ALLOC(item.toString().toUtf8().constData()));
        UA_Variant_setArrayCopy(&value, buffer.data(), buffer.size(), &UA_TYPES[UA_TYPES_STRING]);
        for (UA_String &s : buffer)
            UA_String_clear(&s);
    }
}

/*!
 * \internal
 * \brief Builds a UA_NodeId referencing \a idBytes for a string id.
 *
 * \a idBytes must outlive the returned node id (server calls copy it). Numeric
 * ids need no backing storage.
 */
UA_NodeId makeNodeId(const ParsedNodeId &parsed, UA_UInt16 runtimeNs, const QByteArray &idBytes)
{
    if (parsed.numeric)
        return UA_NODEID_NUMERIC(runtimeNs, static_cast<UA_UInt32>(parsed.identifier.toUInt()));
    return UA_NODEID_STRING(runtimeNs, const_cast<char *>(idBytes.constData()));
}

/*!
 * \internal
 * \brief Adds one node to \a server, translating namespaces through \a nsMap.
 */
bool addNode(UA_Server *server, const Node &node, const QHash<quint16, UA_UInt16> &nsMap,
             QString &error)
{
    const ParsedNodeId parsed = ServerProject::parseNodeId(node.nodeId);
    const UA_UInt16 runtimeNs = nsMap.value(parsed.ns, 0);

    // Keep every backing byte array alive until after the add call, since the
    // non-allocating open62541 macros reference these buffers and the server
    // copies them internally.
    const QByteArray idBytes = parsed.identifier.toUtf8();
    const QByteArray browseBytes = node.browseName.toUtf8();
    const QByteArray displayBytes =
        (node.displayName.isEmpty() ? node.browseName : node.displayName).toUtf8();
    const QByteArray descriptionBytes = node.description.toUtf8();

    const UA_NodeId id = makeNodeId(parsed, runtimeNs, idBytes);

    QByteArray parentIdBytes;
    UA_NodeId parentId;
    if (node.parentNodeId.isEmpty()) {
        parentId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    } else {
        const ParsedNodeId parentParsed = ServerProject::parseNodeId(node.parentNodeId);
        const UA_UInt16 parentNs = nsMap.value(parentParsed.ns, 0);
        parentIdBytes = parentParsed.identifier.toUtf8();
        parentId = makeNodeId(parentParsed, parentNs, parentIdBytes);
    }

    const UA_QualifiedName browseName =
        UA_QUALIFIEDNAME(runtimeNs, const_cast<char *>(browseBytes.constData()));
    const UA_LocalizedText displayName =
        UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(displayBytes.constData()));

    UA_StatusCode status = UA_STATUSCODE_GOOD;
    if (node.kind == NodeKind::Variable) {
        const int typeIndex = uaTypeIndexFor(node.dataType);
        if (typeIndex < 0) {
            error = QStringLiteral("Unsupported data type '%1' for node '%2'.")
                        .arg(node.dataType, node.nodeId);
            return false;
        }

        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = displayName;
        if (!node.description.isEmpty())
            attr.description = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                               const_cast<char *>(descriptionBytes.constData()));
        attr.dataType = UA_TYPES[typeIndex].typeId;
        attr.valueRank = node.valueRank;
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        if (node.writable)
            attr.accessLevel |= UA_ACCESSLEVELMASK_WRITE;

        if (node.valueRank == 1)
            setArray(attr.value, node.dataType, node.initialValue.toList());
        else if (node.initialValue.isValid())
            setScalar(attr.value, node.dataType, node.initialValue);

        status = UA_Server_addVariableNode(
            server, id, parentId, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), browseName,
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr, nullptr);

        UA_Variant_clear(&attr.value);
    } else {
        UA_ObjectAttributes attr = UA_ObjectAttributes_default;
        attr.displayName = displayName;
        if (!node.description.isEmpty())
            attr.description = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                               const_cast<char *>(descriptionBytes.constData()));

        const bool isFolder = node.kind == NodeKind::Folder;
        const UA_NodeId referenceType = isFolder
            ? UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES)
            : UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
        const UA_NodeId typeDefinition = isFolder
            ? UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE)
            : UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE);

        status = UA_Server_addObjectNode(server, id, parentId, referenceType, browseName,
                                         typeDefinition, attr, nullptr, nullptr);
    }

    if (status != UA_STATUSCODE_GOOD) {
        error = QStringLiteral("Failed to add node '%1': %2")
                    .arg(node.nodeId, QString::fromUtf8(UA_StatusCode_name(status)));
        return false;
    }
    return true;
}

} // namespace

namespace ProjectBuilder {

/*!
 * \brief Adds every namespace and node from \a project to \a server.
 */
bool build(UA_Server *server, const ProjectData &project, QString &error)
{
    QHash<quint16, UA_UInt16> nsMap;
    nsMap.insert(0, 0);
    for (int k = 0; k < project.namespaces.size(); ++k) {
        const QByteArray uri = project.namespaces.at(k).uri.toUtf8();
        const UA_UInt16 runtimeNs = UA_Server_addNamespace(server, uri.constData());
        nsMap.insert(static_cast<quint16>(k + 1), runtimeNs);
    }

    // Add nodes parent-before-child: repeatedly add every node whose parent has
    // already been created (or which sits directly under the Objects folder).
    QSet<QString> created;
    QList<Node> pending = project.nodes;
    bool progress = true;
    while (!pending.isEmpty() && progress) {
        progress = false;
        for (int i = 0; i < pending.size();) {
            const Node &node = pending.at(i);
            const bool parentReady =
                node.parentNodeId.isEmpty() || created.contains(node.parentNodeId);
            if (parentReady) {
                if (!addNode(server, node, nsMap, error))
                    return false;
                created.insert(node.nodeId);
                pending.removeAt(i);
                progress = true;
            } else {
                ++i;
            }
        }
    }

    if (!pending.isEmpty()) {
        error = QStringLiteral("Could not resolve parent references for %1 node(s).")
                    .arg(pending.size());
        return false;
    }
    return true;
}

} // namespace ProjectBuilder
