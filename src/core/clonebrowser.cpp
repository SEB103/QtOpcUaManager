#include "clonebrowser.h"

#include <QLoggingCategory>
#include <QOpcUaClient>
#include <QOpcUaEndpointDescription>
#include <QOpcUaLocalizedText>
#include <QOpcUaNode>
#include <QOpcUaQualifiedName>
#include <QOpcUaReadItem>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <utility>

/*!
 * \internal
 * \brief Logging category for clone diagnostics.
 *
 * Debug output is disabled by default. Enable it with
 * QT_LOGGING_RULES="opcua.clone.debug=true" to list every captured DataType id
 * with its name, base type and resolution, and the traversal outcome.
 */
Q_LOGGING_CATEGORY(lcOpcUaClone, "opcua.clone", QtInfoMsg)

/*!
 * \class CloneBrowser
 * \inmodule OpcUaManager
 * \brief Recursively browses a server subtree and reads its variables for a clone.
 *
 * The traversal is breadth-first and bounded: at most
 * CloneOptions::maxConcurrentBrowses browse requests and
 * CloneOptions::maxConcurrentReads attribute read requests are in flight at
 * the same time, so an embedded PLC is not flooded with thousands of parallel
 * requests.
 *
 * Objects are browsed as soon as they are discovered. Variables are first
 * queued for a batched metadata read of DataType, ValueRank, ArrayDimensions,
 * AccessLevel and UserAccessLevel; each attribute keeps its status, so a
 * failed read stays visible instead of turning into a default value. A
 * variable is then browsed for children unless it is a scalar of a simple,
 * resolved type: on a PLC such as CODESYS these are about nine in ten nodes,
 * they have no children, and browsing each of them costs a round trip on a
 * server that answers one request at a time. Whether such scalars really have
 * no children is probed per TypeDefinition: the first
 * CloneOptions::leafProbeCount of them are browsed, and only when none had
 * children are the others of that TypeDefinition left unbrowsed. Structures,
 * arrays, variables whose type is unknown or could not be read, and every
 * TypeDefinition whose probe found children are always browsed, so the
 * members of a structure and the elements of an array are found even when
 * the structure's own value or type cannot be read.
 *
 * The Value is read only for variables whose DataType is not a structure. It
 * travels in the next metadata request (one request then carries the
 * metadata of new variables and the values of routed ones), so a variable
 * costs no extra round trip; separate Value requests are only sent for a full
 * batch or for the values left at the end. A structure's members are its child nodes, and
 * on servers such as CODESYS the Value of a structure encodes its whole
 * subtree, so reading it at every nesting level would make the server encode
 * the same data again and again. A DataType that the namespace-0 tables do not
 * cover is looked up through DataTypeResolver while the traversal continues;
 * the variables using it wait for that lookup before their Value is read.
 * A lookup round that times out on a busy server is retried; a type that is
 * still unknown after CloneOptions::typeLookupAttempts rounds leaves its
 * variables without a value and with an error, because an unknown type may be
 * a structure whose Value would encode a whole subtree.
 *
 * Arrays of structures hold most nodes of a large PLC (a 1000-row recipe
 * table of structures alone can hold more than half of them). When the
 * elements of an array follow the NodeId pattern "<array>[index]", only the
 * first element is browsed; once nothing else is queued, its subtree is
 * copied for the other elements by replacing the "<array>[first]" prefix,
 * deepest arrays first. The copies are then read like any variable, so their
 * DataType, ValueRank, access levels and values come from the server; a copy
 * the server does not know (BadNodeIdUnknown) makes that element fall back to
 * a normal browse. Elements of arrays whose template contains an Object, or
 * that do not follow the pattern, are browsed as before.
 *
 * At the start, the server's MaxNodesPerRead is read; the read batches are
 * then enlarged up to CloneOptions::maxVariablesPerRead and
 * CloneOptions::maxValuesPerRead within that limit, because a PLC that serves
 * one request at a time pays mostly per request.
 *
 * A node reached through more than one hierarchical reference is captured and
 * browsed once; the additional parents are recorded. Reaching the node or depth
 * limit marks the result Truncated, the idle timeout (or an optional overall
 * timeout) marks it TimedOut, and cancel() marks it Cancelled, so a partial
 * result is never reported as complete. Read failures are logged to the
 * opcua.clone category, the first kMaxLoggedReadFailures of them in detail.
 */

namespace {

/*!
 * \internal
 * \brief Node id of the standard FolderType, used to tag folders.
 */
const QString kFolderTypeId = QStringLiteral("ns=0;i=61");

/*!
 * \internal
 * \brief Metadata attributes read for every variable, in request order.
 *
 * The Value is read separately, see CloneBrowser::settleVariable().
 */
const QList<QOpcUa::NodeAttribute> kMetadataAttributes = {
    QOpcUa::NodeAttribute::DataType,        QOpcUa::NodeAttribute::ValueRank,
    QOpcUa::NodeAttribute::ArrayDimensions, QOpcUa::NodeAttribute::AccessLevel,
    QOpcUa::NodeAttribute::UserAccessLevel,
};

/*!
 * \internal
 * \brief Attributes read for a captured root node, in request order.
 */
const QList<QOpcUa::NodeAttribute> kRootInfoAttributes = {
    QOpcUa::NodeAttribute::NodeClass,
    QOpcUa::NodeAttribute::BrowseName,
    QOpcUa::NodeAttribute::DisplayName,
};

/*!
 * \internal
 * \brief Namespace-0 variables read for the server identity, in request order.
 *
 * ServerArray, then the BuildInfo members ProductName, ProductUri,
 * ManufacturerName, SoftwareVersion, BuildNumber and BuildDate.
 */
const QStringList kIdentityNodeIds = {
    QStringLiteral("ns=0;i=2254"), QStringLiteral("ns=0;i=2261"), QStringLiteral("ns=0;i=2262"),
    QStringLiteral("ns=0;i=2263"), QStringLiteral("ns=0;i=2264"), QStringLiteral("ns=0;i=2265"),
    QStringLiteral("ns=0;i=2266"),
};

/*!
 * \internal
 * \brief Time in milliseconds the server identity read may take before it is abandoned.
 */
constexpr int kIdentityTimeoutMs = 5000;

/*!
 * \internal
 * \brief Returns the text of message security \a mode as used in snapshots.
 */
QString securityModeName(QOpcUaEndpointDescription::MessageSecurityMode mode)
{
    switch (mode) {
    case QOpcUaEndpointDescription::MessageSecurityMode::None: return QStringLiteral("None");
    case QOpcUaEndpointDescription::MessageSecurityMode::Sign: return QStringLiteral("Sign");
    case QOpcUaEndpointDescription::MessageSecurityMode::SignAndEncrypt:
        return QStringLiteral("SignAndEncrypt");
    default: return QString();
    }
}

/*!
 * \internal
 * \brief Returns \a value as text; a LocalizedText yields its text.
 */
QString identityText(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QOpcUaLocalizedText>())
        return value.value<QOpcUaLocalizedText>().text();
    return value.toString();
}

/*!
 * \internal
 * \brief Number of read failures logged in detail; later ones are only counted.
 */
constexpr int kMaxLoggedReadFailures = 20;

/*!
 * \internal
 * \brief Returns the symbol path of string NodeId \a nodeId: the text after its last '|'.
 *
 * CODESYS NodeIds carry a kind marker before the path, e.g. "|var|" for
 * variables and "|vprop|" for the Dimensions/IndexMin/IndexMax properties of
 * an array, so the same path appears behind different prefixes. Without a
 * marker the text after ";s=" is the path.
 */
QString symbolPath(const QString &nodeId)
{
    const qsizetype bar = nodeId.lastIndexOf(QLatin1Char('|'));
    if (bar >= 0)
        return nodeId.mid(bar + 1);
    const qsizetype string = nodeId.indexOf(QLatin1String(";s="));
    return string >= 0 ? nodeId.mid(string + 3) : nodeId;
}

/*!
 * \internal
 * \brief Returns \a nodeId of a template subtree node mapped to another array element.
 * \param nodeId NodeId of a node below the template element.
 * \param templateId NodeId of the template element.
 * \param elementId NodeId of the element the copy is for.
 * \return The mapped NodeId, or an empty string when \a nodeId does not contain
 *         the template's path.
 *
 * A NodeId that starts with the template's NodeId gets the element's NodeId as
 * prefix. Otherwise the template's symbol path is replaced where it occurs as
 * a whole path segment, which maps e.g. the CODESYS property
 * "|vprop|…arr[1].sub.Dimensions" of the template "|var|…arr[1]" correctly.
 */
QString mapTemplateId(const QString &nodeId, const QString &templateId, const QString &elementId)
{
    if (nodeId.startsWith(templateId))
        return elementId + nodeId.mid(templateId.size());
    const QString templatePath = symbolPath(templateId);
    const QString elementPath = symbolPath(elementId);
    if (templatePath.isEmpty())
        return {};
    for (qsizetype pos = nodeId.indexOf(templatePath); pos >= 0;
         pos = nodeId.indexOf(templatePath, pos + 1)) {
        const qsizetype end = pos + templatePath.size();
        const bool startsSegment = pos == 0 || nodeId.at(pos - 1) == QLatin1Char('|')
                                   || nodeId.at(pos - 1) == QLatin1Char('=');
        const bool endsSegment = end == nodeId.size() || nodeId.at(end) == QLatin1Char('.')
                                 || nodeId.at(end) == QLatin1Char('[');
        if (startsSegment && endsSegment)
            return nodeId.left(pos) + elementPath + nodeId.mid(end);
    }
    return {};
}

/*!
 * \internal
 * \brief Minimum number of captured elements for an array to be expanded by template.
 */
constexpr int kTemplateMinElements = 3;

/*!
 * \internal
 * \brief Node id of Server.ServerCapabilities.OperationLimits.MaxNodesPerRead.
 */
const QString kMaxNodesPerReadId = QStringLiteral("ns=0;i=11705");

/*!
 * \internal
 * \brief Operations a metadata read costs per variable: five attributes plus a carried Value.
 */
constexpr int kOperationsPerVariable = 6;

/*!
 * \internal
 * \brief Built-in base name of structure DataTypes, whose Value is not read.
 */
const QString kStructureBase = QStringLiteral("Structure");

/*!
 * \internal
 * \brief Interval of the progress reports in milliseconds.
 */
constexpr int kProgressIntervalMs = 500;

/*!
 * \internal
 * \brief Progress reports between two progress lines in the log (every 30 s).
 */
constexpr int kProgressLogTicks = 60;

/*!
 * \internal
 * \brief Returns whether \a status is a Bad status code (severity bits 10).
 *
 * Uncertain statuses still carry a value and are not treated as failures.
 */
bool isBad(QOpcUa::UaStatusCode status)
{
    return (static_cast<quint32>(status) & 0x80000000u) != 0;
}

/*!
 * \internal
 * \brief Returns the attribute name of \a attribute used in error texts.
 */
QString attributeName(QOpcUa::NodeAttribute attribute)
{
    switch (attribute) {
    case QOpcUa::NodeAttribute::Value: return QStringLiteral("Value");
    case QOpcUa::NodeAttribute::DataType: return QStringLiteral("DataType");
    case QOpcUa::NodeAttribute::ValueRank: return QStringLiteral("ValueRank");
    case QOpcUa::NodeAttribute::ArrayDimensions: return QStringLiteral("ArrayDimensions");
    case QOpcUa::NodeAttribute::AccessLevel: return QStringLiteral("AccessLevel");
    case QOpcUa::NodeAttribute::UserAccessLevel: return QStringLiteral("UserAccessLevel");
    default: return QStringLiteral("Attribute");
    }
}

/*!
 * \internal
 * \brief Converts an ArrayDimensions attribute value \a value to a list.
 *
 * Qt OPC UA returns a one-element array as a scalar, so both forms are accepted.
 */
QList<quint32> toArrayDimensions(const QVariant &value)
{
    QList<quint32> dimensions;
    if (!value.isValid())
        return dimensions;
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &entry : list)
            dimensions.append(entry.toUInt());
    } else {
        dimensions.append(value.toUInt());
    }
    return dimensions;
}

} // namespace

/*!
 * \brief Constructs the browser for \a rootNodeId using \a client.
 * \param client Connected client; must live in the calling thread.
 * \param rootNodeId Node whose subtree is captured; the root itself is not captured.
 * \param requestId GUI request id echoed in progress() and finished().
 * \param options Traversal limits.
 * \param parent Owning QObject.
 */
CloneBrowser::CloneBrowser(QOpcUaClient *client, const QString &rootNodeId, quint64 requestId,
                           const CloneOptions &options, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_rootNodeId(rootNodeId)
    , m_requestId(requestId)
    , m_options(options)
{
    m_options.maxConcurrentBrowses = std::max(1, m_options.maxConcurrentBrowses);
    m_options.maxConcurrentReads = std::max(1, m_options.maxConcurrentReads);
    m_options.variablesPerRead = std::max(1, m_options.variablesPerRead);
    m_options.valuesPerRead = std::max(1, m_options.valuesPerRead);
    m_options.typeLookupTimeoutMs = std::max(1, m_options.typeLookupTimeoutMs);
    m_options.typeLookupAttempts = std::max(1, m_options.typeLookupAttempts);
    m_options.leafProbeCount = std::max(0, m_options.leafProbeCount);
    m_options.maxArrayElements = std::max(0, m_options.maxArrayElements);
    m_options.maxVariablesPerRead = std::max(1, m_options.maxVariablesPerRead);
    m_options.maxValuesPerRead = std::max(1, m_options.maxValuesPerRead);
    m_options.maxNodes = std::max(1, m_options.maxNodes);
    m_options.maxDepth = std::max(1, m_options.maxDepth);
}

/*!
 * \brief Stops listening for read results; in-flight protocol nodes are deleted as children.
 */
CloneBrowser::~CloneBrowser()
{
    if (m_readConnection)
        disconnect(m_readConnection);
}

/*!
 * \brief Starts the traversal at the root node and arms the timers.
 */
void CloneBrowser::start()
{
    if (m_started)
        return;
    m_started = true;
    m_elapsed.start();

    if (!m_client || m_rootNodeId.isEmpty()) {
        m_rootBrowseFailed = true;
        finish(CloneCompletion::State::Failed);
        return;
    }

    // A large PLC can take well over ten minutes, so there is no overall limit
    // by default: the idle timeout catches a server that stops answering, and
    // the user can cancel at any time.
    if (m_options.overallTimeoutMs > 0) {
        m_overallTimer = new QTimer(this);
        m_overallTimer->setSingleShot(true);
        connect(m_overallTimer, &QTimer::timeout, this,
                [this]() { finish(CloneCompletion::State::TimedOut); });
        m_overallTimer->start(m_options.overallTimeoutMs);
    }

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        // A DataType lookup round has its own timeout; while it runs the
        // traversal is not idle even if no browse or read is in flight.
        if (m_typeLookup) {
            m_idleTimer->start(m_options.idleTimeoutMs);
            return;
        }
        finish(CloneCompletion::State::TimedOut);
    });
    m_idleTimer->start(m_options.idleTimeoutMs);

    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        if (++m_progressTicks >= kProgressLogTicks) {
            m_progressTicks = 0;
            logProgress();
        }
        emit progress(m_requestId, progressSnapshot());
    });
    m_progressTimer->start(kProgressIntervalMs);

    // The read result signal is shared by every batch read of this client;
    // handleReadResults() claims only the results of this browser's requests.
    m_readConnection = connect(m_client, &QOpcUaClient::readNodeAttributesFinished, this,
                               &CloneBrowser::handleReadResults);

    // A captured root is the first node; its own attributes are read once,
    // and its children name it as their parent.
    if (m_options.captureRoot) {
        CloneNode root;
        root.nodeId = m_rootNodeId;
        appendNode(root);
        startRootInfoRead();
    }

    startLimitsRead();
    m_browseQueue.enqueue({m_rootNodeId, 0});
    pump();
}

/*!
 * \brief Ends the traversal as Cancelled with the nodes captured so far.
 */
void CloneBrowser::cancel()
{
    if (!m_started || m_finished)
        return;
    finish(CloneCompletion::State::Cancelled);
}

/*!
 * \brief Marks the variables in \a nodes whose DataType is not provided but that have members.
 * \param nodes Captured nodes; marked variables get CloneNode::dataTypeNotProvided.
 * \return The number of variables marked.
 *
 * CODESYS does not publish a DataType for a structure that contains a
 * function block instance, directly or in a nested structure: the DataType
 * read fails (BadNotSupported), while the members, including the function
 * block object, are browsable. Such a variable is a container that the clone
 * reproduces through its children. Only variables whose every attribute error
 * concerns the DataType and that have at least one captured child are marked;
 * any other failure stays a failed read.
 */
int CloneBrowser::markContainersWithoutDataType(QList<CloneNode> &nodes)
{
    QSet<QString> parents;
    for (const CloneNode &node : std::as_const(nodes)) {
        if (!node.parentNodeId.isEmpty())
            parents.insert(node.parentNodeId);
    }
    int marked = 0;
    for (CloneNode &node : nodes) {
        if (!node.isVariable || !node.dataTypeId.isEmpty() || node.attributeErrors.isEmpty()
            || !parents.contains(node.nodeId))
            continue;
        const bool onlyDataType =
            std::all_of(node.attributeErrors.cbegin(), node.attributeErrors.cend(),
                        [](const QString &error) {
                            return error.startsWith(QLatin1String("DataType:"));
                        });
        if (onlyDataType) {
            node.dataTypeNotProvided = true;
            ++marked;
        }
    }
    return marked;
}

/*!
 * \internal
 * \brief Starts queued browse and read requests up to the concurrency limits.
 *
 * Ends the traversal when nothing is queued or in flight.
 */
void CloneBrowser::pump()
{
    if (m_finished)
        return;

    while (m_browsing.size() < m_options.maxConcurrentBrowses && !m_browseQueue.isEmpty())
        startBrowse(m_browseQueue.dequeue());

    // A metadata request also carries up to as many queued values as it has
    // variables, so a variable costs no round trip of its own (the request
    // shape of one metadata + Value read per 16 variables). A full batch of
    // single-attribute reads (values, and the DataType checks of built nodes),
    // e.g. released by a DataType lookup or an array expansion, gets a request
    // of its own; leftovers are read when no metadata is queued and no read runs.
    const auto take = [](QStringList &queue, qsizetype count) {
        const qsizetype n = std::min<qsizetype>(count, queue.size());
        QStringList batch = queue.mid(0, n);
        queue.remove(0, n);
        return batch;
    };
    while (m_reading.size() < m_options.maxConcurrentReads) {
        QStringList metadata;
        QStringList values;
        QStringList checks;
        const qsizetype singles = m_valueQueue.size() + m_checkQueue.size();
        if (singles >= m_options.valuesPerRead
            || (singles > 0 && m_readQueue.isEmpty() && m_reading.isEmpty())) {
            values = take(m_valueQueue, m_options.valuesPerRead);
            checks = take(m_checkQueue, m_options.valuesPerRead - values.size());
        } else if (!m_readQueue.isEmpty()) {
            metadata = take(m_readQueue, m_options.variablesPerRead);
            values = take(m_valueQueue, m_options.variablesPerRead);
        } else {
            break;
        }
        startRead(metadata, values, checks);
    }

    startTypeLookup();
    finishIfIdle();
}

/*!
 * \internal
 * \brief Browses the hierarchical Object and Variable children of \a item.
 *
 * A request that cannot be started is recorded as a browse failure of that node.
 */
void CloneBrowser::startBrowse(const BrowseItem &item)
{
    QOpcUaNode *node = m_client->node(item.nodeId);
    if (!node) {
        markBrowseError(item.nodeId, QStringLiteral("BadNodeIdInvalid"));
        return;
    }
    node->setParent(this);
    m_browsing.insert(node);
    m_browseStarted.insert(node, m_elapsed.elapsed());
    ++m_completion.browseRequests;

    connect(node, &QOpcUaNode::browseFinished, this,
            [this, item, node](const QList<QOpcUaReferenceDescription> &references,
                               QOpcUa::UaStatusCode status) {
                handleBrowse(item, node, references, status);
            },
            Qt::SingleShotConnection);

    if (!node->browseChildren(QOpcUa::ReferenceTypeId::HierarchicalReferences,
                              QOpcUa::NodeClass::Object | QOpcUa::NodeClass::Variable)) {
        m_browsing.remove(node);
        m_browseStarted.remove(node);
        node->deleteLater();
        markBrowseError(item.nodeId, QStringLiteral("browse request could not be started"));
    }
}

/*!
 * \internal
 * \brief Captures the children found under \a item and queues their own work.
 *
 * A child already captured through another reference is not captured again;
 * \a item is recorded as an additional parent instead. New children are
 * dropped once the node limit is reached. Children at the depth limit are
 * captured but not browsed. A child object is queued for browsing at once; a
 * child variable is queued for its metadata read, and its browse is decided
 * by settleVariable() once its type is known. Below an array variable only the
 * first CloneOptions::maxArrayElements element children (browse names ending
 * in "[...]") are captured; other children, such as the CODESYS Dimensions,
 * IndexMin and IndexMax properties, are always captured.
 */
void CloneBrowser::handleBrowse(const BrowseItem &item, QOpcUaNode *node,
                                const QList<QOpcUaReferenceDescription> &references,
                                QOpcUa::UaStatusCode status)
{
    m_browsing.remove(node);
    const qint64 startedMs = m_browseStarted.take(node);
    node->deleteLater();
    if (m_finished)
        return;
    touch();

    ++m_browseWindow.count;
    m_browseWindow.totalMs += m_elapsed.elapsed() - startedMs;
    ++m_browseWindow.items;
    if (references.isEmpty())
        ++m_browseWindow.empty;
    recordProbe(item.nodeId, status == QOpcUa::UaStatusCode::Good && !references.isEmpty());

    if (status != QOpcUa::UaStatusCode::Good) {
        markBrowseError(item.nodeId, QOpcUa::statusToString(status));
        pump();
        return;
    }

    const bool parentIsRoot = item.depth == 0;
    const QString parentId = parentIsRoot && !m_options.captureRoot ? QString() : item.nodeId;

    // Large arrays (e.g. a 1000-row recipe table of structures) hold most of
    // a PLC's nodes; their elements can be limited.
    bool isArray = false;
    if (!parentIsRoot) {
        const auto parent = m_index.constFind(item.nodeId);
        if (parent != m_index.constEnd()) {
            const CloneNode &array = m_nodes.at(parent.value());
            isArray = array.isVariable
                      && (array.valueRank >= 1 || !array.arrayDimensions.isEmpty());
        }
    }
    const bool limitElements = isArray && m_options.maxArrayElements > 0;
    int elements = 0;
    int skippedElements = 0;
    QList<qsizetype> elementIndices;
    for (const QOpcUaReferenceDescription &reference : references) {
        const QString childId = reference.targetNodeId().nodeId();
        if (childId.isEmpty() || childId == m_rootNodeId)
            continue;

        const auto existing = m_index.constFind(childId);
        if (existing != m_index.constEnd()) {
            CloneNode &known = m_nodes[existing.value()];
            const QString referencingId = parentIsRoot ? m_rootNodeId : item.nodeId;
            const QString knownParent = known.parentNodeId.isEmpty() ? m_rootNodeId
                                                                     : known.parentNodeId;
            if (knownParent != referencingId && !known.additionalParentIds.contains(referencingId))
                known.additionalParentIds.append(referencingId);
            continue;
        }

        if (limitElements) {
            const QString name = reference.browseName().name();
            if (name.endsWith(QLatin1Char(']')) && name.contains(QLatin1Char('['))
                && ++elements > m_options.maxArrayElements) {
                ++skippedElements;
                continue;
            }
        }

        if (liveNodeCount() >= m_options.maxNodes) {
            m_completion.nodeLimitReached = true;
            break;
        }

        CloneNode child;
        child.nodeId = childId;
        child.parentNodeId = parentId;
        child.browseName = reference.browseName().name();
        child.browseNameNamespace = reference.browseName().namespaceIndex();
        child.displayName = reference.displayName().text();
        child.nodeClass = static_cast<int>(reference.nodeClass());
        child.referenceTypeId = reference.refTypeId();
        child.typeDefinitionId = reference.typeDefinition().nodeId();
        child.isVariable = reference.nodeClass() == QOpcUa::NodeClass::Variable;
        child.isFolder = reference.nodeClass() == QOpcUa::NodeClass::Object
                         && child.typeDefinitionId == kFolderTypeId;

        if (isArray && child.browseName.endsWith(QLatin1Char(']'))
            && child.browseName.contains(QLatin1Char('[')))
            elementIndices.append(m_nodes.size());
        appendNode(child);

        const int childDepth = item.depth + 1;
        if (childDepth >= m_options.maxDepth)
            ++m_completion.depthLimitedNodes;
        else if (child.isVariable)
            m_pendingBrowseDepth.insert(childId, childDepth);
        else
            m_browseQueue.enqueue({childId, childDepth});
        if (child.isVariable)
            m_readQueue.append(childId);
    }

    if (isArray)
        planExpansion(item, elementIndices);

    if (skippedElements > 0) {
        ++m_completion.limitedArrays;
        m_completion.arrayElementsSkipped += skippedElements;
        if (m_completion.limitedArrays <= kMaxLoggedReadFailures) {
            qCInfo(lcOpcUaClone).noquote()
                << "array limited:" << item.nodeId << "first" << m_options.maxArrayElements
                << "of" << (m_options.maxArrayElements + skippedElements) << "elements";
        }
    }

    pump();
}

/*!
 * \internal
 * \brief Records a browse failure \a reason for \a nodeId.
 */
void CloneBrowser::markBrowseError(const QString &nodeId, const QString &reason)
{
    ++m_completion.failedBrowses;
    if (nodeId == m_rootNodeId) {
        m_rootBrowseFailed = true;
        return;
    }
    const auto it = m_index.constFind(nodeId);
    if (it != m_index.constEnd())
        m_nodes[it.value()].browseError = reason;
}

/*!
 * \internal
 * \brief Issues one batched read of the metadata of \a metadataIds, the Value
 * of \a valueIds and the DataType of \a checkIds.
 *
 * Any list may be empty. The request is claimed later by the union of the
 * lists, which is unique among the requests in flight: a variable's Value is
 * only queued after its metadata request has been answered, and a built
 * variable is in exactly one of the value and check queues.
 */
void CloneBrowser::startRead(const QStringList &metadataIds, const QStringList &valueIds,
                             const QStringList &checkIds)
{
    QList<QOpcUaReadItem> items;
    items.reserve(metadataIds.size() * kMetadataAttributes.size() + valueIds.size()
                  + checkIds.size());
    for (const QString &nodeId : metadataIds) {
        for (const QOpcUa::NodeAttribute attribute : kMetadataAttributes)
            items.append(QOpcUaReadItem(nodeId, attribute));
    }
    for (const QString &nodeId : valueIds)
        items.append(QOpcUaReadItem(nodeId, QOpcUa::NodeAttribute::Value));
    for (const QString &nodeId : checkIds)
        items.append(QOpcUaReadItem(nodeId, QOpcUa::NodeAttribute::DataType));

    QSet<QString> keySet(metadataIds.cbegin(), metadataIds.cend());
    keySet.unite(QSet<QString>(valueIds.cbegin(), valueIds.cend()));
    keySet.unite(QSet<QString>(checkIds.cbegin(), checkIds.cend()));
    m_reading.append({metadataIds, valueIds, checkIds, keySet, m_elapsed.elapsed()});
    ++m_completion.readRequests;
    if (!m_client->readNodeAttributes(items)) {
        m_reading.removeLast();
        failRead(metadataIds, QStringLiteral("Read: read request could not be started"));
        failRead(valueIds, QStringLiteral("Value: read request could not be started"));
        failRead(checkIds, QStringLiteral("DataType: read request could not be started"));
    }
}

/*!
 * \internal
 * \brief Reads the NodeClass, BrowseName and DisplayName of the captured root.
 *
 * The read is claimed by its own connection (one node, NodeClass first), so it
 * never mixes with the metadata and Value batches.
 */
void CloneBrowser::startRootInfoRead()
{
    QList<QOpcUaReadItem> items;
    for (const QOpcUa::NodeAttribute attribute : kRootInfoAttributes)
        items.append(QOpcUaReadItem(m_rootNodeId, attribute));
    m_rootInfoConnection =
        connect(m_client, &QOpcUaClient::readNodeAttributesFinished, this,
                [this](const QList<QOpcUaReadResult> &results, QOpcUa::UaStatusCode) {
                    if (m_finished || results.isEmpty()
                        || results.first().nodeId() != m_rootNodeId
                        || results.first().attribute() != QOpcUa::NodeAttribute::NodeClass)
                        return;
                    disconnect(m_rootInfoConnection);
                    touch();
                    applyRootInfo(results);
                    pump();
                });
    if (!m_client->readNodeAttributes(items)) {
        disconnect(m_rootInfoConnection);
        m_nodes[0].attributeErrors.append(
            QStringLiteral("Read: read request could not be started"));
    }
}

/*!
 * \internal
 * \brief Stores the NodeClass, BrowseName and DisplayName of the captured root.
 *
 * A variable root is then queued for its metadata read like any other variable.
 */
void CloneBrowser::applyRootInfo(const QList<QOpcUaReadResult> &results)
{
    CloneNode &root = m_nodes[0];
    for (const QOpcUaReadResult &result : results) {
        if (isBad(result.statusCode())) {
            const QString error = QStringLiteral("%1: %2").arg(
                result.attribute() == QOpcUa::NodeAttribute::NodeClass ? QStringLiteral("NodeClass")
                : result.attribute() == QOpcUa::NodeAttribute::BrowseName
                    ? QStringLiteral("BrowseName")
                    : QStringLiteral("DisplayName"),
                QOpcUa::statusToString(result.statusCode()));
            root.attributeErrors.append(error);
            logReadFailure(root.nodeId, error);
            continue;
        }
        switch (result.attribute()) {
        case QOpcUa::NodeAttribute::NodeClass:
            root.nodeClass = result.value().toInt();
            break;
        case QOpcUa::NodeAttribute::BrowseName: {
            const auto name = result.value().value<QOpcUaQualifiedName>();
            root.browseName = name.name();
            root.browseNameNamespace = name.namespaceIndex();
            break;
        }
        case QOpcUa::NodeAttribute::DisplayName:
            root.displayName = result.value().value<QOpcUaLocalizedText>().text();
            break;
        default:
            break;
        }
    }
    root.isVariable = root.nodeClass == static_cast<int>(QOpcUa::NodeClass::Variable);
    if (root.isVariable)
        m_readQueue.prepend(root.nodeId);
}

/*!
 * \internal
 * \brief Applies a batched read result that belongs to one of this browser's requests.
 *
 * The signal is shared with other users of the client, so a result is claimed
 * only when its node-id set matches a request in flight. A service-level
 * failure carries no results; it is attributed to the oldest request in
 * flight. BadTooManyOperations splits that request and lowers the batch size,
 * so servers with a small MaxNodesPerRead still work. After the results are
 * stored, each variable whose metadata arrived is routed by routeVariable().
 */
void CloneBrowser::handleReadResults(const QList<QOpcUaReadResult> &results,
                                     QOpcUa::UaStatusCode serviceResult)
{
    if (m_finished || m_reading.isEmpty())
        return;

    if (results.isEmpty()) {
        if (serviceResult == QOpcUa::UaStatusCode::Good)
            return; // Not a failure of ours: nothing to claim.
        touch();
        const ReadBatch batch = m_reading.takeFirst();
        ++m_readWindow.count;
        m_readWindow.totalMs += m_elapsed.elapsed() - batch.startedMs;
        const qsizetype size =
            batch.metadataIds.size() + batch.valueIds.size() + batch.checkIds.size();
        // A too large request or response is split like too many operations.
        const bool tooLarge = serviceResult == QOpcUa::UaStatusCode::BadTooManyOperations
                              || serviceResult == QOpcUa::UaStatusCode::BadResponseTooLarge
                              || serviceResult == QOpcUa::UaStatusCode::BadRequestTooLarge
                              || serviceResult == QOpcUa::UaStatusCode::BadEncodingLimitsExceeded;
        if (tooLarge && size > 1) {
            if (!batch.metadataIds.isEmpty())
                m_options.variablesPerRead = std::max<int>(1, int(batch.metadataIds.size() / 2));
            else
                m_options.valuesPerRead = std::max<int>(1, int(size / 2));
            m_readQueue = batch.metadataIds + m_readQueue;
            m_valueQueue = batch.valueIds + m_valueQueue;
            m_checkQueue = batch.checkIds + m_checkQueue;
        } else {
            const QString status = QOpcUa::statusToString(serviceResult);
            failRead(batch.metadataIds, QStringLiteral("Read: %1").arg(status));
            failRead(batch.valueIds, QStringLiteral("Value: %1").arg(status));
            failRead(batch.checkIds, QStringLiteral("DataType: %1").arg(status));
        }
        pump();
        return;
    }

    QSet<QString> resultIds;
    for (const QOpcUaReadResult &result : results)
        resultIds.insert(result.nodeId());
    const auto batch = std::find_if(m_reading.begin(), m_reading.end(),
                                    [&resultIds](const ReadBatch &candidate) {
                                        return candidate.keySet == resultIds;
                                    });
    if (batch == m_reading.end())
        return; // A read issued by another component.
    const QStringList metadataIds = batch->metadataIds;
    const QStringList confirmIds = batch->valueIds + batch->checkIds;
    ++m_readWindow.count;
    m_readWindow.totalMs += m_elapsed.elapsed() - batch->startedMs;
    m_readWindow.items += int(results.size());
    m_reading.erase(batch);
    touch();

    for (const QOpcUaReadResult &result : results) {
        const auto it = m_index.constFind(result.nodeId());
        if (it != m_index.constEnd())
            applyReadResult(m_nodes[it.value()], result);
    }
    const auto unknownNode = [](const CloneNode &node) {
        return std::any_of(node.attributeErrors.cbegin(), node.attributeErrors.cend(),
                           [](const QString &error) {
                               return error.contains(QLatin1String("BadNodeIdUnknown"))
                                      || error.contains(QLatin1String("BadNodeIdInvalid"));
                           });
    };
    for (const QString &nodeId : metadataIds) {
        const auto it = m_index.constFind(nodeId);
        if (it == m_index.constEnd())
            continue;
        const CloneNode &node = m_nodes.at(it.value());
        // A built node the server does not know: its element is browsed instead.
        if (node.synthesized && node.dataTypeId.isEmpty() && unknownNode(node)) {
            handleTemplateMiss(node);
            continue;
        }
        routeVariable(nodeId);
    }
    // A built node with copied metadata is confirmed by its Value or DataType read.
    for (const QString &nodeId : confirmIds) {
        const auto it = m_index.constFind(nodeId);
        if (it == m_index.constEnd())
            continue;
        const CloneNode &node = m_nodes.at(it.value());
        if (node.metadataFromTemplate && unknownNode(node))
            handleTemplateMiss(node);
    }
    pump();
}

/*!
 * \internal
 * \brief Marks the variables \a nodeIds as not readable because of \a reason.
 *
 * A variable whose metadata could not be read has an unknown type, so it is
 * browsed for children: it may be a structure or an array.
 */
void CloneBrowser::failRead(const QStringList &nodeIds, const QString &reason)
{
    for (const QString &nodeId : nodeIds) {
        const auto it = m_index.constFind(nodeId);
        if (it == m_index.constEnd())
            continue;
        CloneNode &node = m_nodes[it.value()];
        node.attributesRead = true;
        node.attributeErrors.append(reason);
        logReadFailure(nodeId, reason);
        browseVariable(nodeId);
    }
}

/*!
 * \internal
 * \brief Settles variable \a nodeId once its DataType is known.
 *
 * A variable whose DataType attribute could not be read gets no Value read:
 * without a type it cannot be recreated faithfully, and its metadata error is
 * already recorded. It is browsed, because it may be a container such as a
 * CODESYS structure that holds a function block instance. A DataType outside
 * the namespace-0 tables is queued for a lookup; the variable waits until
 * startTypeLookup() has resolved it.
 */
void CloneBrowser::routeVariable(const QString &nodeId)
{
    const auto index = m_index.constFind(nodeId);
    if (index == m_index.constEnd())
        return;
    CloneNode &node = m_nodes[index.value()];
    if (node.dataTypeId.isEmpty()) {
        browseVariable(nodeId);
        return;
    }

    const QString typeId = DataTypeIds::normalize(node.dataTypeId);
    if (m_typeLookupGivenUp.contains(typeId)) {
        node.attributeErrors.append(QStringLiteral("Value: not read, DataType lookup timed out"));
        logReadFailure(nodeId, QStringLiteral("Value: not read, DataType lookup timed out"));
        browseVariable(nodeId);
        return;
    }
    auto cached = m_typeCache.constFind(typeId);
    if (cached == m_typeCache.constEnd()) {
        const ResolvedDataType local = DataTypeIds::resolveLocally(typeId);
        if (local.resolution == ResolvedDataType::Resolution::None) {
            const bool first = !m_waitingForType.contains(typeId);
            m_waitingForType[typeId].append(nodeId);
            if (first)
                m_typeQueue.append(typeId);
            return;
        }
        cached = m_typeCache.insert(typeId, local);
    }
    settleVariable(node, cached.value());
}

/*!
 * \internal
 * \brief Decides the browse and the Value read of \a node, whose DataType is \a type.
 *
 * The Value is queued unless \a type is a structure. A scalar of a simple type
 * is one of the probes of its TypeDefinition, waits for the probe's outcome,
 * is browsed after a probe found children, or is counted as not browsed once
 * all probes came back empty. Waiting instead of browsing keeps the decision
 * independent of how fast reads arrive compared with browses. Every other
 * variable is browsed.
 */
void CloneBrowser::settleVariable(CloneNode &node, const ResolvedDataType &type)
{
    // Without values only the Value read is left out; the browse decision
    // below still runs, so the members of structures are captured.
    if (type.builtInBase == kStructureBase)
        node.valueSkipped = true;
    else if (m_options.readValues)
        m_valueQueue.append(node.nodeId);

    if (!isScalarLeaf(node, type)) {
        browseVariable(node.nodeId);
        return;
    }

    if (!m_pendingBrowseDepth.contains(node.nodeId))
        return;
    LeafProbe &probe = m_leafProbes[node.typeDefinitionId];
    if (probe.hasChildren) {
        browseVariable(node.nodeId);
    } else if (probe.answered >= m_options.leafProbeCount) {
        m_pendingBrowseDepth.remove(node.nodeId);
        ++m_completion.leafVariablesNotBrowsed;
    } else if (probe.issued < m_options.leafProbeCount) {
        ++probe.issued;
        m_probeNodes.insert(node.nodeId, node.typeDefinitionId);
        browseVariable(node.nodeId);
    } else {
        probe.waiting.append(node.nodeId);
    }
}

/*!
 * \internal
 * \brief Returns whether \a node, of DataType \a type, is a scalar of a simple type.
 *
 * Only such variables may be left unbrowsed: a resolved type that is not a
 * structure, ValueRank -1 and no ArrayDimensions. CloneOptions::browseScalarLeaves
 * turns this off.
 */
bool CloneBrowser::isScalarLeaf(const CloneNode &node, const ResolvedDataType &type) const
{
    if (m_options.browseScalarLeaves)
        return false;
    const bool resolved = type.resolution == ResolvedDataType::Resolution::BuiltIn
                          || type.resolution == ResolvedDataType::Resolution::KnownSubtype
                          || type.resolution == ResolvedDataType::Resolution::Hierarchy;
    return resolved && !type.builtInBase.isEmpty() && type.builtInBase != kStructureBase
           && node.valueRank == -1 && node.arrayDimensions.isEmpty();
}

/*!
 * \internal
 * \brief Queues the browse of variable \a nodeId if it still waits for its decision.
 *
 * Variables at the depth limit and variables already decided are not queued.
 */
void CloneBrowser::browseVariable(const QString &nodeId)
{
    const auto it = m_pendingBrowseDepth.constFind(nodeId);
    if (it == m_pendingBrowseDepth.constEnd())
        return;
    const int depth = it.value();
    m_pendingBrowseDepth.erase(it);
    // An element waiting for its template is built, not browsed.
    if (m_deferredElements.contains(nodeId))
        return;
    m_browseQueue.enqueue({nodeId, depth});
}

/*!
 * \internal
 * \brief Records the answer of a probe browse of \a nodeId, which found children if \a hasChildren.
 *
 * A probe that finds children makes its TypeDefinition "always browse". When
 * all CloneOptions::leafProbeCount probes of a TypeDefinition came back
 * without children, its later scalar variables are no longer browsed.
 */
void CloneBrowser::recordProbe(const QString &nodeId, bool hasChildren)
{
    const auto it = m_probeNodes.constFind(nodeId);
    if (it == m_probeNodes.constEnd())
        return;
    const QString typeDefinition = it.value();
    m_probeNodes.erase(it);

    LeafProbe &probe = m_leafProbes[typeDefinition];
    ++probe.answered;
    if (hasChildren && !probe.hasChildren) {
        probe.hasChildren = true;
        qCInfo(lcOpcUaClone).noquote()
            << "scalar variables of TypeDefinition" << typeDefinition
            << "have children (e.g." << nodeId << "); all of them are browsed";
        for (const QString &waitingId : std::exchange(probe.waiting, {}))
            browseVariable(waitingId);
    } else if (!probe.hasChildren && probe.answered == m_options.leafProbeCount) {
        qCInfo(lcOpcUaClone).noquote()
            << probe.answered << "scalar variables of TypeDefinition" << typeDefinition
            << "had no children; further ones are not browsed";
        for (const QString &waitingId : std::exchange(probe.waiting, {})) {
            if (m_pendingBrowseDepth.remove(waitingId))
                ++m_completion.leafVariablesNotBrowsed;
        }
    }
}

/*!
 * \internal
 * \brief Appends \a node to the captured nodes and indexes it by id and by parent.
 */
void CloneBrowser::appendNode(const CloneNode &node)
{
    const qsizetype index = m_nodes.size();
    m_index.insert(node.nodeId, index);
    m_children[node.parentNodeId].append(index);
    m_nodes.append(node);
}

/*!
 * \internal
 * \brief Plans the template expansion of the array browsed as \a item.
 * \param item The browsed array variable.
 * \param elementIndices Indices of the element children captured by this browse.
 *
 * The first element stays the template and is browsed normally; the other
 * elements are deferred: they are captured and read, but not browsed, and
 * their subtrees are built by expandJob(). An array qualifies only when it has
 * at least kTemplateMinElements elements, a string NodeId, and every element
 * follows the pattern NodeId "<array>[i]" with BrowseName "<arrayName>[i]".
 */
void CloneBrowser::planExpansion(const BrowseItem &item, const QList<qsizetype> &elementIndices)
{
    if (!m_options.expandArraysByTemplate || elementIndices.size() < kTemplateMinElements
        || !item.nodeId.contains(QLatin1String(";s=")))
        return;
    const auto arrayIt = m_index.constFind(item.nodeId);
    if (arrayIt == m_index.constEnd())
        return;
    const QString arrayName = m_nodes.at(arrayIt.value()).browseName;

    for (const qsizetype index : elementIndices) {
        const CloneNode &element = m_nodes.at(index);
        if (!element.isVariable || !element.browseName.startsWith(arrayName))
            return;
        const QString suffix = element.browseName.mid(arrayName.size());
        if (!suffix.startsWith(QLatin1Char('[')) || element.nodeId != item.nodeId + suffix)
            return;
    }

    ExpansionJob job;
    job.arrayId = item.nodeId;
    job.templateId = m_nodes.at(elementIndices.first()).nodeId;
    job.depth = item.depth + 1;
    for (qsizetype i = 1; i < elementIndices.size(); ++i) {
        const QString elementId = m_nodes.at(elementIndices.at(i)).nodeId;
        job.elementIds.append(elementId);
        m_deferredElements.insert(elementId, job.depth);
    }
    m_jobs.append(job);
}

/*!
 * \internal
 * \brief Expands every planned array, deepest first.
 *
 * Jobs are expanded in reverse order of planning: an array nested inside a
 * template element is planned after its outer array, so it is complete before
 * the outer template is copied.
 */
void CloneBrowser::expandJobs()
{
    const QList<ExpansionJob> jobs = std::exchange(m_jobs, {});
    for (auto it = jobs.crbegin(); it != jobs.crend(); ++it)
        expandJob(*it);
}

/*!
 * \internal
 * \brief Builds the subtree of \a job's template element for each of its other elements.
 *
 * Every node below the template is copied with the "<template>" NodeId prefix
 * replaced by the element's NodeId; BrowseName, DisplayName, NodeClass and
 * TypeDefinition are copied. With CloneOptions::copyTemplateMetadata, a
 * variable whose template node has its metadata (read without errors, or
 * itself copied) also takes DataType, ValueRank, ArrayDimensions and access
 * levels from it and is confirmed by reading only its Value, or its DataType
 * when no Value is read; other variables get the full metadata read. A
 * template that contains an Object cannot be verified by a variable read, so
 * its elements are browsed normally instead.
 */
void CloneBrowser::expandJob(const ExpansionJob &job)
{
    // The template subtree, parents before children.
    QList<qsizetype> subtree;
    QList<QString> pending{job.templateId};
    bool hasObject = false;
    while (!pending.isEmpty()) {
        const QString parentId = pending.takeFirst();
        for (const qsizetype index : m_children.value(parentId)) {
            const CloneNode &node = m_nodes.at(index);
            if (node.missing)
                continue;
            if (!node.isVariable)
                hasObject = true;
            subtree.append(index);
            pending.append(node.nodeId);
        }
    }

    if (hasObject) {
        qCInfo(lcOpcUaClone).noquote()
            << "template" << job.templateId << "contains an Object; elements are browsed";
        for (const QString &elementId : job.elementIds) {
            m_deferredElements.remove(elementId);
            m_browseQueue.enqueue({elementId, job.depth});
        }
        return;
    }

    // Every template node must map to the other elements; otherwise the
    // elements are browsed, so no node is silently left out.
    const bool mappable = job.elementIds.isEmpty()
                          || std::all_of(subtree.cbegin(), subtree.cend(), [&](qsizetype index) {
                                 return !mapTemplateId(m_nodes.at(index).nodeId, job.templateId,
                                                       job.elementIds.first())
                                             .isEmpty();
                             });
    if (!mappable) {
        qCInfo(lcOpcUaClone).noquote()
            << "template" << job.templateId << "has NodeIds outside its path; elements are browsed";
        for (const QString &elementId : job.elementIds) {
            m_deferredElements.remove(elementId);
            m_browseQueue.enqueue({elementId, job.depth});
        }
        return;
    }

    int built = 0;
    int unmapped = 0;
    for (const QString &elementId : job.elementIds) {
        m_deferredElements.remove(elementId);
        if (!m_index.contains(elementId))
            continue;
        m_expandedElements.insert(elementId, job.depth);
        // Template node id -> its copy for this element; parents come first.
        QHash<QString, QString> copied{{job.templateId, elementId}};
        for (const qsizetype index : std::as_const(subtree)) {
            if (liveNodeCount() >= m_options.maxNodes) {
                m_completion.nodeLimitReached = true;
                break;
            }
            const CloneNode &source = m_nodes.at(index);
            const QString parentCopy = copied.value(source.parentNodeId);
            const QString copyId = mapTemplateId(source.nodeId, job.templateId, elementId);
            if (parentCopy.isEmpty() || copyId.isEmpty()) {
                ++unmapped; // browsed by the element's fallback if it is real
                continue;
            }
            copied.insert(source.nodeId, copyId);
            CloneNode copy;
            copy.nodeId = copyId;
            if (m_index.contains(copy.nodeId))
                continue;
            copy.parentNodeId = parentCopy;
            copy.browseName = source.browseName;
            copy.browseNameNamespace = source.browseNameNamespace;
            copy.displayName = source.displayName;
            copy.nodeClass = source.nodeClass;
            copy.typeDefinitionId = source.typeDefinitionId;
            copy.isVariable = source.isVariable;
            copy.isFolder = source.isFolder;
            copy.synthesized = true;
            copy.expansionElementId = elementId;
            const bool sourceHasMetadata =
                source.metadataFromTemplate
                || (source.attributesRead && !source.dataTypeId.isEmpty()
                    && source.attributeErrors.isEmpty());
            const bool copyMetadata =
                copy.isVariable && m_options.copyTemplateMetadata && sourceHasMetadata;
            if (copyMetadata) {
                copy.dataTypeId = source.dataTypeId;
                copy.valueRank = source.valueRank;
                copy.arrayDimensions = source.arrayDimensions;
                copy.accessLevel = source.accessLevel;
                copy.userAccessLevel = source.userAccessLevel;
                copy.valueSkipped = source.valueSkipped;
                copy.metadataFromTemplate = true;
            }
            appendNode(copy);
            if (copyMetadata) {
                ++m_completion.templateMetadataNodes;
                if (!copy.valueSkipped && m_options.readValues)
                    m_valueQueue.append(copy.nodeId);
                else
                    m_checkQueue.append(copy.nodeId);
            } else if (copy.isVariable) {
                m_readQueue.append(copy.nodeId);
            }
            ++built;
        }
    }
    if (built > 0) {
        ++m_completion.expandedArrays;
        m_completion.synthesizedNodes += built;
    }
    qCInfo(lcOpcUaClone).noquote()
        << "array expanded:" << job.arrayId << "template" << job.templateId << "elements"
        << job.elementIds.size() << "built nodes" << built << "unmapped" << unmapped;
}

/*!
 * \internal
 * \brief Drops the nodes built for the element of \a node and browses that element instead.
 *
 * \a node is a built node the server does not know, so the element differs
 * from its template. Its built nodes are marked missing (never published) and
 * removed from the index, so the browse can capture the real children.
 */
void CloneBrowser::handleTemplateMiss(const CloneNode &node)
{
    const QString elementId = node.expansionElementId;
    if (elementId.isEmpty() || m_fallbackElements.contains(elementId))
        return;
    m_fallbackElements.insert(elementId);
    ++m_completion.templateMisses;
    if (m_completion.templateMisses <= kMaxLoggedReadFailures) {
        qCInfo(lcOpcUaClone).noquote()
            << "template miss:" << node.nodeId << "does not exist; browsing" << elementId;
    }

    QSet<QString> dropped;
    QList<QString> pending{elementId};
    while (!pending.isEmpty()) {
        const QString parentId = pending.takeFirst();
        for (const qsizetype index : m_children.value(parentId)) {
            CloneNode &built = m_nodes[index];
            if (!built.synthesized || built.missing)
                continue;
            built.missing = true;
            ++m_missingNodes;
            if (built.metadataFromTemplate)
                --m_completion.templateMetadataNodes;
            m_index.remove(built.nodeId);
            dropped.insert(built.nodeId);
            pending.append(built.nodeId);
        }
    }
    m_completion.synthesizedNodes -= int(dropped.size());
    const auto isDropped = [&dropped](const QString &id) { return dropped.contains(id); };
    m_readQueue.removeIf(isDropped);
    m_valueQueue.removeIf(isDropped);
    m_checkQueue.removeIf(isDropped);
    m_browseQueue.enqueue({elementId, m_expandedElements.value(elementId, 1)});
}

/*!
 * \internal
 * \brief Reads the server's MaxNodesPerRead to size the read batches.
 */
void CloneBrowser::startLimitsRead()
{
    m_limitsPending = true;
    m_limitsConnection =
        connect(m_client, &QOpcUaClient::readNodeAttributesFinished, this,
                [this](const QList<QOpcUaReadResult> &results, QOpcUa::UaStatusCode) {
                    if (!m_limitsPending || results.size() != 1
                        || results.first().nodeId() != kMaxNodesPerReadId)
                        return;
                    applyLimits(results);
                });
    if (!m_client->readNodeAttributes({QOpcUaReadItem(kMaxNodesPerReadId,
                                                      QOpcUa::NodeAttribute::Value)}))
        applyLimits({});
}

/*!
 * \internal
 * \brief Sizes the read batches from the MaxNodesPerRead read \a results.
 *
 * 0 means no limit: the batches grow to CloneOptions::maxVariablesPerRead and
 * CloneOptions::maxValuesPerRead. A positive limit is divided by the
 * operations per variable. When the limit cannot be read the default sizes
 * stay. BadTooManyOperations still halves a batch that is too large.
 */
void CloneBrowser::applyLimits(const QList<QOpcUaReadResult> &results)
{
    if (!m_limitsPending)
        return;
    m_limitsPending = false;
    disconnect(m_limitsConnection);
    if (results.isEmpty() || isBad(results.first().statusCode())
        || !results.first().value().isValid()) {
        qCInfo(lcOpcUaClone) << "MaxNodesPerRead not available; read batches stay"
                             << m_options.variablesPerRead << "/" << m_options.valuesPerRead;
        return;
    }
    const quint32 limit = results.first().value().toUInt();
    m_completion.serverMaxNodesPerRead = int(std::min<quint32>(limit, quint32(INT_MAX)));
    if (limit == 0) {
        m_options.variablesPerRead = m_options.maxVariablesPerRead;
        m_options.valuesPerRead = m_options.maxValuesPerRead;
    } else {
        m_options.variablesPerRead = std::clamp<int>(int(limit / kOperationsPerVariable), 1,
                                                     m_options.maxVariablesPerRead);
        m_options.valuesPerRead = std::clamp<int>(int(std::min<quint32>(limit, 1u << 20)), 1,
                                                  m_options.maxValuesPerRead);
    }
    qCInfo(lcOpcUaClone).noquote()
        << "server MaxNodesPerRead" << limit << "-> variables per metadata read"
        << m_options.variablesPerRead << ", per value read" << m_options.valuesPerRead;
}

/*!
 * \internal
 * \brief Writes the 30-second progress line with the request statistics and resets them.
 *
 * The line shows the queues and, per request kind, how many requests were
 * answered in the window and their average latency, so a slow server can be
 * told apart from a slow client.
 */
void CloneBrowser::logProgress()
{
    const CloneProgress snapshot = progressSnapshot();
    const auto average = [](const RequestStats &stats) {
        return stats.count > 0 ? stats.totalMs / stats.count : 0;
    };
    qCInfo(lcOpcUaClone).noquote()
        << "progress: captured" << snapshot.captured << "toBrowse" << snapshot.browseQueued
        << "browsing" << snapshot.browsing << "metadataQueued" << snapshot.metadataQueued
        << "valuesQueued" << snapshot.valuesQueued << "waitingForType"
        << snapshot.waitingForType << "readRequests" << snapshot.readRequests << "typeLookups"
        << snapshot.typeLookups << "| last 30 s: browses" << m_browseWindow.count << "avgMs"
        << average(m_browseWindow) << "empty" << m_browseWindow.empty << "reads"
        << m_readWindow.count << "avgMs" << average(m_readWindow) << "operations"
        << m_readWindow.items << "| leavesNotBrowsed" << m_completion.leafVariablesNotBrowsed
        << "waitingForTemplate" << snapshot.waitingForTemplate << "synthesized"
        << m_completion.synthesizedNodes
        << "elapsedMs" << m_elapsed.elapsed();
    m_browseWindow = RequestStats();
    m_readWindow = RequestStats();
}

/*!
 * \internal
 * \brief Starts one DataType lookup for the queued type ids, unless one is running.
 *
 * Lookups run one at a time with DataTypeResolver's own concurrency limit, so
 * they never compete with the browse and read requests for more than a few
 * request slots.
 */
void CloneBrowser::startTypeLookup()
{
    if (m_finished || m_typeLookup || m_typeQueue.isEmpty() || !m_client)
        return;
    m_typeLookupIds = m_typeQueue;
    m_typeQueue.clear();
    m_typeLookup = new DataTypeResolver(m_client, this);
    m_typeLookup->setTimeoutMs(m_options.typeLookupTimeoutMs);
    connect(m_typeLookup, &DataTypeResolver::finished, this,
            &CloneBrowser::handleTypeLookupFinished);
    m_typeLookup->resolve(m_typeLookupIds);
}

/*!
 * \internal
 * \brief Caches the resolved DataTypes and routes the variables waiting for them.
 *
 * When the round timed out, a type that is still unresolved is not cached: it
 * is looked up again in a later round, and its variables keep waiting. After
 * CloneOptions::typeLookupAttempts timed-out rounds the type is given up.
 */
void CloneBrowser::handleTypeLookupFinished()
{
    DataTypeResolver *lookup = m_typeLookup;
    m_typeLookup = nullptr;
    if (lookup)
        lookup->deleteLater();
    if (m_finished || !lookup)
        return;
    touch();

    const bool timedOut = lookup->timedOut();
    const QStringList typeIds = std::exchange(m_typeLookupIds, {});
    for (const QString &typeId : typeIds) {
        const ResolvedDataType resolved = lookup->result(typeId);
        const bool unresolved = resolved.resolution == ResolvedDataType::Resolution::None
                                || resolved.resolution == ResolvedDataType::Resolution::Unresolved;
        if (timedOut && unresolved) {
            const int rounds = ++m_typeLookupRounds[typeId];
            qCInfo(lcOpcUaClone).noquote()
                << "DataType lookup timed out:" << typeId << "round" << rounds << "of"
                << m_options.typeLookupAttempts;
            if (rounds < m_options.typeLookupAttempts)
                m_typeQueue.append(typeId);
            else
                failTypeLookup(typeId);
            continue;
        }
        m_typeCache.insert(typeId, resolved);
        const QStringList waiting = m_waitingForType.take(typeId);
        for (const QString &nodeId : waiting) {
            const auto it = m_index.constFind(nodeId);
            if (it != m_index.constEnd())
                settleVariable(m_nodes[it.value()], resolved);
        }
    }
    pump();
}

/*!
 * \internal
 * \brief Gives up the DataType \a typeId: its variables get no Value read.
 *
 * The waiting variables and every later variable of this type record the
 * error, so the clone reports them as incomplete. The final resolution after
 * the traversal still tries the type once more for its name and base.
 */
void CloneBrowser::failTypeLookup(const QString &typeId)
{
    m_typeLookupGivenUp.insert(typeId);
    const QStringList waiting = m_waitingForType.take(typeId);
    for (const QString &nodeId : waiting) {
        const auto it = m_index.constFind(nodeId);
        if (it == m_index.constEnd())
            continue;
        const QString error = QStringLiteral("Value: not read, DataType lookup timed out");
        m_nodes[it.value()].attributeErrors.append(error);
        logReadFailure(nodeId, error);
        browseVariable(nodeId); // an unknown type may be a structure
    }
}

/*!
 * \internal
 * \brief Returns the current breakdown of queued and in-flight work.
 */
CloneProgress CloneBrowser::progressSnapshot() const
{
    CloneProgress snapshot;
    snapshot.captured = int(liveNodeCount());
    snapshot.browseQueued = int(m_browseQueue.size());
    snapshot.browsing = int(m_browsing.size());
    snapshot.metadataQueued = int(m_readQueue.size());
    snapshot.valuesQueued = int(m_valueQueue.size() + m_checkQueue.size());
    for (const QStringList &nodeIds : m_waitingForType)
        snapshot.waitingForType += int(nodeIds.size());
    for (const ReadBatch &batch : m_reading)
        snapshot.reading +=
            int(batch.metadataIds.size() + batch.valueIds.size() + batch.checkIds.size());
    snapshot.readRequests = int(m_reading.size());
    snapshot.typeLookups = m_typeLookup ? int(m_typeLookupIds.size()) : 0;
    snapshot.waitingForTemplate = int(m_deferredElements.size());
    return snapshot;
}

/*!
 * \internal
 * \brief Logs the read failure \a what of \a nodeId; only the first ones are detailed.
 */
void CloneBrowser::logReadFailure(const QString &nodeId, const QString &what)
{
    if (m_loggedReadFailures < kMaxLoggedReadFailures)
        qCInfo(lcOpcUaClone).noquote() << "read failed:" << nodeId << what;
    else if (m_loggedReadFailures == kMaxLoggedReadFailures)
        qCInfo(lcOpcUaClone) << "further read failures are only counted";
    ++m_loggedReadFailures;
}

/*!
 * \internal
 * \brief Stores one attribute of \a result in \a node, or its failure status.
 *
 * Bad statuses are recorded in CloneNode::attributeErrors and leave the
 * attribute at its "not read" default. An absent ArrayDimensions attribute is
 * optional for variables and therefore not an error.
 */
void CloneBrowser::applyReadResult(CloneNode &node, const QOpcUaReadResult &result)
{
    node.attributesRead = true;
    const QOpcUa::UaStatusCode status = result.statusCode();
    const QOpcUa::NodeAttribute attribute = result.attribute();

    if (attribute == QOpcUa::NodeAttribute::Value) {
        node.valueRead = true;
        node.valueStatus = static_cast<quint32>(status);
    }

    if (isBad(status)) {
        if (attribute == QOpcUa::NodeAttribute::ArrayDimensions)
            return;
        const QString error =
            QStringLiteral("%1: %2").arg(attributeName(attribute), QOpcUa::statusToString(status));
        node.attributeErrors.append(error);
        logReadFailure(node.nodeId, error);
        return;
    }

    switch (attribute) {
    case QOpcUa::NodeAttribute::Value:
        node.value = result.value();
        node.sourceTimestamp = result.sourceTimestamp();
        node.serverTimestamp = result.serverTimestamp();
        break;
    case QOpcUa::NodeAttribute::DataType:
        node.dataTypeId = result.value().toString();
        break;
    case QOpcUa::NodeAttribute::ValueRank:
        if (result.value().isValid())
            node.valueRank = result.value().toInt();
        break;
    case QOpcUa::NodeAttribute::ArrayDimensions:
        node.arrayDimensions = toArrayDimensions(result.value());
        break;
    case QOpcUa::NodeAttribute::AccessLevel:
        if (result.value().isValid())
            node.accessLevel = int(result.value().toUInt());
        break;
    case QOpcUa::NodeAttribute::UserAccessLevel:
        if (result.value().isValid())
            node.userAccessLevel = int(result.value().toUInt());
        break;
    default:
        break;
    }
}

/*!
 * \internal
 * \brief Restarts the idle timeout after a response.
 */
void CloneBrowser::touch()
{
    if (m_idleTimer)
        m_idleTimer->start(m_options.idleTimeoutMs);
}

/*!
 * \internal
 * \brief Returns the number of nodes whose browse or read is queued or in flight.
 */
int CloneBrowser::pendingCount() const
{
    return progressSnapshot().pending();
}

/*!
 * \internal
 * \brief Ends the traversal once nothing is queued or in flight.
 */
void CloneBrowser::finishIfIdle()
{
    if (m_finished || !m_browseQueue.isEmpty() || !m_browsing.isEmpty()
        || !m_readQueue.isEmpty() || !m_valueQueue.isEmpty() || !m_checkQueue.isEmpty()
        || !m_reading.isEmpty()
        || !m_waitingForType.isEmpty() || m_typeLookup)
        return;
    if (!m_jobs.isEmpty()) {
        expandJobs();
        pump();
        return;
    }
    const bool truncated = m_completion.nodeLimitReached || m_completion.depthLimitedNodes > 0;
    finish(truncated ? CloneCompletion::State::Truncated : CloneCompletion::State::Complete);
}

/*!
 * \internal
 * \brief Ends the traversal with \a state and resolves the captured DataType ids.
 *
 * Responses arriving afterwards are ignored. A root that could not be browsed
 * with nothing captured turns the result into Failed. The remaining DataType
 * ids are resolved and the server identity is read in parallel; finished() is
 * emitted once both have settled. A cancelled traversal is published at once,
 * without type resolution or identity.
 */
void CloneBrowser::finish(CloneCompletion::State state)
{
    if (m_finished)
        return;
    m_finished = true;

    if (m_overallTimer)
        m_overallTimer->stop();
    if (m_idleTimer)
        m_idleTimer->stop();
    if (m_progressTimer)
        m_progressTimer->stop();
    if (m_readConnection)
        disconnect(m_readConnection);
    if (m_limitsConnection)
        disconnect(m_limitsConnection);
    if (m_rootInfoConnection)
        disconnect(m_rootInfoConnection);

    // Built nodes the server does not have are never published.
    m_nodes.removeIf([](const CloneNode &node) { return node.missing; });

    // A captured root alone (its browse failed) is no result either.
    const qsizetype minimumNodes = m_options.captureRoot ? 1 : 0;
    if (m_rootBrowseFailed && m_nodes.size() <= minimumNodes
        && state != CloneCompletion::State::Cancelled)
        state = CloneCompletion::State::Failed;

    m_completion.state = state;
    m_completion.nodeCount = int(m_nodes.size());
    m_completion.pendingRequests = pendingCount();
    // Containers without a DataType (a CODESYS structure holding a function
    // block) are complete through their members and are not failed reads.
    markContainersWithoutDataType(m_nodes);
    m_completion.failedReads = int(std::count_if(
        m_nodes.cbegin(), m_nodes.cend(), [](const CloneNode &node) {
            return !node.attributeErrors.isEmpty() && !node.dataTypeNotProvided;
        }));
    m_completion.options = m_options;
    m_completion.elapsedMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    m_resultNamespaces = m_client ? m_client->namespaceArray() : QStringList();

    qCInfo(lcOpcUaClone).noquote()
        << "traversal ended: state" << int(state) << "nodes" << m_completion.nodeCount
        << "pending" << m_completion.pendingRequests << "failedBrowses"
        << m_completion.failedBrowses << "failedReads" << m_completion.failedReads
        << "depthLimited" << m_completion.depthLimitedNodes << "browseRequests"
        << m_completion.browseRequests << "readRequests" << m_completion.readRequests
        << "leavesNotBrowsed" << m_completion.leafVariablesNotBrowsed << "limitedArrays"
        << m_completion.limitedArrays << "arrayElementsSkipped"
        << m_completion.arrayElementsSkipped << "expandedArrays" << m_completion.expandedArrays
        << "synthesizedNodes" << m_completion.synthesizedNodes << "templateMisses"
        << m_completion.templateMisses << "templateMetadataNodes"
        << m_completion.templateMetadataNodes << "maxNodesPerRead"
        << m_completion.serverMaxNodesPerRead << "elapsedMs" << m_completion.elapsedMs;

    QStringList typeIds;
    QSet<QString> seen;
    for (const CloneNode &node : std::as_const(m_nodes)) {
        if (!node.isVariable || node.dataTypeId.isEmpty() || seen.contains(node.dataTypeId))
            continue;
        seen.insert(node.dataTypeId);
        // Types settled during the traversal are reused from the cache.
        if (!m_typeCache.contains(DataTypeIds::normalize(node.dataTypeId)))
            typeIds.append(node.dataTypeId);
    }

    if (!m_client || state == CloneCompletion::State::Cancelled) {
        publishIfReady();
        return;
    }

    startIdentityRead();
    if (!typeIds.isEmpty()) {
        m_resolverPending = true;
        m_resolver = new DataTypeResolver(m_client, this);
        m_resolver->setTimeoutMs(m_options.typeLookupTimeoutMs);
        connect(m_resolver, &DataTypeResolver::finished, this, [this]() {
            m_resolverPending = false;
            publishIfReady();
        });
        m_resolver->resolve(typeIds);
    }
    publishIfReady();
}

/*!
 * \internal
 * \brief Records the session endpoint and reads the server's identity variables.
 *
 * One batched Value read of ServerArray and the BuildInfo members; the shared
 * result signal is claimed by its node-id set. A failed or missing answer
 * leaves the fields empty after kIdentityTimeoutMs.
 */
void CloneBrowser::startIdentityRead()
{
    const QOpcUaEndpointDescription endpoint = m_client->endpoint();
    m_completion.server.endpointUrl = endpoint.endpointUrl();
    m_completion.server.securityMode = securityModeName(endpoint.securityMode());
    m_completion.server.securityPolicy = endpoint.securityPolicy();

    QList<QOpcUaReadItem> items;
    for (const QString &nodeId : kIdentityNodeIds)
        items.append(QOpcUaReadItem(nodeId, QOpcUa::NodeAttribute::Value));

    m_identityPending = true;
    m_identityConnection =
        connect(m_client, &QOpcUaClient::readNodeAttributesFinished, this,
                [this](const QList<QOpcUaReadResult> &results, QOpcUa::UaStatusCode) {
                    QSet<QString> ids;
                    for (const QOpcUaReadResult &result : results)
                        ids.insert(result.nodeId());
                    if (!m_identityPending
                        || ids != QSet<QString>(kIdentityNodeIds.cbegin(), kIdentityNodeIds.cend()))
                        return;
                    applyIdentity(results);
                });
    QTimer::singleShot(kIdentityTimeoutMs, this, [this]() { applyIdentity({}); });

    if (!m_client->readNodeAttributes(items))
        applyIdentity({});
}

/*!
 * \internal
 * \brief Stores the identity read \a results, if any, and publishes when ready.
 */
void CloneBrowser::applyIdentity(const QList<QOpcUaReadResult> &results)
{
    if (!m_identityPending)
        return;
    m_identityPending = false;
    disconnect(m_identityConnection);

    ServerIdentity &server = m_completion.server;
    for (const QOpcUaReadResult &result : results) {
        if (isBad(result.statusCode()))
            continue;
        const QVariant value = result.value();
        const qsizetype index = kIdentityNodeIds.indexOf(result.nodeId());
        switch (index) {
        case 0: {
            // Qt OPC UA returns a one-element array as a scalar.
            const QVariantList list = value.typeId() == QMetaType::QVariantList
                                          ? value.toList()
                                          : QVariantList{value};
            server.applicationUri = list.isEmpty() ? QString() : list.first().toString();
            break;
        }
        case 1: server.productName = identityText(value); break;
        case 2: server.productUri = identityText(value); break;
        case 3: server.manufacturerName = identityText(value); break;
        case 4: server.softwareVersion = identityText(value); break;
        case 5: server.buildNumber = identityText(value); break;
        case 6: server.buildDate = value.toDateTime(); break;
        default: break;
        }
    }
    publishIfReady();
}

/*!
 * \internal
 * \brief Emits the result once the type resolution and identity read have settled.
 */
void CloneBrowser::publishIfReady()
{
    if (m_published || m_resolverPending || m_identityPending)
        return;
    m_published = true;
    publishResult();
}

/*!
 * \internal
 * \brief Attaches the resolved DataTypes and emits finished() exactly once.
 *
 * A variable whose DataType attribute was never read keeps an empty id and is
 * reported as unresolved, so consumers do not mistake it for a known type.
 */
void CloneBrowser::publishResult()
{
    const DataTypeHierarchy localOnly;
    QHash<QString, ResolvedDataType> cache;
    for (CloneNode &node : m_nodes) {
        if (!node.isVariable)
            continue;
        auto it = cache.constFind(node.dataTypeId);
        if (it == cache.constEnd()) {
            const auto known = m_typeCache.constFind(DataTypeIds::normalize(node.dataTypeId));
            ResolvedDataType resolved = known != m_typeCache.constEnd() ? known.value()
                                        : m_resolver ? m_resolver->result(node.dataTypeId)
                                                     : localOnly.resolve(node.dataTypeId);
            if (node.dataTypeId.isEmpty())
                resolved.reason = QStringLiteral("DataType attribute was not read");
            it = cache.insert(node.dataTypeId, resolved);
            qCDebug(lcOpcUaClone).noquote()
                << "DataType" << (node.dataTypeId.isEmpty() ? QStringLiteral("<none>")
                                                            : node.dataTypeId)
                << "name:" << resolved.typeName << "base:" << resolved.builtInBase
                << "resolution:" << int(resolved.resolution) << resolved.reason;
        }
        node.resolvedDataType = it.value();
    }

    emit finished(m_requestId, m_nodes, m_resultNamespaces, m_completion);
    deleteLater();
}
