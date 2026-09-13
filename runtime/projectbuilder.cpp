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
 * \brief Adds one enumeration DataType node plus its EnumValues property.
 */
bool addEnumType(UA_Server *server, const ServerProject::EnumType &enumType,
                 const QHash<quint16, UA_UInt16> &nsMap, QString &error)
{
    const ParsedNodeId parsed = ServerProject::parseNodeId(enumType.nodeId);
    const UA_UInt16 runtimeNs = nsMap.value(parsed.ns, 0);
    const QByteArray nameBytes = enumType.name.toUtf8();

    UA_NodeId typeId = ProjectBuilder::toRuntimeNodeId(enumType.nodeId, nsMap);

    UA_DataTypeAttributes dtAttr = UA_DataTypeAttributes_default;
    dtAttr.displayName =
        UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>(nameBytes.constData()));
    // Model the enum as an Int32 subtype (not Enumeration): open62541 rejects an
    // Int32 value whose DataType is an Enumeration subtype because Enumeration is
    // not below Int32 in the type tree, so an Enumeration-typed variable cannot be
    // added at runtime with an Int32 value. Subtyping under Int32 keeps the value
    // valid while still exposing the named values through the EnumValues property.
    UA_StatusCode status = UA_Server_addDataTypeNode(
        server, typeId, UA_NODEID_NUMERIC(0, UA_NS0ID_INT32),
        UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
        UA_QUALIFIEDNAME(runtimeNs, const_cast<char *>(nameBytes.constData())), dtAttr, nullptr,
        nullptr);
    if (status != UA_STATUSCODE_GOOD) {
        error = QStringLiteral("Failed to add enum type '%1': %2")
                    .arg(enumType.name, QString::fromUtf8(UA_StatusCode_name(status)));
        UA_NodeId_clear(&typeId);
        return false;
    }

    // Attach the standard EnumValues property so clients can resolve the names.
    std::vector<UA_EnumValueType> values(static_cast<size_t>(enumType.entries.size()));
    for (int i = 0; i < enumType.entries.size(); ++i) {
        UA_EnumValueType_init(&values[i]);
        values[i].value = enumType.entries.at(i).value;
        const QByteArray entryName = enumType.entries.at(i).name.toUtf8();
        values[i].displayName = UA_LOCALIZEDTEXT_ALLOC(const_cast<char *>("en-US"),
                                                       const_cast<char *>(entryName.constData()));
    }

    UA_VariableAttributes vAttr = UA_VariableAttributes_default;
    vAttr.displayName =
        UA_LOCALIZEDTEXT(const_cast<char *>("en-US"), const_cast<char *>("EnumValues"));
    vAttr.dataType = UA_NODEID_NUMERIC(0, UA_NS0ID_ENUMVALUETYPE);
    // A one-dimensional array of any length; ArrayDimensions must match ValueRank.
    UA_UInt32 arrayDimensions[1] = {0};
    vAttr.valueRank = 1;
    vAttr.arrayDimensions = arrayDimensions;
    vAttr.arrayDimensionsSize = 1;
    vAttr.accessLevel = UA_ACCESSLEVELMASK_READ;
    UA_Variant_setArrayCopy(&vAttr.value, values.data(), values.size(),
                            &UA_TYPES[UA_TYPES_ENUMVALUETYPE]);

    status = UA_Server_addVariableNode(server, UA_NODEID_NULL, typeId,
                                       UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY),
                                       UA_QUALIFIEDNAME(0, const_cast<char *>("EnumValues")),
                                       UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), vAttr, nullptr,
                                       nullptr);

    UA_Variant_clear(&vAttr.value);
    for (UA_EnumValueType &value : values)
        UA_EnumValueType_clear(&value);
    UA_NodeId_clear(&typeId);

    if (status != UA_STATUSCODE_GOOD) {
        error = QStringLiteral("Failed to add EnumValues for '%1': %2")
                    .arg(enumType.name, QString::fromUtf8(UA_StatusCode_name(status)));
        return false;
    }
    return true;
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
    const bool isEnumVariable = node.kind == NodeKind::Variable && !node.enumTypeId.isEmpty();
    if (node.kind == NodeKind::Variable) {
        const int typeIndex = isEnumVariable ? UA_TYPES_INT32 : uaTypeIndexFor(node.dataType);
        if (typeIndex < 0) {
            error = QStringLiteral("Unsupported data type '%1' for node '%2'.")
                        .arg(node.dataType, node.nodeId);
            return false;
        }

        // An enum variable is Int32 on the wire but its DataType attribute points
        // at the custom enum type node so clients can resolve the value names.
        UA_NodeId enumDataType = UA_NODEID_NULL;
        if (isEnumVariable)
            enumDataType = ProjectBuilder::toRuntimeNodeId(node.enumTypeId, nsMap);

        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = displayName;
        if (!node.description.isEmpty())
            attr.description = UA_LOCALIZEDTEXT(const_cast<char *>("en-US"),
                                               const_cast<char *>(descriptionBytes.constData()));
        attr.dataType = isEnumVariable ? enumDataType : UA_TYPES[typeIndex].typeId;
        attr.valueRank = isEnumVariable ? -1 : node.valueRank;
        attr.accessLevel = UA_ACCESSLEVELMASK_READ;
        if (node.writable)
            attr.accessLevel |= UA_ACCESSLEVELMASK_WRITE;

        if (isEnumVariable) {
            UA_Int32 enumValue = node.initialValue.isValid() ? node.initialValue.toInt() : 0;
            UA_Variant_setScalarCopy(&attr.value, &enumValue, &UA_TYPES[UA_TYPES_INT32]);
        } else if (node.valueRank == 1) {
            setArray(attr.value, node.dataType, node.initialValue.toList());
        } else if (node.initialValue.isValid()) {
            setScalar(attr.value, node.dataType, node.initialValue);
        }

        status = UA_Server_addVariableNode(
            server, id, parentId, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), browseName,
            UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attr, nullptr, nullptr);

        UA_Variant_clear(&attr.value);
        if (isEnumVariable)
            UA_NodeId_clear(&enumDataType);
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
QHash<quint16, UA_UInt16> registerNamespaces(UA_Server *server, const ProjectData &project)
{
    QHash<quint16, UA_UInt16> nsMap;
    nsMap.insert(0, 0);
    for (int k = 0; k < project.namespaces.size(); ++k) {
        const QByteArray uri = project.namespaces.at(k).uri.toUtf8();
        const UA_UInt16 runtimeNs = UA_Server_addNamespace(server, uri.constData());
        nsMap.insert(static_cast<quint16>(k + 1), runtimeNs);
    }
    return nsMap;
}

UA_NodeId toRuntimeNodeId(const QString &projectNodeId, const QHash<quint16, UA_UInt16> &nsMap)
{
    const ParsedNodeId parsed = ServerProject::parseNodeId(projectNodeId);
    const UA_UInt16 runtimeNs = nsMap.value(parsed.ns, 0);
    if (parsed.numeric)
        return UA_NODEID_NUMERIC(runtimeNs, static_cast<UA_UInt32>(parsed.identifier.toUInt()));
    return UA_NODEID_STRING_ALLOC(runtimeNs, parsed.identifier.toUtf8().constData());
}

bool build(UA_Server *server, const ProjectData &project, QString &error)
{
    const QHash<quint16, UA_UInt16> nsMap = registerNamespaces(server, project);

    // Custom enumeration types must exist before variables reference them.
    for (const ServerProject::EnumType &enumType : project.enumTypes) {
        if (!addEnumType(server, enumType, nsMap, error))
            return false;
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
