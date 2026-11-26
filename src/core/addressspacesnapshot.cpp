#include "addressspacesnapshot.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <QtOpcUa/qopcuatype.h>

#include <algorithm>
#include <numeric>

#include "snapshotvaluecodec.h"

/*!
    \namespace AddressSpaceSnapshot
    \inmodule OpcUaManager
    \brief Builds and writes the JSON address-space snapshot (\c{*.uasnapshot.json}).

    The document has these top-level members:

    \list
    \li \c format and \c formatVersion identify the file; \c created and
        \c generator say when and by which application it was written.
    \li \c server holds the endpoint, security and BuildInfo of the source server.
    \li \c namespaces lists the NamespaceArray with each index.
    \li \c scope, \c limits and \c completion describe what was browsed, with
        which limits, and whether the result is complete, with the reasons.
    \li \c dataTypes lists every DataType used by a variable with its name,
        built-in base type, supertype chain and how it was resolved.
    \li \c nodes lists the nodes sorted by browse path.
    \li \c diagnostics lists the nodes whose attributes or browse failed.
    \endlist

    A variable's \c value object states its read outcome explicitly: a value
    with \c status, \c variantType, \c data and timestamps; only a \c status
    for a failed read; \c {"read": false} when no answer arrived; or
    \c {"skipped": "structure"} for a structure, whose members are captured as
    nodes of their own. Without values (SnapshotScope::includeValues off) the
    \c value member is absent. A missing value is never written as 0.
*/

namespace {

/*!
 * \internal
 * \brief Returns whether \a status is a Bad status code.
 */
bool isBad(quint32 status)
{
    return (status & 0x80000000u) != 0;
}

/*!
 * \internal
 * \brief Returns the name of OPC UA NodeClass \a nodeClass, or its number when unknown.
 */
QString nodeClassName(int nodeClass)
{
    switch (nodeClass) {
    case 1: return QStringLiteral("Object");
    case 2: return QStringLiteral("Variable");
    case 4: return QStringLiteral("Method");
    case 8: return QStringLiteral("ObjectType");
    case 16: return QStringLiteral("VariableType");
    case 32: return QStringLiteral("ReferenceType");
    case 64: return QStringLiteral("DataType");
    case 128: return QStringLiteral("View");
    default: return QString::number(nodeClass);
    }
}

/*!
 * \internal
 * \brief Returns the names of the bits set in the AccessLevel mask \a accessLevel.
 */
QJsonArray accessLevelNames(int accessLevel)
{
    static const QList<std::pair<int, QString>> bits = {
        {0x01, QStringLiteral("CurrentRead")},   {0x02, QStringLiteral("CurrentWrite")},
        {0x04, QStringLiteral("HistoryRead")},   {0x08, QStringLiteral("HistoryWrite")},
        {0x10, QStringLiteral("SemanticChange")}, {0x20, QStringLiteral("StatusWrite")},
        {0x40, QStringLiteral("TimestampWrite")},
    };
    QJsonArray names;
    for (const auto &[bit, name] : bits) {
        if (accessLevel & bit)
            names.append(name);
    }
    return names;
}

/*!
 * \internal
 * \brief Returns the name of DataType \a resolution.
 */
QString resolutionName(ResolvedDataType::Resolution resolution)
{
    switch (resolution) {
    case ResolvedDataType::Resolution::BuiltIn: return QStringLiteral("BuiltIn");
    case ResolvedDataType::Resolution::KnownSubtype: return QStringLiteral("KnownSubtype");
    case ResolvedDataType::Resolution::Hierarchy: return QStringLiteral("Hierarchy");
    case ResolvedDataType::Resolution::Unresolved: return QStringLiteral("Unresolved");
    case ResolvedDataType::Resolution::None: break;
    }
    return QStringLiteral("None");
}

/*!
 * \internal
 * \brief Returns whether \a type counts as unresolved in the completion counters.
 */
bool isUnresolved(const ResolvedDataType &type)
{
    return type.resolution == ResolvedDataType::Resolution::None
           || type.resolution == ResolvedDataType::Resolution::Unresolved;
}

/*!
 * \internal
 * \brief Returns the name of traversal \a state.
 */
QString stateName(CloneCompletion::State state)
{
    switch (state) {
    case CloneCompletion::State::Complete: return QStringLiteral("Complete");
    case CloneCompletion::State::Truncated: return QStringLiteral("Truncated");
    case CloneCompletion::State::TimedOut: return QStringLiteral("TimedOut");
    case CloneCompletion::State::Cancelled: return QStringLiteral("Cancelled");
    case CloneCompletion::State::Failed: break;
    }
    return QStringLiteral("Failed");
}

/*!
 * \internal
 * \brief Returns \a dateTime as ISO-8601 UTC with milliseconds.
 */
QString isoUtc(const QDateTime &dateTime)
{
    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

/*!
 * \internal
 * \brief Returns the \c value object of variable \a node.
 *
 * The read outcome is explicit: skipped structure, no answer, failed read
 * with its status, or the value with status, type, data and timestamps.
 */
QJsonObject valueObject(const CloneNode &node)
{
    if (node.valueSkipped)
        return {{QStringLiteral("skipped"), QStringLiteral("structure")}};

    if (!node.valueRead) {
        QJsonObject notRead{{QStringLiteral("read"), false}};
        for (const QString &error : node.attributeErrors) {
            if (error.startsWith(QLatin1String("Value:"))) {
                notRead.insert(QStringLiteral("reason"), error.mid(6).trimmed());
                break;
            }
        }
        return notRead;
    }

    const QString status = QOpcUa::statusToString(QOpcUa::UaStatusCode(node.valueStatus));
    if (isBad(node.valueStatus))
        return {{QStringLiteral("status"), status}};

    QJsonObject value{{QStringLiteral("status"), status},
                      {QStringLiteral("variantType"),
                       SnapshotValueCodec::variantTypeName(node.value)},
                      {QStringLiteral("data"),
                       SnapshotValueCodec::encodeForValueRank(node.value, node.valueRank)}};
    if (node.sourceTimestamp.isValid())
        value.insert(QStringLiteral("sourceTimestamp"), isoUtc(node.sourceTimestamp));
    if (node.serverTimestamp.isValid())
        value.insert(QStringLiteral("serverTimestamp"), isoUtc(node.serverTimestamp));
    return value;
}

/*!
 * \internal
 * \brief Returns the JSON object of \a node at \a browsePath.
 * \param node Captured node.
 * \param browsePath Browse path of the node.
 * \param includeValues Whether the \c value member is written for variables.
 */
QJsonObject nodeObject(const CloneNode &node, const QString &browsePath, bool includeValues)
{
    QJsonObject object{
        {QStringLiteral("nodeId"), node.nodeId},
        {QStringLiteral("nodeClass"), nodeClassName(node.nodeClass)},
        {QStringLiteral("browseName"),
         QJsonObject{{QStringLiteral("namespaceIndex"), int(node.browseNameNamespace)},
                     {QStringLiteral("name"), node.browseName}}},
        {QStringLiteral("displayName"), node.displayName},
        {QStringLiteral("browsePath"), browsePath},
    };
    if (!node.parentNodeId.isEmpty())
        object.insert(QStringLiteral("parentNodeId"), node.parentNodeId);
    if (!node.additionalParentIds.isEmpty())
        object.insert(QStringLiteral("additionalParentIds"),
                      QJsonArray::fromStringList(node.additionalParentIds));
    if (!node.referenceTypeId.isEmpty())
        object.insert(QStringLiteral("referenceTypeId"), node.referenceTypeId);
    if (!node.typeDefinitionId.isEmpty())
        object.insert(QStringLiteral("typeDefinition"), node.typeDefinitionId);

    if (node.isVariable) {
        QJsonObject variable{
            {QStringLiteral("dataType"),
             QJsonObject{{QStringLiteral("nodeId"), node.dataTypeId},
                         {QStringLiteral("name"), node.resolvedDataType.typeName}}},
            {QStringLiteral("valueRank"), node.valueRank},
        };
        if (!node.arrayDimensions.isEmpty()) {
            QJsonArray dimensions;
            for (const quint32 dimension : node.arrayDimensions)
                dimensions.append(double(dimension));
            variable.insert(QStringLiteral("arrayDimensions"), dimensions);
        }
        if (node.accessLevel >= 0)
            variable.insert(QStringLiteral("accessLevel"), accessLevelNames(node.accessLevel));
        if (node.userAccessLevel >= 0)
            variable.insert(QStringLiteral("userAccessLevel"),
                            accessLevelNames(node.userAccessLevel));
        if (includeValues)
            variable.insert(QStringLiteral("value"), valueObject(node));
        object.insert(QStringLiteral("variable"), variable);
    }

    if (!node.attributeErrors.isEmpty())
        object.insert(QStringLiteral("attributeErrors"),
                      QJsonArray::fromStringList(node.attributeErrors));
    if (!node.browseError.isEmpty())
        object.insert(QStringLiteral("browseError"), node.browseError);
    return object;
}

/*!
 * \internal
 * \brief Returns the browse path of every node in \a nodes, in the same order.
 *
 * A node's path is its parent's path plus "/" and its browse name; a node
 * without a captured parent starts at \a rootBrowsePath. Parents precede their
 * children in \a nodes, so one pass suffices.
 */
QStringList browsePaths(const QList<CloneNode> &nodes, const QString &rootBrowsePath)
{
    QHash<QString, qsizetype> index;
    index.reserve(nodes.size());
    QStringList paths;
    paths.reserve(nodes.size());
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        const CloneNode &node = nodes.at(i);
        const auto parent = index.constFind(node.parentNodeId);
        const QString &base = (node.parentNodeId.isEmpty() || parent == index.constEnd())
                                  ? rootBrowsePath
                                  : paths.at(parent.value());
        paths.append(base + QLatin1Char('/') + node.browseName);
        index.insert(node.nodeId, i);
    }
    return paths;
}

/*!
 * \internal
 * \brief Returns the \c dataTypes array and counts the unresolved types into \a unresolved.
 */
QJsonArray dataTypesArray(const QList<CloneNode> &nodes, int &unresolved)
{
    QHash<QString, ResolvedDataType> types;
    for (const CloneNode &node : nodes) {
        if (node.isVariable && !node.dataTypeId.isEmpty() && !types.contains(node.dataTypeId))
            types.insert(node.dataTypeId, node.resolvedDataType);
    }
    QStringList ids = types.keys();
    std::sort(ids.begin(), ids.end());

    unresolved = 0;
    QJsonArray array;
    for (const QString &id : std::as_const(ids)) {
        const ResolvedDataType &type = types.value(id);
        QJsonObject object{
            {QStringLiteral("nodeId"), id},
            {QStringLiteral("name"), type.typeName},
            {QStringLiteral("builtInBase"), type.builtInBase},
            {QStringLiteral("superTypes"), QJsonArray::fromStringList(type.superTypeIds)},
            {QStringLiteral("resolution"), resolutionName(type.resolution)},
        };
        if (!type.reason.isEmpty())
            object.insert(QStringLiteral("reason"), type.reason);
        if (isUnresolved(type))
            ++unresolved;
        array.append(object);
    }
    return array;
}

/*!
 * \internal
 * \brief Returns the \c completion object of \a completion with \a unresolvedTypes.
 */
QJsonObject completionObject(const CloneCompletion &completion, int unresolvedTypes)
{
    QJsonArray reasons;
    if (completion.nodeLimitReached)
        reasons.append(QStringLiteral("nodeLimit"));
    if (completion.depthLimitedNodes > 0)
        reasons.append(QStringLiteral("depthLimit"));
    if (completion.state == CloneCompletion::State::TimedOut)
        reasons.append(QStringLiteral("timeout"));
    if (completion.state == CloneCompletion::State::Cancelled)
        reasons.append(QStringLiteral("cancelled"));
    if (completion.state == CloneCompletion::State::Failed)
        reasons.append(QStringLiteral("failed"));
    if (completion.failedReads > 0)
        reasons.append(QStringLiteral("readErrors"));
    if (completion.failedBrowses > 0)
        reasons.append(QStringLiteral("browseErrors"));
    if (unresolvedTypes > 0)
        reasons.append(QStringLiteral("unresolvedTypes"));

    return {
        {QStringLiteral("state"), stateName(completion.state)},
        {QStringLiteral("reasons"), reasons},
        {QStringLiteral("nodeCount"), completion.nodeCount},
        {QStringLiteral("pendingRequests"), completion.pendingRequests},
        {QStringLiteral("failedReads"), completion.failedReads},
        {QStringLiteral("failedBrowses"), completion.failedBrowses},
        {QStringLiteral("depthLimitedNodes"), completion.depthLimitedNodes},
        {QStringLiteral("unresolvedTypes"), unresolvedTypes},
        {QStringLiteral("elapsedMs"), double(completion.elapsedMs)},
    };
}

/*!
 * \internal
 * \brief Returns the \c server object of \a server; empty fields are left out.
 */
QJsonObject serverObject(const ServerIdentity &server)
{
    QJsonObject object;
    const auto add = [&object](const char *key, const QString &value) {
        if (!value.isEmpty())
            object.insert(QLatin1String(key), value);
    };
    add("endpointUrl", server.endpointUrl);
    add("securityMode", server.securityMode);
    add("securityPolicy", server.securityPolicy);
    add("applicationUri", server.applicationUri);
    add("productUri", server.productUri);
    add("productName", server.productName);
    add("manufacturerName", server.manufacturerName);
    add("softwareVersion", server.softwareVersion);
    add("buildNumber", server.buildNumber);
    if (server.buildDate.isValid())
        object.insert(QStringLiteral("buildDate"), isoUtc(server.buildDate));
    return object;
}

} // namespace

/*!
 * \brief Returns the format identifier stored in every snapshot.
 */
QString AddressSpaceSnapshot::formatName()
{
    return QStringLiteral("opcuamanager.addressspace-snapshot");
}

/*!
 * \brief Returns the snapshot document for \a input.
 *
 * The nodes are sorted by browse path and then by node id, so the order does
 * not depend on the order in which the server answered.
 */
QJsonObject AddressSpaceSnapshot::toJson(const SnapshotInput &input)
{
    const QStringList paths = browsePaths(input.nodes, input.scope.rootBrowsePath);
    QList<qsizetype> order(input.nodes.size());
    std::iota(order.begin(), order.end(), qsizetype(0));
    std::stable_sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
        const int byPath = QString::compare(paths.at(a), paths.at(b));
        return byPath != 0 ? byPath < 0 : input.nodes.at(a).nodeId < input.nodes.at(b).nodeId;
    });

    QJsonArray nodes;
    QJsonArray diagnostics;
    for (const qsizetype i : std::as_const(order)) {
        const CloneNode &node = input.nodes.at(i);
        nodes.append(nodeObject(node, paths.at(i), input.scope.includeValues));
        if (!node.attributeErrors.isEmpty() || !node.browseError.isEmpty()) {
            QStringList errors = node.attributeErrors;
            if (!node.browseError.isEmpty())
                errors.append(QStringLiteral("Browse: %1").arg(node.browseError));
            diagnostics.append(QJsonObject{{QStringLiteral("nodeId"), node.nodeId},
                                           {QStringLiteral("browsePath"), paths.at(i)},
                                           {QStringLiteral("errors"),
                                            QJsonArray::fromStringList(errors)}});
        }
    }

    QJsonArray namespaces;
    for (qsizetype i = 0; i < input.namespaceUris.size(); ++i) {
        namespaces.append(QJsonObject{{QStringLiteral("index"), int(i)},
                                      {QStringLiteral("uri"), input.namespaceUris.at(i)}});
    }

    int unresolvedTypes = 0;
    const QJsonArray dataTypes = dataTypesArray(input.nodes, unresolvedTypes);
    const CloneOptions &limits = input.completion.options;

    return {
        {QStringLiteral("format"), formatName()},
        {QStringLiteral("formatVersion"), kFormatVersion},
        {QStringLiteral("created"), isoUtc(input.created)},
        {QStringLiteral("generator"),
         QJsonObject{{QStringLiteral("application"), input.application},
                     {QStringLiteral("version"), input.applicationVersion}}},
        {QStringLiteral("server"), serverObject(input.completion.server)},
        {QStringLiteral("namespaces"), namespaces},
        {QStringLiteral("scope"),
         QJsonObject{{QStringLiteral("kind"), input.scope.kind},
                     {QStringLiteral("rootNodeId"), input.scope.rootNodeId},
                     {QStringLiteral("rootBrowsePath"), input.scope.rootBrowsePath},
                     {QStringLiteral("includeValues"), input.scope.includeValues}}},
        {QStringLiteral("limits"),
         QJsonObject{{QStringLiteral("maxNodes"), limits.maxNodes},
                     {QStringLiteral("maxDepth"), limits.maxDepth},
                     {QStringLiteral("overallTimeoutMs"), limits.overallTimeoutMs},
                     {QStringLiteral("idleTimeoutMs"), limits.idleTimeoutMs},
                     {QStringLiteral("typeLookupTimeoutMs"), limits.typeLookupTimeoutMs}}},
        {QStringLiteral("completion"), completionObject(input.completion, unresolvedTypes)},
        {QStringLiteral("dataTypes"), dataTypes},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("diagnostics"), diagnostics},
    };
}

/*!
 * \brief Writes the snapshot of \a input to \a filePath atomically, as indented JSON.
 *
 * QSaveFile replaces an existing file only after the whole document was
 * written, so a failed write never leaves a truncated snapshot behind.
 */
AddressSpaceSnapshot::WriteResult AddressSpaceSnapshot::write(const QString &filePath,
                                                              const SnapshotInput &input)
{
    WriteResult result;
    const QByteArray data = QJsonDocument(toJson(input)).toJson(QJsonDocument::Indented);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.errorString = file.errorString();
        return result;
    }
    if (file.write(data) != data.size()) {
        result.errorString = file.errorString();
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) {
        result.errorString = file.errorString();
        return result;
    }
    result.ok = true;
    result.bytes = data.size();
    return result;
}
