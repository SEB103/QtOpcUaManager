#include "serverstudio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include "core/diagnosticslevel.h"
#include "opcuamanager.h"
#include "servernodemodel.h"
#include "serverproject/serverprojectserializer.h"
#include "serverproject/serverprojectvalidator.h"

using ServerProject::Node;
using ServerProject::NodeKind;

namespace {

/*!
 * \internal
 * \brief Returns a local filesystem path from a plain path or a file URL.
 */
QString toLocalPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:")))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

/*!
 * \internal
 * \brief Turns \a text into a node-id-safe slug, defaulting to "Node".
 */
QString slug(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('.'))
            out.append(ch);
        else if (ch.isSpace())
            out.append(QLatin1Char('_'));
    }
    return out.isEmpty() ? QStringLiteral("Node") : out;
}

/*!
 * \internal
 * \brief Converts \a text to a scalar QVariant of \a dataType.
 */
QVariant scalarFromText(const QString &dataType, const QString &text)
{
    const QString trimmed = text.trimmed();
    if (dataType == QLatin1String("Boolean"))
        return trimmed.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
               || trimmed == QLatin1String("1");
    if (dataType == QLatin1String("Float") || dataType == QLatin1String("Double"))
        return trimmed.isEmpty() ? 0.0 : trimmed.toDouble();
    if (dataType == QLatin1String("String"))
        return text;
    if (dataType == QLatin1String("Byte") || dataType == QLatin1String("UInt16")
        || dataType == QLatin1String("UInt32") || dataType == QLatin1String("UInt64"))
        return trimmed.isEmpty() ? qulonglong(0) : trimmed.toULongLong();
    // Remaining integer types.
    return trimmed.isEmpty() ? qlonglong(0) : trimmed.toLongLong();
}

} // namespace

/*!
 * \brief Creates the facade, its runtime controller and its tree model.
 */
ServerStudio::ServerStudio(QObject *parent)
    : QObject(parent)
    , m_nodeModel(new ServerNodeModel(this))
{
    connect(&m_controller, &ServerRuntimeController::stateChanged,
            this, &ServerStudio::stateChanged);
    connect(&m_controller, &ServerRuntimeController::endpointUrlChanged,
            this, &ServerStudio::endpointUrlChanged);
}

ServerStudio::~ServerStudio() = default;

/*!
 * \brief Injects the client \a manager used by openInClient().
 */
void ServerStudio::setOpcUaManager(OpcUaManager *manager)
{
    m_opcUaManager = manager;
}

// --- Runtime lifecycle accessors -------------------------------------------

ServerRuntimeController::State ServerStudio::state() const
{
    return m_controller.state();
}

bool ServerStudio::running() const
{
    return m_controller.state() == ServerRuntimeController::State::Running;
}

bool ServerStudio::busy() const
{
    return m_controller.state() == ServerRuntimeController::State::Starting
           || m_controller.state() == ServerRuntimeController::State::Stopping;
}

bool ServerStudio::crashed() const
{
    return m_controller.state() == ServerRuntimeController::State::Crashed;
}

QString ServerStudio::stateText() const
{
    switch (m_controller.state()) {
    case ServerRuntimeController::State::Stopped:
        return tr("Stopped");
    case ServerRuntimeController::State::Starting:
        return tr("Starting…");
    case ServerRuntimeController::State::Running:
        return tr("Running — %1").arg(m_controller.endpointUrl());
    case ServerRuntimeController::State::Stopping:
        return tr("Stopping…");
    case ServerRuntimeController::State::Crashed:
        return tr("Crashed");
    }
    return tr("Unknown");
}

QString ServerStudio::endpointUrl() const
{
    return m_controller.endpointUrl();
}

// --- Editing accessors ------------------------------------------------------

QObject *ServerStudio::nodeModel() const
{
    return m_nodeModel;
}

QStringList ServerStudio::dataTypeNames() const
{
    return ServerProject::builtinDataTypeNames();
}

void ServerStudio::setSelectedNodeId(const QString &nodeId)
{
    if (m_selectedNodeId == nodeId)
        return;
    m_selectedNodeId = nodeId;
    emit selectedNodeChanged();
}

const Node *ServerStudio::findNode(const QString &nodeId) const
{
    for (const Node &node : m_project.nodes) {
        if (node.nodeId == nodeId)
            return &node;
    }
    return nullptr;
}

QVariantMap ServerStudio::selectedNode() const
{
    QVariantMap map;
    const Node *node = findNode(m_selectedNodeId);
    if (!node)
        return map;

    map["nodeId"] = node->nodeId;
    map["kind"] = int(node->kind);
    map["kindName"] = ServerProject::nodeKindToString(node->kind);
    map["browseName"] = node->browseName;
    map["displayName"] = node->displayName;
    map["description"] = node->description;
    map["isVariable"] = node->isVariable();
    if (node->isVariable()) {
        map["dataType"] = node->dataType;
        map["valueRank"] = node->valueRank;
        map["writable"] = node->writable;
        map["initialValue"] = initialValueToText(node->initialValue);
    }
    return map;
}

// --- Project lifecycle ------------------------------------------------------

void ServerStudio::newProject(const QString &displayName)
{
    const QString name = displayName.trimmed().isEmpty() ? tr("Untitled Server")
                                                         : displayName.trimmed();
    m_project = ServerProject::ProjectData();
    m_project.displayName = name;
    m_project.namespaces.append({QStringLiteral("urn:opcuamanager:%1").arg(slug(name).toLower())});

    m_projectPath.clear();
    m_hasProject = true;
    m_selectedNodeId.clear();
    refreshModel();
    setDirty(true);
    emit projectChanged();
    emit selectedNodeChanged();
}

bool ServerStudio::openProject(const QString &path)
{
    const QString local = toLocalPath(path);
    const ServerProject::Serializer::LoadResult result =
        ServerProject::Serializer::load(local);
    if (!result.ok) {
        emit notification(Diagnostics::Error,
                          tr("Cannot open server project: %1").arg(result.errorString));
        return false;
    }

    m_project = result.data;
    m_projectPath = local;
    m_hasProject = true;
    m_selectedNodeId.clear();
    m_dirty = false;
    refreshModel();
    emit projectChanged();
    emit dirtyChanged();
    emit selectedNodeChanged();
    emit notification(Diagnostics::Info, tr("Opened server project %1").arg(m_project.displayName));
    return true;
}

bool ServerStudio::saveProject()
{
    if (m_projectPath.isEmpty())
        return false;
    const ServerProject::Serializer::SaveResult result =
        ServerProject::Serializer::save(m_projectPath, m_project);
    if (!result.ok) {
        emit notification(Diagnostics::Error,
                          tr("Cannot save server project: %1").arg(result.errorString));
        return false;
    }
    setDirty(false);
    return true;
}

bool ServerStudio::saveProjectAs(const QString &path)
{
    const QString local = toLocalPath(path);
    const ServerProject::Serializer::SaveResult result =
        ServerProject::Serializer::save(local, m_project);
    if (!result.ok) {
        emit notification(Diagnostics::Error,
                          tr("Cannot save server project: %1").arg(result.errorString));
        return false;
    }
    m_projectPath = local;
    setDirty(false);
    emit projectChanged();
    return true;
}

void ServerStudio::closeProject()
{
    if (m_controller.state() != ServerRuntimeController::State::Stopped)
        m_controller.stop();

    m_project = ServerProject::ProjectData();
    m_hasProject = false;
    m_projectPath.clear();
    m_selectedNodeId.clear();
    m_dirty = false;
    refreshModel();
    emit projectChanged();
    emit dirtyChanged();
    emit selectedNodeChanged();
}

// --- Address-space editing --------------------------------------------------

QString ServerStudio::makeUniqueNodeId(const QString &base) const
{
    const QString baseSlug = slug(base);
    QString candidate = QStringLiteral("ns=1;s=%1").arg(baseSlug);
    int suffix = 2;
    while (findNode(candidate) != nullptr) {
        candidate = QStringLiteral("ns=1;s=%1_%2").arg(baseSlug).arg(suffix++);
    }
    return candidate;
}

QString ServerStudio::addFolder(const QString &parentNodeId, const QString &browseName)
{
    if (!m_hasProject)
        return {};
    Node node;
    node.kind = NodeKind::Folder;
    node.nodeId = makeUniqueNodeId(browseName);
    node.parentNodeId = parentNodeId;
    node.browseName = browseName.trimmed().isEmpty() ? tr("Folder") : browseName.trimmed();
    node.displayName = node.browseName;
    m_project.nodes.append(node);
    refreshModel();
    setDirty(true);
    setSelectedNodeId(node.nodeId);
    return node.nodeId;
}

QString ServerStudio::addObject(const QString &parentNodeId, const QString &browseName)
{
    if (!m_hasProject)
        return {};
    Node node;
    node.kind = NodeKind::Object;
    node.nodeId = makeUniqueNodeId(browseName);
    node.parentNodeId = parentNodeId;
    node.browseName = browseName.trimmed().isEmpty() ? tr("Object") : browseName.trimmed();
    node.displayName = node.browseName;
    m_project.nodes.append(node);
    refreshModel();
    setDirty(true);
    setSelectedNodeId(node.nodeId);
    return node.nodeId;
}

QString ServerStudio::addVariable(const QString &parentNodeId, const QString &browseName,
                                  const QString &dataType, int valueRank, bool writable)
{
    if (!m_hasProject)
        return {};
    Node node;
    node.kind = NodeKind::Variable;
    node.nodeId = makeUniqueNodeId(browseName);
    node.parentNodeId = parentNodeId;
    node.browseName = browseName.trimmed().isEmpty() ? tr("Variable") : browseName.trimmed();
    node.displayName = node.browseName;
    node.dataType = dataTypeNames().contains(dataType) ? dataType : QStringLiteral("Double");
    node.valueRank = (valueRank == 1) ? 1 : -1;
    node.writable = writable;
    node.initialValue = (node.valueRank == 1) ? QVariant(QVariantList())
                                              : scalarFromText(node.dataType, QString());
    m_project.nodes.append(node);
    refreshModel();
    setDirty(true);
    setSelectedNodeId(node.nodeId);
    return node.nodeId;
}

void ServerStudio::updateNode(const QString &nodeId, const QVariantMap &fields)
{
    for (Node &node : m_project.nodes) {
        if (node.nodeId != nodeId)
            continue;

        if (fields.contains(QStringLiteral("displayName")))
            node.displayName = fields.value(QStringLiteral("displayName")).toString();
        if (fields.contains(QStringLiteral("description")))
            node.description = fields.value(QStringLiteral("description")).toString();

        if (node.isVariable()) {
            if (fields.contains(QStringLiteral("dataType"))) {
                const QString type = fields.value(QStringLiteral("dataType")).toString();
                if (dataTypeNames().contains(type))
                    node.dataType = type;
            }
            if (fields.contains(QStringLiteral("valueRank")))
                node.valueRank = fields.value(QStringLiteral("valueRank")).toInt() == 1 ? 1 : -1;
            if (fields.contains(QStringLiteral("writable")))
                node.writable = fields.value(QStringLiteral("writable")).toBool();
            if (fields.contains(QStringLiteral("initialValue"))) {
                node.initialValue = parseInitialValue(
                    node.dataType, node.valueRank,
                    fields.value(QStringLiteral("initialValue")).toString());
            }
        }

        refreshModel();
        setDirty(true);
        emit selectedNodeChanged();
        return;
    }
}

void ServerStudio::removeNode(const QString &nodeId)
{
    // Collect the node and every descendant by walking parent references.
    QSet<QString> toRemove{nodeId};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const Node &node : m_project.nodes) {
            if (!toRemove.contains(node.nodeId) && toRemove.contains(node.parentNodeId)) {
                toRemove.insert(node.nodeId);
                grew = true;
            }
        }
    }

    QList<Node> kept;
    kept.reserve(m_project.nodes.size());
    for (const Node &node : m_project.nodes) {
        if (!toRemove.contains(node.nodeId))
            kept.append(node);
    }
    m_project.nodes = kept;

    if (toRemove.contains(m_selectedNodeId))
        m_selectedNodeId.clear();

    refreshModel();
    setDirty(true);
    emit selectedNodeChanged();
}

// --- Runtime control --------------------------------------------------------

void ServerStudio::startServer()
{
    if (!m_hasProject) {
        emit notification(Diagnostics::Warning, tr("Create or open a server project first."));
        return;
    }

    const ServerProject::Validator::Result validation =
        ServerProject::Validator::validate(m_project);
    if (!validation.ok) {
        emit notification(Diagnostics::Error,
                          tr("Project is not valid: %1")
                              .arg(validation.errors.join(QStringLiteral("; "))));
        return;
    }

    const QString snapshot = writeRuntimeSnapshot();
    if (snapshot.isEmpty()) {
        emit notification(Diagnostics::Error, tr("Could not prepare the server project."));
        return;
    }

    m_controller.start(m_project.server.endpoint.port, snapshot);
}

void ServerStudio::stop()
{
    m_controller.stop();
}

void ServerStudio::restart()
{
    m_controller.restart();
}

void ServerStudio::kill()
{
    m_controller.kill();
}

void ServerStudio::openInClient()
{
    if (!running() || m_controller.endpointUrl().isEmpty()) {
        emit notification(Diagnostics::Warning,
                          tr("Start the server runtime before opening it in the client."));
        return;
    }
    if (!m_opcUaManager) {
        emit notification(Diagnostics::Error, tr("No OPC UA client is available."));
        return;
    }
    m_opcUaManager->connectToLocalEndpoint(m_controller.endpointUrl());
    emit openInClientRequested();
}

// --- Private helpers --------------------------------------------------------

void ServerStudio::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void ServerStudio::refreshModel()
{
    m_nodeModel->setNodes(m_project.nodes);
}

QVariant ServerStudio::parseInitialValue(const QString &dataType, int valueRank, const QString &text)
{
    if (valueRank == 1) {
        QVariantList list;
        const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts)
            list.append(scalarFromText(dataType, part));
        return list;
    }
    return scalarFromText(dataType, text);
}

QString ServerStudio::initialValueToText(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::QVariantList) {
        QStringList parts;
        const QVariantList list = value.toList();
        for (const QVariant &item : list)
            parts.append(item.toString());
        return parts.join(QStringLiteral(", "));
    }
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.toString();
}

QString ServerStudio::writeRuntimeSnapshot()
{
    // A saved, clean project can be served directly; otherwise snapshot the
    // current edits to a temp file so the running server matches the editor.
    if (!m_projectPath.isEmpty() && !m_dirty)
        return m_projectPath;

    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QDir(tempDir).filePath(
        QStringLiteral("opcuamanager-runtime-%1.uaserver")
            .arg(QCoreApplication::applicationPid()));
    const ServerProject::Serializer::SaveResult result =
        ServerProject::Serializer::save(path, m_project);
    return result.ok ? path : QString();
}
