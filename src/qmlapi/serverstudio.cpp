#include "serverstudio.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

#include <QOpcUaExtensionObject>
#include <QOpcUaLocalizedText>
#include <QOpcUaMultiDimensionalArray>
#include <QOpcUaQualifiedName>

#include "core/clonebrowser.h"
#include "core/datatyperesolver.h"
#include "core/diagnosticslevel.h"
#include "models/dataaccessmodel.h"
#include "models/opcuamodel.h"
#include "opcuamanager.h"
#include "servernodemodel.h"
#include "serverproject/clonedatatype.h"
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
 * \brief Returns the OPC UA base type of the cloned variable \a source.
 *
 * Uses the base type CloneBrowser resolved on the server. A node that was not
 * resolved there (for example one built by a test or an older producer) falls
 * back to the namespace-0 tables, so standard types still map exactly.
 */
QString cloneBaseType(const CloneNode &source)
{
    if (source.resolvedDataType.resolution != ResolvedDataType::Resolution::None)
        return source.resolvedDataType.builtInBase;
    return DataTypeIds::resolveLocally(source.dataTypeId).builtInBase;
}

/*!
 * \internal
 * \brief Returns whether \a value is an ExtensionObject or an array of them.
 */
bool holdsExtensionObject(const QVariant &value)
{
    const QMetaType extensionObject = QMetaType::fromType<QOpcUaExtensionObject>();
    if (value.metaType() == extensionObject)
        return true;
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        return !list.isEmpty() && list.first().metaType() == extensionObject;
    }
    return false;
}

/*!
 * \internal
 * \brief Captured value split into its elements and array dimensions.
 */
struct UnpackedValue
{
    bool valid = false;           //!< Whether a value was captured.
    bool isArray = false;         //!< Whether the value is an array.
    QVariantList elements;        //!< Scalar: one element; array: all elements, row-major.
    QList<quint32> dimensions;    //!< Dimensions of a multi-dimensional value; else empty.
};

/*!
 * \internal
 * \brief Splits the captured value of \a source into elements without losing shape.
 *
 * Qt OPC UA returns a one-element array as a scalar and a multi-dimensional
 * array as QOpcUaMultiDimensionalArray; both are turned back into arrays here,
 * using the ValueRank read from the server.
 */
UnpackedValue unpackValue(const CloneNode &source)
{
    UnpackedValue result;
    const QVariant &value = source.value;
    if (!value.isValid())
        return result;
    result.valid = true;

    if (value.metaType() == QMetaType::fromType<QOpcUaMultiDimensionalArray>()) {
        const auto array = value.value<QOpcUaMultiDimensionalArray>();
        result.isArray = true;
        result.elements = array.valueArray();
        result.dimensions = array.arrayDimensions();
    } else if (value.typeId() == QMetaType::QVariantList) {
        result.isArray = true;
        result.elements = value.toList();
    } else {
        result.isArray = source.valueRank >= 1;
        result.elements = {value};
    }
    return result;
}

/*!
 * \internal
 * \brief Returns the text of one captured element \a element for Server Studio.
 *
 * OPC UA value types without a Qt string conversion are mapped explicitly;
 * an ExtensionObject has no text.
 */
QString elementText(const QVariant &element)
{
    if (element.metaType() == QMetaType::fromType<QOpcUaLocalizedText>())
        return element.value<QOpcUaLocalizedText>().text();
    if (element.metaType() == QMetaType::fromType<QOpcUaQualifiedName>())
        return element.value<QOpcUaQualifiedName>().name();
    if (element.metaType() == QMetaType::fromType<QOpcUaExtensionObject>())
        return {};
    switch (element.typeId()) {
    case QMetaType::QDateTime:
        return element.toDateTime().toString(Qt::ISODateWithMs);
    case QMetaType::QByteArray:
        return QString::fromLatin1(element.toByteArray().toBase64());
    case QMetaType::QUuid:
        return element.toUuid().toString(QUuid::WithoutBraces);
    default:
        return element.toString();
    }
}

/*!
 * \internal
 * \brief Returns the structure type name of the structured variable \a source.
 * \param source Captured structure variable.
 * \param memberTypeHint Value of a "structType"/"strStructType" member, if any.
 *
 * The OPC UA type name is authoritative: the BrowseName of the DataType node
 * read from the server, then the name embedded in a CODESYS "|type|" id. A
 * string member called structType is a PLC-project convention, not a rule, so
 * it is only a fallback for servers that do not name the type. An anonymous
 * structure is called "Structure".
 */
QString structureTypeName(const CloneNode &source, const QString &memberTypeHint)
{
    const QString serverName = source.resolvedDataType.typeName;
    if (!serverName.isEmpty() && serverName != QLatin1String("Structure")
        && serverName != QLatin1String("BaseDataType"))
        return serverName;
    const QString idName = DataTypeIds::nameFromTypeId(source.dataTypeId);
    if (!idName.isEmpty())
        return idName;
    if (!memberTypeHint.trimmed().isEmpty())
        return memberTypeHint.trimmed();
    return QStringLiteral("Structure");
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
                       const QStringList &namespaceUris, const CloneCompletion &completion) {
                    applyCloneSnapshot(nodes, namespaceUris, completion);
                });
        connect(m_opcUaManager, &OpcUaManager::cloneProgress, this,
                [this](quint64 /*requestId*/, const CloneProgress &progress) {
                    // After Cancel, late progress must not reopen the progress dialog.
                    if (!m_cloneInProgress || m_cloneCancelling)
                        return;
                    m_cloneProgress = progress;
                    emit cloneProgressChanged();
                });
    }
}

/*!
 * \brief Returns a short progress text for the clone in flight.
 *
 * Empty when no clone is running.
 */
QString ServerStudio::cloneProgressText() const
{
    if (!m_cloneInProgress)
        return {};
    // The breakdown shows where a slow clone waits: browsing, metadata or value
    // reads, or DataType lookups.
    const CloneProgress &p = m_cloneProgress;
    QString text = tr("%1 node(s) captured").arg(p.captured);
    text += QLatin1Char('\n')
            + tr("Queued: %1 to browse, %2 metadata reads, %3 value reads, %4 waiting for "
                 "data types")
                  .arg(p.browseQueued)
                  .arg(p.metadataQueued)
                  .arg(p.valuesQueued)
                  .arg(p.waitingForType);
    text += QLatin1Char('\n')
            + tr("In progress: %1 browse(s), %2 read request(s)")
                  .arg(p.browsing)
                  .arg(p.readRequests);
    if (p.typeLookups > 0)
        text += QLatin1Char('\n') + tr("Looking up %1 data type(s)").arg(p.typeLookups);
    return text;
}

/*!
 * \internal
 * \brief Sets whether a clone is in flight to \a inProgress and resets the counters.
 */
void ServerStudio::setCloneInProgress(bool inProgress)
{
    m_cloneInProgress = inProgress;
    m_cloneCancelling = false;
    m_cloneProgress = CloneProgress();
    emit cloneProgressChanged();
}

/*!
 * \brief Cancels the clone in progress.
 *
 * The worker answers with a Cancelled result, which leaves the current project
 * unchanged.
 */
void ServerStudio::cancelClone()
{
    if (!m_cloneInProgress || m_cloneCancelling || !m_opcUaManager)
        return;
    m_cloneCancelling = true;
    m_opcUaManager->cancelCloneSnapshot();
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

        QString structName;
        for (const ServerProject::StructType &structType : m_project.structTypes) {
            if (structType.nodeId == node->structTypeId) {
                structName = structType.name;
                break;
            }
        }
        map["isStructure"] = node->isStructure();
        map["structTypeName"] = structName;
        map["typeText"] = ServerProject::variableTypeText(*node, structName);
        map["isMultiDimensional"] = node->valueRank > 1;

        // Origin of a cloned variable: the original DataType name and id.
        QString sourceType = node->source.originalDataTypeName;
        if (!node->source.originalDataTypeId.isEmpty()) {
            sourceType = sourceType.isEmpty()
                             ? node->source.originalDataTypeId
                             : QStringLiteral("%1 (%2)").arg(sourceType,
                                                             node->source.originalDataTypeId);
        }
        // A cloned structure without any original DataType is a container the
        // server published without a type, e.g. a CODESYS structure that holds
        // a function block instance.
        if (sourceType.isEmpty() && node->isStructure() && !node->source.originalNodeId.isEmpty())
            sourceType = tr("not provided by the server (a structure that contains a function "
                            "block or another object)");
        map["sourceDataType"] = sourceType;
    }
    map["sourceNodeId"] = node->source.originalNodeId;
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
    m_project.structTypes = result.structTypes;
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

bool ServerStudio::cloneFromClient(bool preserveOriginalIds, int maxNodes, int maxArrayElements)
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
    setCloneInProgress(true);
    notify(Diagnostics::Info, [this] {
        return tr("Cloning the server address space…");
    });
    m_opcUaManager->requestCloneSnapshot(maxNodes, maxArrayElements);
    return true;
}

void ServerStudio::applyCloneSnapshot(const QList<CloneNode> &nodes,
                                      const QStringList &namespaceUris,
                                      const CloneCompletion &completion)
{
    setCloneInProgress(false);

    if (completion.state == CloneCompletion::State::Cancelled) {
        notify(Diagnostics::Info, [this] {
            return tr("Clone cancelled; the server project was not changed.");
        });
        return;
    }

    if (completion.state == CloneCompletion::State::Failed || nodes.isEmpty()) {
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
    int substitutedCount = 0; // variables whose type Server Studio cannot model exactly

    // Which captured nodes have child variables, and the value of a string
    // member called structType/strStructType (a PLC-project naming hint).
    QSet<QString> withChildVariables;
    QHash<QString, QString> memberTypeHints;
    for (const CloneNode &source : nodes) {
        if (!source.isVariable || source.parentNodeId.isEmpty())
            continue;
        withChildVariables.insert(source.parentNodeId);
        if ((source.browseName == QLatin1String("structType")
             || source.browseName == QLatin1String("strStructType"))
            && source.value.typeId() == QMetaType::QString)
            memberTypeHints.insert(source.parentNodeId, source.value.toString());
    }

    // Structure types are created once per source DataType. A namespace-0 type
    // (for example ServerStatusDataType) already exists in every server.
    QHash<QString, QString> structTypeIds; // source DataType id (or name) -> project type id
    QSet<QString> usedTypeIds;
    const auto structTypeFor = [&](const CloneNode &source) -> QString {
        const QString name = structureTypeName(source, memberTypeHints.value(source.nodeId));
        const QString key = source.dataTypeId.isEmpty() ? QStringLiteral("name:") + name
                                                        : source.dataTypeId;
        const auto known = structTypeIds.constFind(key);
        if (known != structTypeIds.constEnd())
            return known.value();

        QString typeId;
        const ServerProject::ParsedNodeId parsed = ServerProject::parseNodeId(source.dataTypeId);
        if (parsed.valid && parsed.ns == 0) {
            typeId = source.dataTypeId;
        } else if (m_clonePreserveIds && parsed.valid) {
            if (!nsIndexMap.contains(parsed.ns)) {
                const QString uri = parsed.ns < namespaceUris.size()
                                        ? namespaceUris.at(parsed.ns)
                                        : QStringLiteral("urn:opcuamanager:clone-ns%1")
                                              .arg(parsed.ns);
                project.namespaces.append({uri});
                nsIndexMap.insert(parsed.ns, static_cast<quint16>(project.namespaces.size()));
            }
            typeId = remapId(source.dataTypeId);
        }
        if (typeId.isEmpty() || usedTypeIds.contains(typeId)) {
            const QString base = QStringLiteral("ns=1;s=Type.%1").arg(slug(name));
            typeId = base;
            int suffix = 2;
            while (usedTypeIds.contains(typeId))
                typeId = QStringLiteral("%1_%2").arg(base).arg(suffix++);
        }
        usedTypeIds.insert(typeId);
        structTypeIds.insert(key, typeId);
        project.structTypes.append({name, typeId, source.dataTypeId});
        return typeId;
    };

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
        node.source.originalNodeId = source.nodeId;
        if (source.isVariable) {
            node.kind = NodeKind::Variable;
            const QString base = cloneBaseType(source);
            const UnpackedValue value = unpackValue(source);
            node.source.originalDataTypeId = source.dataTypeId;
            node.source.originalDataTypeName = source.resolvedDataType.typeName;
            node.source.builtInBase = base;
            node.source.originalValueRank = source.valueRank;

            // Rank and dimensions as on the server. ValueRank 0, -2 and -3 are
            // "any"/"scalar or array" ranks; the captured value decides.
            int rank = source.valueRank;
            if (rank < 1)
                rank = value.isArray ? int(std::max<qsizetype>(1, value.dimensions.size())) : -1;
            node.valueRank = rank;
            if (source.arrayDimensions.size() == rank)
                node.arrayDimensions = source.arrayDimensions;
            else if (value.dimensions.size() == rank)
                node.arrayDimensions = value.dimensions;

            // A structure keeps its own type; its members are the child nodes,
            // so it carries no value. A structure is recognized by its base type,
            // by an ExtensionObject value, or - when the type is unknown - by
            // member variables without a value of its own.
            const bool structured = base == QLatin1String("Structure")
                                    || holdsExtensionObject(source.value)
                                    || (base.isEmpty() && withChildVariables.contains(source.nodeId)
                                        && !value.valid);
            if (structured) {
                node.structTypeId = structTypeFor(source);
                node.writable = false;
                project.nodes.append(node);
                continue;
            }

            // The resolved base type keeps vendor aliases (for example a CODESYS
            // STRING type) exact; unsupported or unknown types get a substitute
            // derived from the captured value and are counted for the report.
            const ServerProject::CloneDataTypeChoice choice =
                ServerProject::cloneDataTypeFor(base, source.value);
            node.dataType = choice.dataType;
            node.source.substituted = choice.substituted;
            if (choice.substituted)
                ++substitutedCount;
            // Writable by default so the cloned server can be edited/exercised.
            node.writable = true;
            // A value that was not read stays absent instead of becoming 0.
            if (value.valid) {
                if (rank >= 1) {
                    QVariantList out;
                    out.reserve(value.elements.size());
                    for (const QVariant &element : value.elements)
                        out.append(scalarFromText(node.dataType, elementText(element)));
                    node.initialValue = out;
                } else {
                    node.initialValue =
                        scalarFromText(node.dataType, elementText(value.elements.value(0)));
                }
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

    // The report states how complete the clone is: a partial traversal, failed
    // reads/browses and substituted types are each named, so a clone that
    // differs from the source never reads as an exact copy.
    const int failedNodes = completion.failedReads + completion.failedBrowses;
    const bool exact =
        completion.isComplete() && failedNodes == 0 && completion.limitedArrays == 0;
    const auto report =
           [this, completion, failedNodes, substitutedCount, n = int(project.nodes.size())] {
               QString text;
               switch (completion.state) {
               case CloneCompletion::State::Truncated:
                   text = completion.nodeLimitReached
                              ? tr("Cloned %1 node(s); the limit of %2 nodes was reached, so the "
                                   "clone is incomplete.")
                                    .arg(n)
                                    .arg(completion.options.maxNodes)
                              : tr("Cloned %1 node(s); %2 node(s) at the depth limit of %3 were "
                                   "not browsed, so the clone may be incomplete.")
                                    .arg(n)
                                    .arg(completion.depthLimitedNodes)
                                    .arg(completion.options.maxDepth);
                   break;
               case CloneCompletion::State::TimedOut:
                   text = tr("Cloned %1 node(s); the server did not answer in time and %2 "
                             "node(s) were left unread, so the clone is incomplete.")
                              .arg(n)
                              .arg(completion.pendingRequests);
                   break;
               default:
                   text = tr("Cloned %1 node(s) from the server.").arg(n);
                   break;
               }
               if (completion.limitedArrays > 0) {
                   text += QLatin1Char(' ')
                           + tr("%1 array(s) were limited to their first %2 elements; %3 "
                                "element(s) were not cloned.")
                                 .arg(completion.limitedArrays)
                                 .arg(completion.options.maxArrayElements)
                                 .arg(completion.arrayElementsSkipped);
               }
               if (failedNodes > 0) {
                   text += QLatin1Char(' ')
                           + tr("%1 node(s) could not be read or browsed completely.")
                                 .arg(failedNodes);
               }
               if (substitutedCount > 0) {
                   text += QLatin1Char(' ')
                           + tr("%1 variable(s) use a substitute data type (date/time or "
                                "unsupported type).")
                                 .arg(substitutedCount);
               }
               return text;
           };
    notify(exact && substitutedCount == 0 ? Diagnostics::Info : Diagnostics::Warning, report);
    // An incomplete clone is easy to miss in the status bar, so QML also shows
    // the report in a dialog.
    if (!exact)
        emit cloneIncomplete(report());
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

        // A structure variable has no type, value or simulation of its own to edit.
        if (node.isVariable() && !node.isStructure()) {
            if (fields.contains(QStringLiteral("dataType"))) {
                const QString type = fields.value(QStringLiteral("dataType")).toString();
                if (dataTypeNames().contains(type))
                    node.dataType = type;
            }
            // The editor toggles scalar/one-dimensional only; a multi-dimensional
            // rank (from a clone or NodeSet2) is kept.
            if (fields.contains(QStringLiteral("valueRank")) && node.valueRank <= 1) {
                node.valueRank = fields.value(QStringLiteral("valueRank")).toInt() == 1 ? 1 : -1;
                if (node.valueRank != 1)
                    node.arrayDimensions.clear();
            }
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
    m_nodeModel->setNodes(m_project.nodes, m_project.structTypes);
    emit nodesChanged();
}

QVariant ServerStudio::parseInitialValue(const QString &dataType, int valueRank, const QString &text)
{
    if (valueRank >= 1) {
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
