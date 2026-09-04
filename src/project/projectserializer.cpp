#include "project/projectserializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

/*!
    \class ProjectSerializer
    \brief Reads and writes ProjectData to and from .uaproj JSON files.

    \internal
*/

namespace {

/*!
 * \internal
 * \brief Serializes \a config into a JSON object.
 */
QJsonObject connectionToJson(const ProjectConnectionConfig &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("discoveryUrl"), config.discoveryUrl);
    object.insert(QStringLiteral("backend"), config.backend);
    object.insert(QStringLiteral("server"), config.server);
    object.insert(QStringLiteral("endpoint"), config.endpoint);
    object.insert(QStringLiteral("authMode"), config.authMode);
    object.insert(QStringLiteral("userName"), config.userName);
    object.insert(QStringLiteral("endpointUrlRewriteEnabled"), config.endpointUrlRewriteEnabled);
    return object;
}

/*!
 * \internal
 * \brief Reads a ProjectConnectionConfig from \a object.
 */
ProjectConnectionConfig connectionFromJson(const QJsonObject &object)
{
    ProjectConnectionConfig config;
    config.discoveryUrl = object.value(QStringLiteral("discoveryUrl")).toString();
    config.backend = object.value(QStringLiteral("backend")).toString();
    config.server = object.value(QStringLiteral("server")).toString();
    config.endpoint = object.value(QStringLiteral("endpoint")).toString();
    config.authMode = object.value(QStringLiteral("authMode")).toInt();
    config.userName = object.value(QStringLiteral("userName")).toString();
    config.endpointUrlRewriteEnabled =
        object.value(QStringLiteral("endpointUrlRewriteEnabled")).toBool();
    return config;
}

/*!
 * \internal
 * \brief Serializes \a node into a JSON object.
 */
QJsonObject focusNodeToJson(const ProjectFocusNode &node)
{
    QJsonObject object;
    object.insert(QStringLiteral("nodeId"), node.nodeId);
    object.insert(QStringLiteral("path"), node.path);
    object.insert(QStringLiteral("displayName"), node.displayName);
    return object;
}

/*!
 * \internal
 * \brief Reads a ProjectFocusNode from \a object.
 */
ProjectFocusNode focusNodeFromJson(const QJsonObject &object)
{
    ProjectFocusNode node;
    node.nodeId = object.value(QStringLiteral("nodeId")).toString();
    node.path = object.value(QStringLiteral("path")).toString();
    node.displayName = object.value(QStringLiteral("displayName")).toString();
    return node;
}

/*!
 * \internal
 * \brief Serializes \a record into a JSON object.
 */
QJsonObject monitoredNodeToJson(const MonitoredNodeRecord &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("server"), record.server);
    object.insert(QStringLiteral("nodeId"), record.nodeId);
    object.insert(QStringLiteral("nodePath"), record.nodePath);
    object.insert(QStringLiteral("displayName"), record.displayName);
    object.insert(QStringLiteral("dataType"), record.dataType);
    return object;
}

/*!
 * \internal
 * \brief Reads a MonitoredNodeRecord from \a object.
 */
MonitoredNodeRecord monitoredNodeFromJson(const QJsonObject &object)
{
    MonitoredNodeRecord record;
    record.server = object.value(QStringLiteral("server")).toString();
    record.nodeId = object.value(QStringLiteral("nodeId")).toString();
    record.nodePath = object.value(QStringLiteral("nodePath")).toString();
    record.displayName = object.value(QStringLiteral("displayName")).toString();
    record.dataType = object.value(QStringLiteral("dataType")).toString();
    return record;
}

} // namespace

/*!
 * \brief Serializes \a data into a JSON object.
 */
QJsonObject ProjectSerializer::toJson(const ProjectData &data)
{
    QJsonObject root;
    root.insert(QStringLiteral("formatVersion"), data.formatVersion);
    root.insert(QStringLiteral("displayName"), data.displayName);
    root.insert(QStringLiteral("connection"), connectionToJson(data.connection));
    root.insert(QStringLiteral("focusNode"), focusNodeToJson(data.focusNode));

    QJsonArray nodes;
    for (const MonitoredNodeRecord &record : data.monitoredNodes)
        nodes.append(monitoredNodeToJson(record));
    root.insert(QStringLiteral("monitoredNodes"), nodes);

    QJsonObject settings;
    settings.insert(QStringLiteral("valueFormat"), data.settings.valueFormat);
    root.insert(QStringLiteral("settings"), settings);

    return root;
}

/*!
 * \brief Loads and validates the project file at \a filePath.
 *
 * The file must exist, contain a JSON object, and carry a supported
 * \c formatVersion. On any failure the returned LoadResult has \c ok set to
 * false and describes the reason; the caller decides how to surface it.
 */
ProjectSerializer::LoadResult ProjectSerializer::load(const QString &filePath)
{
    LoadResult result;

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        result.error = Error::FileNotFound;
        result.errorString = errorToString(Error::FileNotFound);
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = Error::ReadFailed;
        result.errorString = errorToString(Error::ReadFailed);
        return result;
    }

    const QByteArray content = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(content, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = Error::InvalidJson;
        result.errorString = parseError.error != QJsonParseError::NoError
                                 ? parseError.errorString()
                                 : errorToString(Error::InvalidJson);
        return result;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("formatVersion"))
        || !root.value(QStringLiteral("formatVersion")).isDouble()) {
        result.error = Error::InvalidSchema;
        result.errorString = errorToString(Error::InvalidSchema);
        return result;
    }

    const int formatVersion = root.value(QStringLiteral("formatVersion")).toInt();
    if (formatVersion < 1 || formatVersion > kProjectFormatVersion) {
        result.error = Error::UnsupportedVersion;
        result.errorString = errorToString(Error::UnsupportedVersion);
        return result;
    }

    ProjectData data;
    data.formatVersion = formatVersion;
    data.displayName = root.value(QStringLiteral("displayName")).toString();
    data.connection = connectionFromJson(root.value(QStringLiteral("connection")).toObject());
    data.focusNode = focusNodeFromJson(root.value(QStringLiteral("focusNode")).toObject());

    const QJsonArray nodes = root.value(QStringLiteral("monitoredNodes")).toArray();
    data.monitoredNodes.reserve(nodes.size());
    for (const QJsonValue &value : nodes) {
        if (value.isObject())
            data.monitoredNodes.append(monitoredNodeFromJson(value.toObject()));
    }

    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    data.settings.valueFormat = settings.value(QStringLiteral("valueFormat")).toInt();

    result.ok = true;
    result.data = data;
    return result;
}

/*!
 * \brief Writes \a data as pretty-printed JSON to \a filePath.
 *
 * Parent directories are created when missing. A QSaveFile is used so a failed
 * write does not leave a partially written project file behind. Returns true on
 * success; on failure returns false and sets \a errorString when it is not null.
 */
bool ProjectSerializer::save(const QString &filePath, const ProjectData &data, QString *errorString)
{
    const QFileInfo info(filePath);
    const QDir directory = info.absoluteDir();
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        if (errorString)
            *errorString = errorToString(Error::WriteFailed);
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorString)
            *errorString = errorToString(Error::WriteFailed);
        return false;
    }

    const QJsonDocument document(toJson(data));
    file.write(document.toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorString)
            *errorString = errorToString(Error::WriteFailed);
        return false;
    }

    return true;
}

/*!
 * \brief Returns a human-readable description of \a error.
 */
QString ProjectSerializer::errorToString(Error error)
{
    switch (error) {
    case Error::None:
        return QString();
    case Error::FileNotFound:
        return QStringLiteral("The project file does not exist.");
    case Error::ReadFailed:
        return QStringLiteral("The project file could not be opened for reading.");
    case Error::InvalidJson:
        return QStringLiteral("The project file does not contain valid JSON.");
    case Error::InvalidSchema:
        return QStringLiteral("The project file is missing required project fields.");
    case Error::UnsupportedVersion:
        return QStringLiteral("The project file uses an unsupported format version.");
    case Error::WriteFailed:
        return QStringLiteral("The project file could not be written.");
    }
    return QString();
}
