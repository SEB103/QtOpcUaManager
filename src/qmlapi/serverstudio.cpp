#include "serverstudio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include "core/diagnosticslevel.h"
#include "models/dataaccessmodel.h"
#include "models/opcuamodel.h"
#include "opcuamanager.h"
#include "servernodemodel.h"
#include "serverproject/serverprojectnodeset.h"
#include "serverproject/serverprojectserializer.h"
#include "serverproject/serverprojectvalidator.h"

using ServerProject::Node;
using ServerProject::NodeKind;
using ServerProject::Rule;

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

/*!
 * \internal
 * \brief Maps a namespace-0 DataType node id to a built-in type name.
 *
 * Unknown or custom data types fall back to Int32 for a structural clone.
 */
QString dataTypeIdToBuiltin(const QString &dataTypeId)
{
    static const QHash<int, QString> map = {
        {1, QStringLiteral("Boolean")}, {2, QStringLiteral("SByte")},
        {3, QStringLiteral("Byte")},    {4, QStringLiteral("Int16")},
        {5, QStringLiteral("UInt16")},  {6, QStringLiteral("Int32")},
        {7, QStringLiteral("UInt32")},  {8, QStringLiteral("Int64")},
        {9, QStringLiteral("UInt64")},  {10, QStringLiteral("Float")},
        {11, QStringLiteral("Double")}, {12, QStringLiteral("String")},
    };
    if (dataTypeId.startsWith(QLatin1String("ns=0;i=")))
        return map.value(dataTypeId.mid(7).toInt(), QStringLiteral("Int32"));
    return QStringLiteral("Int32");
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
    // The running state gates restartRequired(), so a state change may flip it.
    connect(&m_controller, &ServerRuntimeController::stateChanged,
            this, &ServerStudio::restartRequiredChanged);
    connect(&m_controller, &ServerRuntimeController::endpointUrlChanged,
            this, &ServerStudio::endpointUrlChanged);
    connect(&m_controller, &ServerRuntimeController::diagnosticsChanged,
            this, &ServerStudio::diagnosticsChanged);
}

ServerStudio::~ServerStudio() = default;

/*!
 * \brief Rebuilds the last notification for the status bar after a language switch.
 *
 * Emits statusRetranslated() with the last message re-rendered in the active
 * language. The transient banner is intentionally not re-raised.
 */
void ServerStudio::retranslate()
{
    if (m_lastStatus.isValid())
        emit statusRetranslated(m_lastStatus.level, m_lastStatus.render());
}

/*!
 * \internal
 * \brief Emits \a render's text at \a level and stores the renderer for retranslate().
 *
 * The renderer captures its runtime arguments by value, so re-invoking it later
 * re-runs its tr() calls in the active language.
 */
void ServerStudio::notify(Diagnostics::Level level, std::function<QString()> render)
{
    m_lastStatus.level = level;
    m_lastStatus.render = std::move(render);
    emit notification(level, m_lastStatus.render());
}

/*!
 * \brief Injects the client \a manager used by openInClient().
 */
void ServerStudio::setOpcUaManager(OpcUaManager *manager)
{
    m_opcUaManager = manager;
    if (m_opcUaManager) {
        connect(m_opcUaManager, &OpcUaManager::cloneSnapshotReady, this,
                [this](quint64 /*requestId*/, const QList<CloneNode> &nodes,
                       const QStringList &namespaceUris, bool success, bool truncated) {
                    applyCloneSnapshot(nodes, namespaceUris, success, truncated);
                });
    }
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

bool ServerStudio::restartRequired() const
{
    return running() && m_configChangedSinceStart;
}

int ServerStudio::sessionCount() const
{
    return m_controller.sessionCount();
}

int ServerStudio::secureChannelCount() const
{
    return m_controller.secureChannelCount();
}

QString ServerStudio::diagnosticsText() const
{
    if (!running())
        return tr("No diagnostics (server stopped).");
    const qint64 seconds = m_controller.uptimeMs() / 1000;
    return tr("Sessions: %1 · Secure channels: %2 · Uptime: %3 s")
        .arg(m_controller.sessionCount())
        .arg(m_controller.secureChannelCount())
        .arg(seconds);
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

QStringList ServerStudio::simulationKindNames() const
{
    return ServerProject::simulationKindNames();
}

QStringList ServerStudio::enumTypeNames() const
{
    QStringList names;
    for (const ServerProject::EnumType &enumType : m_project.enumTypes)
        names.append(enumType.name);
    return names;
}

QVariantList ServerStudio::enumTypes() const
{
    QVariantList list;
    for (const ServerProject::EnumType &enumType : m_project.enumTypes) {
        QVariantMap map;
        map["name"] = enumType.name;
        map["nodeId"] = enumType.nodeId;
        QVariantList entries;
        for (const ServerProject::EnumEntry &entry : enumType.entries) {
            QVariantMap entryMap;
            entryMap["value"] = entry.value;
            entryMap["name"] = entry.name;
            entries.append(entryMap);
        }
        map["entries"] = entries;
        list.append(map);
    }
    return list;
}

QString ServerStudio::addEnumType(const QString &name)
{
    if (!m_hasProject)
        return {};
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};
    for (const ServerProject::EnumType &existing : m_project.enumTypes) {
        if (existing.name == trimmed)
            return existing.nodeId;
    }

    ServerProject::EnumType enumType;
    enumType.name = trimmed;
    QString nodeId = QStringLiteral("ns=1;s=Enum.%1").arg(slug(trimmed));
    int suffix = 2;
    const auto idTaken = [this](const QString &id) {
        for (const ServerProject::EnumType &e : m_project.enumTypes)
            if (e.nodeId == id)
                return true;
        return false;
    };
    while (idTaken(nodeId))
        nodeId = QStringLiteral("ns=1;s=Enum.%1_%2").arg(slug(trimmed)).arg(suffix++);
    enumType.nodeId = nodeId;

    m_project.enumTypes.append(enumType);
    setDirty(true);
    emit enumsChanged();
    return nodeId;
}

void ServerStudio::addEnumEntry(const QString &enumName, int value, const QString &entryName)
{
    if (!m_hasProject || entryName.trimmed().isEmpty())
        return;
    for (ServerProject::EnumType &enumType : m_project.enumTypes) {
        if (enumType.name == enumName) {
            enumType.entries.append({value, entryName.trimmed()});
            setDirty(true);
            emit enumsChanged();
            return;
        }
    }
}

void ServerStudio::removeEnumType(const QString &enumName)
{
    if (!m_hasProject)
        return;
    QString removedId;
    for (const ServerProject::EnumType &enumType : m_project.enumTypes) {
        if (enumType.name == enumName) {
            removedId = enumType.nodeId;
            break;
        }
    }
    if (removedId.isEmpty())
        return;

    m_project.enumTypes.removeIf([&enumName](const ServerProject::EnumType &e) {
        return e.name == enumName;
    });
    // Detach variables that referenced the removed enum; fall back to Int32.
    for (ServerProject::Node &node : m_project.nodes) {
        if (node.enumTypeId == removedId) {
            node.enumTypeId.clear();
            if (node.dataType.isEmpty())
                node.dataType = QStringLiteral("Int32");
        }
    }

    setDirty(true);
    refreshModel();
    emit enumsChanged();
    emit selectedNodeChanged();
}

// --- Behavior rules ---------------------------------------------------------

QVariantList ServerStudio::rules() const
{
    QVariantList list;
    for (const ServerProject::Rule &rule : m_project.rules) {
        QVariantMap map;
        map["triggerNodeId"] = rule.triggerNodeId;
        QVariantList actions;
        for (const ServerProject::RuleAction &action : rule.actions) {
            QVariantMap actionMap;
            actionMap["targetNodeId"] = action.targetNodeId;
            actionMap["valueMode"] = ServerProject::ruleValueModeToString(action.valueMode);
            actionMap["literalValue"] = initialValueToText(action.literalValue);
            actionMap["delayMs"] = action.delayMs;
            actions.append(actionMap);
        }
        map["actions"] = actions;
        list.append(map);
    }
    return list;
}

QStringList ServerStudio::variableNodeIds() const
{
    QStringList ids;
    for (const Node &node : m_project.nodes) {
        if (node.isVariable())
            ids.append(node.nodeId);
    }
    return ids;
}

QStringList ServerStudio::ruleValueModeNames() const
{
    return {QStringLiteral("Literal"), QStringLiteral("CopyTrigger")};
}

int ServerStudio::addRule(const QString &triggerNodeId)
{
    if (!m_hasProject)
        return -1;
    ServerProject::Rule rule;
    rule.triggerNodeId = triggerNodeId;
    m_project.rules.append(rule);
    setDirty(true);
    emit rulesChanged();
    return int(m_project.rules.size()) - 1;
}

void ServerStudio::removeRule(int index)
{
    if (index < 0 || index >= m_project.rules.size())
        return;
    m_project.rules.removeAt(index);
    setDirty(true);
    emit rulesChanged();
}

void ServerStudio::setRuleTrigger(int index, const QString &triggerNodeId)
{
    if (index < 0 || index >= m_project.rules.size())
        return;
    m_project.rules[index].triggerNodeId = triggerNodeId;
    setDirty(true);
    emit rulesChanged();
}

void ServerStudio::addRuleAction(int ruleIndex, const QString &targetNodeId)
{
    if (ruleIndex < 0 || ruleIndex >= m_project.rules.size())
        return;
    ServerProject::RuleAction action;
    action.targetNodeId = targetNodeId;
    // Seed the literal with the target's typed zero so a Literal write is well
    // formed even before the user edits it.
    const Node *target = findNode(targetNodeId);
    if (target && target->isVariable())
        action.literalValue = scalarFromText(target->dataType, QString());
    m_project.rules[ruleIndex].actions.append(action);
    setDirty(true);
    emit rulesChanged();
}

void ServerStudio::removeRuleAction(int ruleIndex, int actionIndex)
{
    if (ruleIndex < 0 || ruleIndex >= m_project.rules.size())
        return;
    QList<ServerProject::RuleAction> &actions = m_project.rules[ruleIndex].actions;
    if (actionIndex < 0 || actionIndex >= actions.size())
        return;
    actions.removeAt(actionIndex);
    setDirty(true);
    emit rulesChanged();
}

void ServerStudio::updateRuleAction(int ruleIndex, int actionIndex, const QVariantMap &fields)
{
    if (ruleIndex < 0 || ruleIndex >= m_project.rules.size())
        return;
    QList<ServerProject::RuleAction> &actions = m_project.rules[ruleIndex].actions;
    if (actionIndex < 0 || actionIndex >= actions.size())
        return;
    ServerProject::RuleAction &action = actions[actionIndex];

    if (fields.contains(QStringLiteral("targetNodeId")))
        action.targetNodeId = fields.value(QStringLiteral("targetNodeId")).toString();
    if (fields.contains(QStringLiteral("valueMode"))) {
        action.valueMode = ServerProject::ruleValueModeFromString(
            fields.value(QStringLiteral("valueMode")).toString());
    }
    if (fields.contains(QStringLiteral("delayMs")))
        action.delayMs = fields.value(QStringLiteral("delayMs")).toDouble();
    if (fields.contains(QStringLiteral("literalValue"))) {
        // Parse to the target's type so "false"/"0" become a typed value the
        // runtime writes correctly (a bare QVariant string would mis-coerce).
        const Node *target = findNode(action.targetNodeId);
        const QString type = target ? target->dataType : QStringLiteral("Double");
        action.literalValue =
            scalarFromText(type, fields.value(QStringLiteral("literalValue")).toString());
    }
    setDirty(true);
    emit rulesChanged();
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
        map["simulationKind"] = ServerProject::simulationKindToString(node->simulation.kind);
        map["simInterval"] = node->simulation.intervalMs;
        map["simMin"] = node->simulation.min;
        map["simMax"] = node->simulation.max;
        map["simStep"] = node->simulation.step;
        map["simPeriod"] = node->simulation.periodMs;

        QString enumName;
        if (!node->enumTypeId.isEmpty()) {
            for (const ServerProject::EnumType &enumType : m_project.enumTypes) {
                if (enumType.nodeId == node->enumTypeId) {
                    enumName = enumType.name;
                    break;
                }
            }
        }
        map["enumTypeName"] = enumName;
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
    emit enumsChanged();
    emit rulesChanged();
    emit securityChanged();
    emit selectedNodeChanged();
}

bool ServerStudio::openProject(const QString &path)
{
    const QString local = toLocalPath(path);
    const ServerProject::Serializer::LoadResult result =
        ServerProject::Serializer::load(local);
    if (!result.ok) {
        notify(Diagnostics::Error, [this, error = result.errorString] {
            return tr("Cannot open server project: %1").arg(error);
        });
        return false;
    }

    m_project = result.data;
    m_projectPath = local;
    m_hasProject = true;
    m_selectedNodeId.clear();
    m_dirty = false;
    refreshModel();
    emit projectChanged();
    emit enumsChanged();
    emit rulesChanged();
    emit securityChanged();
    emit dirtyChanged();
    emit selectedNodeChanged();
    notify(Diagnostics::Info, [this, name = m_project.displayName] {
        return tr("Opened server project %1").arg(name);
    });
    return true;
}

bool ServerStudio::saveProject()
{
    if (m_projectPath.isEmpty())
        return false;
    const ServerProject::Serializer::SaveResult result =
        ServerProject::Serializer::save(m_projectPath, m_project);
    if (!result.ok) {
        notify(Diagnostics::Error, [this, error = result.errorString] {
            return tr("Cannot save server project: %1").arg(error);
        });
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
        notify(Diagnostics::Error, [this, error = result.errorString] {
            return tr("Cannot save server project: %1").arg(error);
        });
        return false;
    }
    m_projectPath = local;
    setDirty(false);
    emit projectChanged();
    emit enumsChanged();
    emit securityChanged();
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
    emit enumsChanged();
    emit rulesChanged();
    emit securityChanged();
    emit dirtyChanged();
    emit selectedNodeChanged();
}

bool ServerStudio::exportNodeSet(const QString &path)
{
    if (!m_hasProject) {
        notify(Diagnostics::Warning, [this] {
            return tr("Open a server project first.");
        });
        return false;
    }
    const ServerProject::NodeSet::Result result =
        ServerProject::NodeSet::exportToFile(toLocalPath(path), m_project);
    if (!result.ok) {
        notify(Diagnostics::Error, [this, error = result.errorString] {
            return tr("Cannot export NodeSet2: %1").arg(error);
        });
        return false;
    }
    notify(Diagnostics::Info, [this] {
        return tr("Exported NodeSet2.");
    });
    return true;
}

bool ServerStudio::importNodeSet(const QString &path)
{
    const ServerProject::NodeSet::ImportResult result =
        ServerProject::NodeSet::importFromFile(toLocalPath(path));
    if (!result.ok) {
        notify(Diagnostics::Error, [this, error = result.errorString] {
            return tr("Cannot import NodeSet2: %1").arg(error);
        });
        return false;
    }

    if (!m_hasProject)
        newProject(tr("Imported Server"));

    // Replace the designed address space; keep the default namespace when the
    // imported file declared none, so node-id namespace 1 still resolves.
    if (!result.namespaces.isEmpty())
        m_project.namespaces = result.namespaces;
    m_project.enumTypes = result.enumTypes;
    m_project.nodes = result.nodes;
    // The imported file replaces the address space, so any prior rules would
    // dangle; drop them.
    m_project.rules.clear();
    m_selectedNodeId.clear();

    refreshModel();
    setDirty(true);
    emit projectChanged();
    emit enumsChanged();
    emit rulesChanged();
    emit selectedNodeChanged();

    if (result.skippedCount > 0) {
        // Report exactly which node classes the model could not represent.
        const auto kindLabel = [this](const QString &kind) -> QString {
            if (kind == QLatin1String("UAMethod"))
                return tr("Methods");
            if (kind == QLatin1String("UAObjectType"))
                return tr("Object types");
            if (kind == QLatin1String("UAVariableType"))
                return tr("Variable types");
            if (kind == QLatin1String("UAReferenceType"))
                return tr("Reference types");
            if (kind == QLatin1String("UAView"))
                return tr("Views");
            if (kind == QLatin1String("UADataType"))
                return tr("Data types");
            return kind;
        };
        QStringList kinds = result.skippedKinds.keys();
        kinds.sort();
        QStringList parts;
        for (const QString &kind : kinds)
            parts.append(QStringLiteral("%1: %2")
                             .arg(kindLabel(kind))
                             .arg(result.skippedKinds.value(kind)));
        notify(Diagnostics::Info,
               [this, n = int(result.nodes.size()), skipped = result.skippedCount,
                detail = parts.join(QStringLiteral(", "))] {
                   return tr("Imported %1 node(s); skipped %2 (%3).")
                       .arg(n)
                       .arg(skipped)
                       .arg(detail);
               });
    } else {
        notify(Diagnostics::Info, [this, n = int(result.nodes.size())] {
            return tr("Imported %1 node(s) from NodeSet2.").arg(n);
        });
    }
    return true;
}

bool ServerStudio::cloneFromClient(bool preserveOriginalIds)
{
    if (!m_opcUaManager) {
        notify(Diagnostics::Error, [this] {
            return tr("No OPC UA client is available.");
        });
        return false;
    }
    if (m_cloneInProgress) {
        notify(Diagnostics::Warning, [this] {
            return tr("A clone is already in progress.");
        });
        return false;
    }

    m_clonePreserveIds = preserveOriginalIds;
    m_cloneInProgress = true;
    notify(Diagnostics::Info, [this] {
        return tr("Cloning the server address space…");
    });
    m_opcUaManager->requestCloneSnapshot();
    return true;
}

void ServerStudio::applyCloneSnapshot(const QList<CloneNode> &nodes,
                                      const QStringList &namespaceUris, bool success, bool truncated)
{
    m_cloneInProgress = false;

    if (!success || nodes.isEmpty()) {
        notify(Diagnostics::Warning, [this] {
            return tr("Nothing was cloned. Connect to a server and try again.");
        });
        return;
    }

    ServerProject::ProjectData project;
    project.displayName = tr("Cloned Server");

    // In preserve-ids mode, map each original namespace index used by the nodes
    // to a project namespace slot, keeping the original URI. Otherwise, all nodes
    // live in a single synthetic clone namespace.
    QHash<quint16, quint16> nsIndexMap; // original ns index -> project ns index (1..N)
    if (m_clonePreserveIds) {
        for (const CloneNode &source : nodes) {
            const ServerProject::ParsedNodeId parsed = ServerProject::parseNodeId(source.nodeId);
            if (!parsed.valid || parsed.ns == 0 || nsIndexMap.contains(parsed.ns))
                continue;
            const QString uri = parsed.ns < namespaceUris.size()
                                    ? namespaceUris.at(parsed.ns)
                                    : QStringLiteral("urn:opcuamanager:clone-ns%1").arg(parsed.ns);
            project.namespaces.append({uri});
            nsIndexMap.insert(parsed.ns, static_cast<quint16>(project.namespaces.size()));
        }
    } else {
        project.namespaces.append({QStringLiteral("urn:opcuamanager:clone")});
    }

    // Remaps an original node id to the project namespace, preserving the
    // identifier (preserve-ids mode only).
    const auto remapId = [&nsIndexMap](const QString &originalId) -> QString {
        const ServerProject::ParsedNodeId parsed = ServerProject::parseNodeId(originalId);
        if (!parsed.valid)
            return originalId;
        const quint16 ns = parsed.ns == 0 ? 0 : nsIndexMap.value(parsed.ns, parsed.ns);
        return QStringLiteral("ns=%1;%2=%3")
            .arg(ns)
            .arg(parsed.numeric ? QLatin1Char('i') : QLatin1Char('s'))
            .arg(parsed.identifier);
    };

    QHash<QString, QString> idMap;   // original node id -> cloned node id
    QHash<QString, QString> pathMap; // original node id -> browse-name path (slug mode)
    QSet<QString> usedIds;

    for (const CloneNode &source : nodes) {
        // Only folders/objects and variables are cloneable.
        constexpr int kObject = 1;
        constexpr int kVariable = 2;
        if (source.nodeClass != kObject && source.nodeClass != kVariable)
            continue;

        const QString segment = slug(source.browseName.isEmpty() ? source.displayName
                                                                 : source.browseName);

        QString nodeId;
        if (m_clonePreserveIds) {
            nodeId = remapId(source.nodeId);
        } else {
            const QString parentPath =
                source.parentNodeId.isEmpty() ? QString() : pathMap.value(source.parentNodeId);
            const QString path =
                parentPath.isEmpty() ? segment : (parentPath + QLatin1Char('.') + segment);
            nodeId = QStringLiteral("ns=1;s=%1").arg(path);
            int suffix = 2;
            while (usedIds.contains(nodeId))
                nodeId = QStringLiteral("ns=1;s=%1_%2").arg(path).arg(suffix++);
            usedIds.insert(nodeId);
            pathMap.insert(source.nodeId, path);
        }
        idMap.insert(source.nodeId, nodeId);

        Node node;
        node.nodeId = nodeId;
        node.parentNodeId =
            source.parentNodeId.isEmpty() ? QString() : idMap.value(source.parentNodeId);
        node.browseName = source.browseName.isEmpty() ? segment : source.browseName;
        node.displayName = source.displayName.isEmpty() ? node.browseName : source.displayName;
        if (source.isVariable) {
            node.kind = NodeKind::Variable;
            node.dataType = dataTypeIdToBuiltin(source.dataTypeId);
            node.valueRank = source.valueRank >= 1 ? 1 : -1;
            // Writable by default so the cloned server can be edited/exercised.
            node.writable = true;
            // Coerce the captured value to the built-in type, keeping it JSON-safe
            // (an ExtensionObject or custom type falls back to the type default).
            if (node.valueRank == 1) {
                QVariantList out;
                const QVariantList elements = source.value.toList();
                for (const QVariant &element : elements)
                    out.append(scalarFromText(node.dataType, element.toString()));
                node.initialValue = out;
            } else {
                node.initialValue = scalarFromText(node.dataType, source.value.toString());
            }
        } else {
            node.kind = source.isFolder ? NodeKind::Folder : NodeKind::Object;
        }
        project.nodes.append(node);
    }

    m_project = project;
    m_projectPath.clear();
    m_hasProject = true;
    m_selectedNodeId.clear();
    refreshModel();
    setDirty(true);
    emit projectChanged();
    emit enumsChanged();
    emit rulesChanged();
    emit securityChanged();
    emit selectedNodeChanged();

    if (truncated) {
        notify(Diagnostics::Warning, [this, n = int(project.nodes.size())] {
            return tr("Cloned %1 node(s); the address space was large and was truncated.")
                .arg(n);
        });
    } else {
        notify(Diagnostics::Info, [this, n = int(project.nodes.size())] {
            return tr("Cloned %1 node(s) from the server.").arg(n);
        });
    }
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
    // The browse name is persisted project data, not UI text, so the default is a
    // fixed identifier independent of the UI language.
    node.browseName = browseName.trimmed().isEmpty() ? QStringLiteral("Folder") : browseName.trimmed();
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
    // The browse name is persisted project data, not UI text, so the default is a
    // fixed identifier independent of the UI language.
    node.browseName = browseName.trimmed().isEmpty() ? QStringLiteral("Object") : browseName.trimmed();
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
    // The browse name is persisted project data, not UI text, so the default is a
    // fixed identifier independent of the UI language.
    node.browseName = browseName.trimmed().isEmpty() ? QStringLiteral("Variable") : browseName.trimmed();
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
            if (fields.contains(QStringLiteral("simulationKind"))) {
                node.simulation.kind = ServerProject::simulationKindFromString(
                    fields.value(QStringLiteral("simulationKind")).toString());
            }
            if (fields.contains(QStringLiteral("simInterval")))
                node.simulation.intervalMs = fields.value(QStringLiteral("simInterval")).toDouble();
            if (fields.contains(QStringLiteral("simMin")))
                node.simulation.min = fields.value(QStringLiteral("simMin")).toDouble();
            if (fields.contains(QStringLiteral("simMax")))
                node.simulation.max = fields.value(QStringLiteral("simMax")).toDouble();
            if (fields.contains(QStringLiteral("simStep")))
                node.simulation.step = fields.value(QStringLiteral("simStep")).toDouble();
            if (fields.contains(QStringLiteral("simPeriod")))
                node.simulation.periodMs = fields.value(QStringLiteral("simPeriod")).toDouble();
            if (fields.contains(QStringLiteral("enumTypeName"))) {
                const QString enumName = fields.value(QStringLiteral("enumTypeName")).toString();
                node.enumTypeId.clear();
                for (const ServerProject::EnumType &enumType : m_project.enumTypes) {
                    if (enumType.name == enumName) {
                        node.enumTypeId = enumType.nodeId;
                        break;
                    }
                }
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

    // Drop rules that reference a removed node so no rule dangles: prune removed
    // action targets, then drop any rule whose trigger is gone or that has no
    // actions left.
    bool rulesTouched = false;
    for (int i = m_project.rules.size() - 1; i >= 0; --i) {
        Rule &rule = m_project.rules[i];
        const qsizetype before = rule.actions.size();
        rule.actions.removeIf([&toRemove](const ServerProject::RuleAction &action) {
            return toRemove.contains(action.targetNodeId);
        });
        if (rule.actions.size() != before)
            rulesTouched = true;
        if (toRemove.contains(rule.triggerNodeId) || rule.actions.isEmpty()) {
            m_project.rules.removeAt(i);
            rulesTouched = true;
        }
    }

    if (toRemove.contains(m_selectedNodeId))
        m_selectedNodeId.clear();

    refreshModel();
    setDirty(true);
    emit selectedNodeChanged();
    if (rulesTouched)
        emit rulesChanged();
}

// --- Security editing -------------------------------------------------------

QVariantMap ServerStudio::security() const
{
    QVariantMap map;
    map["allowAnonymous"] = m_project.security.allowAnonymous;
    map["allowNone"] = m_project.security.allowNone;
    map["enableSecurity"] = m_project.security.enableSecurity;
    map["acceptAllClientCerts"] = m_project.security.acceptAllClientCerts;
    QVariantList users;
    for (const ServerProject::UserCredential &user : m_project.security.users) {
        QVariantMap userMap;
        userMap["username"] = user.username;
        userMap["password"] = user.password;
        users.append(userMap);
    }
    map["users"] = users;
    return map;
}

void ServerStudio::setSecurityFlags(bool allowAnonymous, bool allowNone, bool enableSecurity)
{
    if (!m_hasProject)
        return;
    m_project.security.allowAnonymous = allowAnonymous;
    m_project.security.allowNone = allowNone;
    m_project.security.enableSecurity = enableSecurity;
    setDirty(true);
    emit securityChanged();
}

void ServerStudio::setAcceptAllClientCerts(bool acceptAll)
{
    if (!m_hasProject || m_project.security.acceptAllClientCerts == acceptAll)
        return;
    m_project.security.acceptAllClientCerts = acceptAll;
    setDirty(true);
    emit securityChanged();
}

void ServerStudio::addUser(const QString &username, const QString &password)
{
    if (!m_hasProject || username.trimmed().isEmpty())
        return;
    for (ServerProject::UserCredential &user : m_project.security.users) {
        if (user.username == username) {
            user.password = password;
            setDirty(true);
            emit securityChanged();
            return;
        }
    }
    m_project.security.users.append({username, password});
    setDirty(true);
    emit securityChanged();
}

void ServerStudio::removeUser(const QString &username)
{
    if (!m_hasProject)
        return;
    const qsizetype before = m_project.security.users.size();
    m_project.security.users.removeIf([&username](const ServerProject::UserCredential &user) {
        return user.username == username;
    });
    if (m_project.security.users.size() != before) {
        setDirty(true);
        emit securityChanged();
    }
}

// --- Runtime control --------------------------------------------------------

void ServerStudio::startServer()
{
    if (!m_hasProject) {
        notify(Diagnostics::Warning, [this] {
            return tr("Create or open a server project first.");
        });
        return;
    }

    const ServerProject::Validator::Result validation =
        ServerProject::Validator::validate(m_project);
    if (!validation.ok) {
        notify(Diagnostics::Error,
               [this, detail = validation.errors.join(QStringLiteral("; "))] {
                   return tr("Project is not valid: %1").arg(detail);
               });
        return;
    }

    const QString snapshot = writeRuntimeSnapshot();
    if (snapshot.isEmpty()) {
        notify(Diagnostics::Error, [this] {
            return tr("Could not prepare the server project.");
        });
        return;
    }

    // The snapshot just written reflects the current project, so nothing is
    // pending a restart until the next edit.
    m_configChangedSinceStart = false;
    emit restartRequiredChanged();

    m_controller.start(m_project.server.endpoint.port, snapshot, serverPkiDir());
}

QString ServerStudio::serverPkiDir() const
{
    // Independent server PKI, kept separate from the client PKI, so the
    // generated server certificate and trust list persist across runs.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("pki-server"));
}

QStringList ServerStudio::rejectedCertificates() const
{
    const QDir dir(serverPkiDir() + QStringLiteral("/rejected/certs"));
    if (!dir.exists())
        return {};
    return dir.entryList({QStringLiteral("*.der"), QStringLiteral("*.crt")}, QDir::Files,
                         QDir::Time);
}

bool ServerStudio::trustRejectedCertificate(const QString &fileName)
{
    // Guard against path traversal: only accept a bare file name.
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/'))
        || fileName.contains(QLatin1Char('\\'))) {
        return false;
    }

    const QString pki = serverPkiDir();
    const QString source = pki + QStringLiteral("/rejected/certs/") + fileName;
    const QString trustedDir = pki + QStringLiteral("/trusted/certs");
    if (!QFileInfo::exists(source)) {
        notify(Diagnostics::Warning, [this] {
            return tr("The certificate is no longer available.");
        });
        return false;
    }
    QDir().mkpath(trustedDir);

    const QString target = trustedDir + QLatin1Char('/') + fileName;
    QFile::remove(target); // replace any stale copy so the rename can succeed
    if (!QFile::rename(source, target)) {
        notify(Diagnostics::Error, [this] {
            return tr("Could not trust the certificate.");
        });
        return false;
    }

    notify(Diagnostics::Info, [this] {
        return tr("Certificate trusted. Restart the server to apply it.");
    });
    emit securityChanged();
    return true;
}

void ServerStudio::stop()
{
    m_controller.stop();
}

void ServerStudio::restart()
{
    // Re-serialize the current project rather than reusing the controller's
    // stored snapshot path, so edits made since the last start are applied.
    if (m_controller.state() != ServerRuntimeController::State::Stopped)
        m_controller.stop();
    startServer();
}

void ServerStudio::kill()
{
    m_controller.kill();
}

void ServerStudio::openInClient()
{
    if (!running() || m_controller.endpointUrl().isEmpty()) {
        notify(Diagnostics::Warning, [this] {
            return tr("Start the server runtime before opening it in the client.");
        });
        return;
    }
    if (!m_opcUaManager) {
        notify(Diagnostics::Error, [this] {
            return tr("No OPC UA client is available.");
        });
        return;
    }
    m_opcUaManager->connectToLocalEndpoint(m_controller.endpointUrl());
    emit openInClientRequested();
}

// --- Private helpers --------------------------------------------------------

void ServerStudio::setDirty(bool dirty)
{
    // Any edit while the server is running means its started configuration is
    // stale until a restart. Track this even when the project is already dirty,
    // so a second edit after start is still recorded (the guard below would
    // otherwise return early).
    if (dirty && running() && !m_configChangedSinceStart) {
        m_configChangedSinceStart = true;
        emit restartRequiredChanged();
    }

    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void ServerStudio::refreshModel()
{
    m_nodeModel->setNodes(m_project.nodes);
    emit nodesChanged();
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
