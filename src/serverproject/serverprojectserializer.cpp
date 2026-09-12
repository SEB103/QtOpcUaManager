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
        obj["valueRank"] = node.valueRank;
        obj["writable"] = node.writable;
        if (node.initialValue.isValid())
            obj["initialValue"] = QJsonValue::fromVariant(node.initialValue);
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
        node.valueRank = obj.value("valueRank").toInt(-1);
        node.writable = obj.value("writable").toBool(false);
        if (obj.contains("initialValue"))
            node.initialValue = obj.value("initialValue").toVariant();
    }
    return node;
}

} // namespace

/*!
 * \brief Serializes \a data to a JSON object.
 */
QJsonObject Serializer::toJson(const ProjectData &data)
{
    QJsonObject root;
    root["formatVersion"] = data.formatVersion;
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

    QJsonArray nodes;
    for (const Node &node : data.nodes)
        nodes.append(nodeToJson(node));
    root["nodes"] = nodes;

    QJsonObject security;
    security["allowAnonymous"] = data.security.allowAnonymous;
    security["allowNone"] = data.security.allowNone;
    security["enableSecurity"] = data.security.enableSecurity;
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

    data.nodes.clear();
    const QJsonArray nodes = root.value("nodes").toArray();
    for (const QJsonValue &value : nodes)
        data.nodes.append(nodeFromJson(value.toObject()));

    // The security object is optional; older files fall back to the defaults
    // (anonymous allowed, None endpoint only).
    const QJsonObject security = root.value("security").toObject();
    data.security.allowAnonymous = security.value("allowAnonymous").toBool(true);
    data.security.allowNone = security.value("allowNone").toBool(true);
    data.security.enableSecurity = security.value("enableSecurity").toBool(false);
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
