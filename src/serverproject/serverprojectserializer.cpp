#include "serverprojectserializer.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

namespace ServerProject {

namespace {

/*!
 * \internal
 * \brief Returns \a dimensions as a JSON array.
 */
QJsonArray dimensionsToJson(const QList<quint32> &dimensions)
{
    QJsonArray array;
    for (const quint32 length : dimensions)
        array.append(qint64(length));
    return array;
}

/*!
 * \internal
 * \brief Returns the dimensions stored in JSON array \a array.
 */
QList<quint32> dimensionsFromJson(const QJsonArray &array)
{
    QList<quint32> dimensions;
    for (const QJsonValue &value : array)
        dimensions.append(quint32(value.toInteger()));
    return dimensions;
}

/*!
 * \internal
 * \brief Returns whether \a data uses a feature introduced with format version 2.
 *
 * Projects without structure types, multi-dimensional arrays or array
 * dimensions are written as version 1 so older builds can still read them.
 */
bool needsVersion2(const ProjectData &data)
{
    if (!data.structTypes.isEmpty())
        return true;
    for (const Node &node : data.nodes) {
        if (node.isStructure() || node.valueRank > 1 || !node.arrayDimensions.isEmpty())
            return true;
    }
    return false;
}

/*!
 * \internal
 * \brief Serializes one node to a JSON object.
 */
QJsonObject nodeToJson(const Node &node)
{
    QJsonObject obj;
    obj["kind"] = nodeKindToString(node.kind);
    obj["nodeId"] = node.nodeId;
    obj["parentNodeId"] = node.parentNodeId;
    obj["browseName"] = node.browseName;
    obj["displayName"] = node.displayName;
    if (!node.description.isEmpty())
        obj["description"] = node.description;

    if (node.kind == NodeKind::Variable) {
        obj["dataType"] = node.dataType;
        if (!node.enumTypeId.isEmpty())
            obj["enumTypeId"] = node.enumTypeId;
        if (!node.structTypeId.isEmpty())
            obj["structTypeId"] = node.structTypeId;
        obj["valueRank"] = node.valueRank;
        if (!node.arrayDimensions.isEmpty())
            obj["arrayDimensions"] = dimensionsToJson(node.arrayDimensions);
        obj["writable"] = node.writable;
        if (node.initialValue.isValid())
            obj["initialValue"] = QJsonValue::fromVariant(node.initialValue);

        if (node.simulation.kind != SimulationKind::Manual) {
            QJsonObject sim;
            sim["kind"] = simulationKindToString(node.simulation.kind);
            sim["intervalMs"] = node.simulation.intervalMs;
            sim["min"] = node.simulation.min;
            sim["max"] = node.simulation.max;
            sim["step"] = node.simulation.step;
            sim["periodMs"] = node.simulation.periodMs;
            obj["simulation"] = sim;
        }
    }

    if (!node.source.isEmpty()) {
        QJsonObject source;
        if (!node.source.originalNodeId.isEmpty())
            source["originalNodeId"] = node.source.originalNodeId;
        if (!node.source.originalDataTypeId.isEmpty())
            source["originalDataTypeId"] = node.source.originalDataTypeId;
        if (!node.source.originalDataTypeName.isEmpty())
            source["originalDataTypeName"] = node.source.originalDataTypeName;
        if (!node.source.builtInBase.isEmpty())
            source["builtInBase"] = node.source.builtInBase;
        if (node.kind == NodeKind::Variable)
            source["originalValueRank"] = node.source.originalValueRank;
        if (node.source.substituted)
            source["substituted"] = true;
        obj["source"] = source;
    }
    return obj;
}

/*!
 * \internal
 * \brief Parses one JSON object into a node.
 */
Node nodeFromJson(const QJsonObject &obj)
{
    Node node;
    node.kind = nodeKindFromString(obj.value("kind").toString());
    node.nodeId = obj.value("nodeId").toString();
    node.parentNodeId = obj.value("parentNodeId").toString();
    node.browseName = obj.value("browseName").toString();
    node.displayName = obj.value("displayName").toString();
    node.description = obj.value("description").toString();

    if (node.kind == NodeKind::Variable) {
        node.dataType = obj.value("dataType").toString();
        node.enumTypeId = obj.value("enumTypeId").toString();
        node.structTypeId = obj.value("structTypeId").toString();
        node.valueRank = obj.value("valueRank").toInt(-1);
        node.arrayDimensions = dimensionsFromJson(obj.value("arrayDimensions").toArray());
        node.writable = obj.value("writable").toBool(false);
        if (obj.contains("initialValue"))
            node.initialValue = obj.value("initialValue").toVariant();

        const QJsonObject sim = obj.value("simulation").toObject();
        node.simulation.kind = simulationKindFromString(sim.value("kind").toString());
        node.simulation.intervalMs = sim.value("intervalMs").toDouble(1000.0);
        node.simulation.min = sim.value("min").toDouble(0.0);
        node.simulation.max = sim.value("max").toDouble(100.0);
        node.simulation.step = sim.value("step").toDouble(1.0);
        node.simulation.periodMs = sim.value("periodMs").toDouble(10000.0);
    }

    const QJsonObject source = obj.value("source").toObject();
    node.source.originalNodeId = source.value("originalNodeId").toString();
    node.source.originalDataTypeId = source.value("originalDataTypeId").toString();
    node.source.originalDataTypeName = source.value("originalDataTypeName").toString();
    node.source.builtInBase = source.value("builtInBase").toString();
    node.source.originalValueRank = source.value("originalValueRank").toInt(-1);
    node.source.substituted = source.value("substituted").toBool(false);
    return node;
}

} // namespace

/*!
 * \brief Serializes \a data to a JSON object.
 */
QJsonObject Serializer::toJson(const ProjectData &data)
{
    QJsonObject root;
    // Version 1 whenever possible, so older builds keep reading the file.
    root["formatVersion"] = needsVersion2(data) ? 2 : 1;
    root["displayName"] = data.displayName;

    QJsonObject server;
    server["applicationUri"] = data.server.applicationUri;
    server["applicationName"] = data.server.applicationName;
    QJsonObject endpoint;
    endpoint["port"] = int(data.server.endpoint.port);
    server["endpoint"] = endpoint;
    root["server"] = server;

    QJsonArray namespaces;
    for (const Namespace &ns : data.namespaces) {
        QJsonObject nsObj;
        nsObj["uri"] = ns.uri;
        namespaces.append(nsObj);
    }
    root["namespaces"] = namespaces;

    QJsonArray enumTypes;
    for (const EnumType &enumType : data.enumTypes) {
        QJsonObject enumObj;
        enumObj["name"] = enumType.name;
        enumObj["nodeId"] = enumType.nodeId;
        QJsonArray entries;
        for (const EnumEntry &entry : enumType.entries) {
            QJsonObject entryObj;
            entryObj["value"] = entry.value;
            entryObj["name"] = entry.name;
            entries.append(entryObj);
        }
        enumObj["entries"] = entries;
        enumTypes.append(enumObj);
    }
    root["enumTypes"] = enumTypes;

    if (!data.structTypes.isEmpty()) {
        QJsonArray structTypes;
        for (const StructType &structType : data.structTypes) {
            QJsonObject structObj;
            structObj["name"] = structType.name;
            structObj["nodeId"] = structType.nodeId;
            if (!structType.originalNodeId.isEmpty())
                structObj["originalNodeId"] = structType.originalNodeId;
            structTypes.append(structObj);
        }
        root["structTypes"] = structTypes;
    }

    QJsonArray nodes;
    for (const Node &node : data.nodes)
        nodes.append(nodeToJson(node));
    root["nodes"] = nodes;

    QJsonArray rules;
    for (const Rule &rule : data.rules) {
        QJsonObject ruleObj;
        ruleObj["triggerNodeId"] = rule.triggerNodeId;
        QJsonArray actions;
        for (const RuleAction &action : rule.actions) {
            QJsonObject actionObj;
            actionObj["targetNodeId"] = action.targetNodeId;
            actionObj["valueMode"] = ruleValueModeToString(action.valueMode);
            if (action.valueMode == RuleValueMode::Literal && action.literalValue.isValid())
                actionObj["literalValue"] = QJsonValue::fromVariant(action.literalValue);
            actionObj["delayMs"] = action.delayMs;
            actions.append(actionObj);
        }
        ruleObj["actions"] = actions;
        rules.append(ruleObj);
    }
    root["rules"] = rules;

    QJsonObject security;
    security["allowAnonymous"] = data.security.allowAnonymous;
    security["allowNone"] = data.security.allowNone;
    security["enableSecurity"] = data.security.enableSecurity;
    security["acceptAllClientCerts"] = data.security.acceptAllClientCerts;
    QJsonArray users;
    for (const UserCredential &user : data.security.users) {
        QJsonObject userObj;
        userObj["username"] = user.username;
        userObj["password"] = user.password;
        users.append(userObj);
    }
    security["users"] = users;
    root["security"] = security;

    return root;
}

/*!
 * \brief Parses \a root into \a data, reporting schema errors in \a errorString.
 */
bool Serializer::fromJson(const QJsonObject &root, ProjectData &data, QString &errorString)
{
    if (!root.contains("formatVersion") || !root.value("formatVersion").isDouble()) {
        errorString = QStringLiteral("Missing or invalid formatVersion.");
        return false;
    }

    data.formatVersion = root.value("formatVersion").toInt();
    data.displayName = root.value("displayName").toString();

    const QJsonObject server = root.value("server").toObject();
    data.server.applicationUri =
        server.value("applicationUri").toString(data.server.applicationUri);
    data.server.applicationName =
        server.value("applicationName").toString(data.server.applicationName);
    const QJsonObject endpoint = server.value("endpoint").toObject();
    data.server.endpoint.port =
        static_cast<quint16>(endpoint.value("port").toInt(data.server.endpoint.port));

    data.namespaces.clear();
    const QJsonArray namespaces = root.value("namespaces").toArray();
    for (const QJsonValue &value : namespaces) {
        Namespace ns;
        ns.uri = value.toObject().value("uri").toString();
        data.namespaces.append(ns);
    }

    data.enumTypes.clear();
    const QJsonArray enumTypes = root.value("enumTypes").toArray();
    for (const QJsonValue &value : enumTypes) {
        const QJsonObject enumObj = value.toObject();
        EnumType enumType;
        enumType.name = enumObj.value("name").toString();
        enumType.nodeId = enumObj.value("nodeId").toString();
        const QJsonArray entries = enumObj.value("entries").toArray();
        for (const QJsonValue &entryValue : entries) {
            const QJsonObject entryObj = entryValue.toObject();
            EnumEntry entry;
            entry.value = entryObj.value("value").toInt();
            entry.name = entryObj.value("name").toString();
            enumType.entries.append(entry);
        }
        data.enumTypes.append(enumType);
    }

    // Structure types are optional (format version 2); older files have none.
    data.structTypes.clear();
    const QJsonArray structTypes = root.value("structTypes").toArray();
    for (const QJsonValue &value : structTypes) {
        const QJsonObject structObj = value.toObject();
        StructType structType;
        structType.name = structObj.value("name").toString();
        structType.nodeId = structObj.value("nodeId").toString();
        structType.originalNodeId = structObj.value("originalNodeId").toString();
        data.structTypes.append(structType);
    }

    data.nodes.clear();
    const QJsonArray nodes = root.value("nodes").toArray();
    for (const QJsonValue &value : nodes)
        data.nodes.append(nodeFromJson(value.toObject()));

    // Rules are optional; older files simply have none.
    data.rules.clear();
    const QJsonArray rules = root.value("rules").toArray();
    for (const QJsonValue &value : rules) {
        const QJsonObject ruleObj = value.toObject();
        Rule rule;
        rule.triggerNodeId = ruleObj.value("triggerNodeId").toString();
        const QJsonArray actions = ruleObj.value("actions").toArray();
        for (const QJsonValue &actionValue : actions) {
            const QJsonObject actionObj = actionValue.toObject();
            RuleAction action;
            action.targetNodeId = actionObj.value("targetNodeId").toString();
            action.valueMode = ruleValueModeFromString(actionObj.value("valueMode").toString());
            if (actionObj.contains("literalValue"))
                action.literalValue = actionObj.value("literalValue").toVariant();
            action.delayMs = actionObj.value("delayMs").toDouble(0.0);
            rule.actions.append(action);
        }
        data.rules.append(rule);
    }

    // The security object is optional; older files fall back to the defaults
    // (anonymous allowed, None endpoint only).
    const QJsonObject security = root.value("security").toObject();
    data.security.allowAnonymous = security.value("allowAnonymous").toBool(true);
    data.security.allowNone = security.value("allowNone").toBool(true);
    data.security.enableSecurity = security.value("enableSecurity").toBool(false);
    // Defaults to true so older files keep the accept-all convenience behavior.
    data.security.acceptAllClientCerts = security.value("acceptAllClientCerts").toBool(true);
    data.security.users.clear();
    const QJsonArray users = security.value("users").toArray();
    for (const QJsonValue &value : users) {
        const QJsonObject userObj = value.toObject();
        UserCredential user;
        user.username = userObj.value("username").toString();
        user.password = userObj.value("password").toString();
        data.security.users.append(user);
    }

    return true;
}

/*!
 * \brief Loads the project at \a filePath.
 */
Serializer::LoadResult Serializer::load(const QString &filePath)
{
    LoadResult result;

    if (!QFileInfo::exists(filePath)) {
        result.error = Error::FileNotFound;
        result.errorString = QStringLiteral("File not found: %1").arg(filePath);
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = Error::ReadFailed;
        result.errorString = QStringLiteral("Cannot read file: %1").arg(file.errorString());
        return result;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = Error::InvalidJson;
        result.errorString = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return result;
    }

    ProjectData data;
    QString schemaError;
    if (!fromJson(doc.object(), data, schemaError)) {
        result.error = Error::InvalidSchema;
        result.errorString = schemaError;
        return result;
    }

    if (data.formatVersion < 1 || data.formatVersion > kServerProjectFormatVersion) {
        result.error = Error::UnsupportedVersion;
        result.errorString = QStringLiteral("Unsupported project version: %1")
                                 .arg(data.formatVersion);
        return result;
    }

    result.ok = true;
    result.data = data;
    return result;
}

/*!
 * \brief Saves \a data to \a filePath atomically.
 */
Serializer::SaveResult Serializer::save(const QString &filePath, const ProjectData &data)
{
    SaveResult result;

    const QDir parentDir = QFileInfo(filePath).absoluteDir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        result.error = Error::WriteFailed;
        result.errorString = QStringLiteral("Cannot create directory: %1")
                                 .arg(parentDir.absolutePath());
        return result;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = Error::WriteFailed;
        result.errorString = QStringLiteral("Cannot open file for writing: %1")
                                 .arg(file.errorString());
        return result;
    }

    const QJsonDocument doc(toJson(data));
    file.write(doc.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        result.error = Error::WriteFailed;
        result.errorString = QStringLiteral("Cannot commit file: %1").arg(file.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace ServerProject
