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
 * Browsing and reading are independent. A node is queued for browsing as soon
 * as it is discovered, whatever its node class, so the members of a structure
 * or the elements of an array are found even when the structure's own value
 * cannot be read or decoded. Variables are additionally queued for a batched
 * metadata read of DataType, ValueRank, ArrayDimensions, AccessLevel and
 * UserAccessLevel; each attribute keeps its status, so a failed read stays
 * visible instead of turning into a default value.
 *
 * The Value is read in a second, separate batch and only for variables whose
 * DataType is not a structure. A structure's members are its child nodes, and
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
 * The Value is read separately, see CloneBrowser::queueValueRead().
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
        const CloneProgress snapshot = progressSnapshot();
        if (++m_progressTicks >= kProgressLogTicks) {
            m_progressTicks = 0;
            qCInfo(lcOpcUaClone).noquote()
                << "progress: captured" << snapshot.captured << "toBrowse"
                << snapshot.browseQueued << "browsing" << snapshot.browsing << "metadataQueued"
                << snapshot.metadataQueued << "valuesQueued" << snapshot.valuesQueued
                << "waitingForType" << snapshot.waitingForType << "readRequests"
                << snapshot.readRequests << "typeLookups" << snapshot.typeLookups
                << "elapsedMs" << m_elapsed.elapsed();
        }
        emit progress(m_requestId, snapshot);
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
        m_index.insert(m_rootNodeId, 0);
        m_nodes.append(root);
        startRead({m_rootNodeId}, ReadKind::RootInfo);
    }

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

    // While metadata is still queued, a Value read waits for a full batch:
    // every metadata batch adds only a few values, and sending those at once
    // would take the few read slots with small requests and double the number
    // of round trips to a slow PLC. Once no metadata is queued, the remaining
    // values are read in whatever batch sizes are left.
    while (m_reading.size() < m_options.maxConcurrentReads) {
        const bool metadataQueued = !m_readQueue.isEmpty();
        const bool values = m_valueQueue.size() >= m_options.valuesPerRead
                            || (!metadataQueued && !m_valueQueue.isEmpty());
        if (!values && !metadataQueued)
            break;
        QStringList &queue = values ? m_valueQueue : m_readQueue;
        const int perRead = values ? m_options.valuesPerRead : m_options.variablesPerRead;
        const qsizetype count = std::min<qsizetype>(perRead, queue.size());
        const QStringList batch = queue.mid(0, count);
        queue.remove(0, count);
        startRead(batch, values ? ReadKind::Values : ReadKind::Metadata);
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

    connect(node, &QOpcUaNode::browseFinished, this,
            [this, item, node](const QList<QOpcUaReferenceDescription> &references,
                               QOpcUa::UaStatusCode status) {
                handleBrowse(item, node, references, status);
            },
            Qt::SingleShotConnection);

    if (!node->browseChildren(QOpcUa::ReferenceTypeId::HierarchicalReferences,
                              QOpcUa::NodeClass::Object | QOpcUa::NodeClass::Variable)) {
        m_browsing.remove(node);
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
 * captured but not browsed.
 */
void CloneBrowser::handleBrowse(const BrowseItem &item, QOpcUaNode *node,
                                const QList<QOpcUaReferenceDescription> &references,
                                QOpcUa::UaStatusCode status)
{
    m_browsing.remove(node);
    node->deleteLater();
    if (m_finished)
        return;
    touch();

    if (status != QOpcUa::UaStatusCode::Good) {
        markBrowseError(item.nodeId, QOpcUa::statusToString(status));
        pump();
        return;
    }

    const bool parentIsRoot = item.depth == 0;
    const QString parentId = parentIsRoot && !m_options.captureRoot ? QString() : item.nodeId;
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

        if (m_nodes.size() >= m_options.maxNodes) {
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

        m_index.insert(childId, m_nodes.size());
        m_nodes.append(child);

        const int childDepth = item.depth + 1;
        if (childDepth < m_options.maxDepth)
            m_browseQueue.enqueue({childId, childDepth});
        else
            ++m_completion.depthLimitedNodes;
        if (child.isVariable)
            m_readQueue.append(childId);
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
 * \brief Issues one batched read for the variables \a nodeIds.
 *
 * \a kind selects the Value attribute, the metadata attributes or the root
 * information attributes.
 */
void CloneBrowser::startRead(const QStringList &nodeIds, ReadKind kind)
{
    QList<QOpcUa::NodeAttribute> attributes;
    switch (kind) {
    case ReadKind::Metadata: attributes = kMetadataAttributes; break;
    case ReadKind::Values: attributes = {QOpcUa::NodeAttribute::Value}; break;
    case ReadKind::RootInfo: attributes = kRootInfoAttributes; break;
    }

    QList<QOpcUaReadItem> items;
    items.reserve(nodeIds.size() * attributes.size());
    for (const QString &nodeId : nodeIds) {
        for (const QOpcUa::NodeAttribute attribute : std::as_const(attributes))
            items.append(QOpcUaReadItem(nodeId, attribute));
    }

    m_reading.append({nodeIds, QSet<QString>(nodeIds.cbegin(), nodeIds.cend()), kind});
    if (!m_client->readNodeAttributes(items)) {
        m_reading.removeLast();
        failRead(nodeIds, kind == ReadKind::Values
                              ? QStringLiteral("Value: read request could not be started")
                              : QStringLiteral("Read: read request could not be started"));
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
 * so servers with a small MaxNodesPerRead still work. After a metadata batch,
 * each of its variables is routed to the Value read or marked as a structure.
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
        if (serviceResult == QOpcUa::UaStatusCode::BadTooManyOperations
            && batch.nodeIds.size() > 1) {
            const int half = std::max<int>(1, int(batch.nodeIds.size() / 2));
            if (batch.kind == ReadKind::Values) {
                m_options.valuesPerRead = half;
                m_valueQueue = batch.nodeIds + m_valueQueue;
            } else {
                m_options.variablesPerRead = half;
                m_readQueue = batch.nodeIds + m_readQueue;
            }
        } else {
            failRead(batch.nodeIds,
                     QStringLiteral("%1: %2").arg(batch.kind == ReadKind::Values
                                                      ? QStringLiteral("Value")
                                                      : QStringLiteral("Read"),
                                                  QOpcUa::statusToString(serviceResult)));
        }
        pump();
        return;
    }

    QSet<QString> resultIds;
    for (const QOpcUaReadResult &result : results)
        resultIds.insert(result.nodeId());
    const QOpcUa::NodeAttribute first = results.first().attribute();
    const ReadKind kind = first == QOpcUa::NodeAttribute::Value       ? ReadKind::Values
                          : first == QOpcUa::NodeAttribute::NodeClass ? ReadKind::RootInfo
                                                                      : ReadKind::Metadata;
    const auto batch = std::find_if(m_reading.begin(), m_reading.end(),
                                    [&resultIds, kind](const ReadBatch &candidate) {
                                        return candidate.kind == kind
                                               && candidate.keySet == resultIds;
                                    });
    if (batch == m_reading.end())
        return; // A read issued by another component.
    const QStringList nodeIds = batch->nodeIds;
    m_reading.erase(batch);
    touch();

    if (kind == ReadKind::RootInfo) {
        applyRootInfo(results);
        pump();
        return;
    }

    for (const QOpcUaReadResult &result : results) {
        const auto it = m_index.constFind(result.nodeId());
        if (it != m_index.constEnd())
            applyReadResult(m_nodes[it.value()], result);
    }
    if (kind == ReadKind::Metadata) {
        for (const QString &nodeId : nodeIds)
            routeValueRead(nodeId);
    }
    pump();
}

/*!
 * \internal
 * \brief Marks the variables \a nodeIds as not readable because of \a reason.
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
    }
}

/*!
 * \internal
 * \brief Queues the Value read of variable \a nodeId once its DataType is known.
 *
 * A variable whose DataType attribute could not be read gets no Value read:
 * without a type it cannot be recreated faithfully, and its metadata error is
 * already recorded. A DataType outside the namespace-0 tables is queued for a
 * lookup; the variable waits until startTypeLookup() has resolved it. With
 * CloneOptions::readValues off no Value is read at all.
 */
void CloneBrowser::routeValueRead(const QString &nodeId)
{
    if (!m_options.readValues)
        return;
    const auto index = m_index.constFind(nodeId);
    if (index == m_index.constEnd())
        return;
    CloneNode &node = m_nodes[index.value()];
    if (node.dataTypeId.isEmpty())
        return;

    const QString typeId = DataTypeIds::normalize(node.dataTypeId);
    if (m_typeLookupGivenUp.contains(typeId)) {
        node.attributeErrors.append(QStringLiteral("Value: not read, DataType lookup timed out"));
        logReadFailure(nodeId, QStringLiteral("Value: not read, DataType lookup timed out"));
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
    queueValueRead(node, cached.value());
}

/*!
 * \internal
 * \brief Queues the Value read of \a node, or skips it when \a type is a structure.
 *
 * An unresolved type is read like any other, so a value is only skipped when
 * the server's type hierarchy says the variable is a structure.
 */
void CloneBrowser::queueValueRead(CloneNode &node, const ResolvedDataType &type)
{
    if (type.builtInBase == kStructureBase)
        node.valueSkipped = true;
    else
        m_valueQueue.append(node.nodeId);
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
                queueValueRead(m_nodes[it.value()], resolved);
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
    }
}

/*!
 * \internal
 * \brief Returns the current breakdown of queued and in-flight work.
 */
CloneProgress CloneBrowser::progressSnapshot() const
{
    CloneProgress snapshot;
    snapshot.captured = int(m_nodes.size());
    snapshot.browseQueued = int(m_browseQueue.size());
    snapshot.browsing = int(m_browsing.size());
    snapshot.metadataQueued = int(m_readQueue.size());
    snapshot.valuesQueued = int(m_valueQueue.size());
    for (const QStringList &nodeIds : m_waitingForType)
        snapshot.waitingForType += int(nodeIds.size());
    for (const ReadBatch &batch : m_reading)
        snapshot.reading += int(batch.nodeIds.size());
    snapshot.readRequests = int(m_reading.size());
    snapshot.typeLookups = m_typeLookup ? int(m_typeLookupIds.size()) : 0;
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
        || !m_readQueue.isEmpty() || !m_valueQueue.isEmpty() || !m_reading.isEmpty()
        || !m_waitingForType.isEmpty() || m_typeLookup)
        return;
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

    // A captured root alone (its browse failed) is no result either.
    const qsizetype minimumNodes = m_options.captureRoot ? 1 : 0;
    if (m_rootBrowseFailed && m_nodes.size() <= minimumNodes
        && state != CloneCompletion::State::Cancelled)
        state = CloneCompletion::State::Failed;

    m_completion.state = state;
    m_completion.nodeCount = int(m_nodes.size());
    m_completion.pendingRequests = pendingCount();
    m_completion.failedReads = int(std::count_if(
        m_nodes.cbegin(), m_nodes.cend(),
        [](const CloneNode &node) { return !node.attributeErrors.isEmpty(); }));
    m_completion.options = m_options;
    m_completion.elapsedMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    m_resultNamespaces = m_client ? m_client->namespaceArray() : QStringList();

    qCInfo(lcOpcUaClone).noquote()
        << "traversal ended: state" << int(state) << "nodes" << m_completion.nodeCount
        << "pending" << m_completion.pendingRequests << "failedBrowses"
        << m_completion.failedBrowses << "failedReads" << m_completion.failedReads
        << "depthLimited" << m_completion.depthLimitedNodes << "elapsedMs"
        << m_completion.elapsedMs;

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
