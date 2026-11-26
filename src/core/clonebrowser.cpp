#include "clonebrowser.h"

#include <QLoggingCategory>
#include <QOpcUaClient>
#include <QOpcUaNode>
#include <QOpcUaQualifiedName>
#include <QOpcUaReadItem>
#include <QTimer>

#include <algorithm>

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
 * read of Value, DataType, ValueRank, ArrayDimensions, AccessLevel and
 * UserAccessLevel; each attribute keeps its status, so a failed read stays
 * visible instead of turning into a default value.
 *
 * A node reached through more than one hierarchical reference is captured and
 * browsed once; the additional parents are recorded. Reaching the node or depth
 * limit marks the result Truncated, the overall or idle timeout marks it
 * TimedOut, and cancel() marks it Cancelled, so a partial result is never
 * reported as complete.
 */

namespace {

/*!
 * \internal
 * \brief Node id of the standard FolderType, used to tag folders.
 */
const QString kFolderTypeId = QStringLiteral("ns=0;i=61");

/*!
 * \internal
 * \brief Attributes read for every variable, in request order.
 */
const QList<QOpcUa::NodeAttribute> kVariableAttributes = {
    QOpcUa::NodeAttribute::Value,           QOpcUa::NodeAttribute::DataType,
    QOpcUa::NodeAttribute::ValueRank,       QOpcUa::NodeAttribute::ArrayDimensions,
    QOpcUa::NodeAttribute::AccessLevel,     QOpcUa::NodeAttribute::UserAccessLevel,
};

/*!
 * \internal
 * \brief Interval of the progress reports in milliseconds.
 */
constexpr int kProgressIntervalMs = 500;

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

    m_overallTimer = new QTimer(this);
    m_overallTimer->setSingleShot(true);
    connect(m_overallTimer, &QTimer::timeout, this,
            [this]() { finish(CloneCompletion::State::TimedOut); });
    m_overallTimer->start(m_options.overallTimeoutMs);

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this,
            [this]() { finish(CloneCompletion::State::TimedOut); });
    m_idleTimer->start(m_options.idleTimeoutMs);

    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        emit progress(m_requestId, int(m_nodes.size()), pendingCount());
    });
    m_progressTimer->start(kProgressIntervalMs);

    // The read result signal is shared by every batch read of this client;
    // handleReadResults() claims only the results of this browser's requests.
    m_readConnection = connect(m_client, &QOpcUaClient::readNodeAttributesFinished, this,
                               &CloneBrowser::handleReadResults);

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

    while (m_reading.size() < m_options.maxConcurrentReads && !m_readQueue.isEmpty()) {
        const qsizetype count = std::min<qsizetype>(m_options.variablesPerRead, m_readQueue.size());
        const QStringList batch = m_readQueue.mid(0, count);
        m_readQueue.remove(0, count);
        startRead(batch);
    }

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
    const QString parentId = parentIsRoot ? QString() : item.nodeId;
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
 * \brief Issues one batched attribute read for the variables \a nodeIds.
 */
void CloneBrowser::startRead(const QStringList &nodeIds)
{
    QList<QOpcUaReadItem> items;
    items.reserve(nodeIds.size() * kVariableAttributes.size());
    for (const QString &nodeId : nodeIds) {
        for (const QOpcUa::NodeAttribute attribute : kVariableAttributes)
            items.append(QOpcUaReadItem(nodeId, attribute));
    }

    m_reading.append({nodeIds, QSet<QString>(nodeIds.cbegin(), nodeIds.cend())});
    if (!m_client->readNodeAttributes(items)) {
        m_reading.removeLast();
        failRead(nodeIds, QStringLiteral("read request could not be started"));
    }
}

/*!
 * \internal
 * \brief Applies a batched read result that belongs to one of this browser's requests.
 *
 * The signal is shared with other users of the client, so a result is claimed
 * only when its node-id set matches a request in flight. A service-level
 * failure carries no results; it is attributed to the oldest request in
 * flight. BadTooManyOperations splits that request and lowers the batch size,
 * so servers with a small MaxNodesPerRead still work.
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
            m_options.variablesPerRead = std::max<int>(1, int(batch.nodeIds.size() / 2));
            m_readQueue = batch.nodeIds + m_readQueue;
        } else {
            failRead(batch.nodeIds, QOpcUa::statusToString(serviceResult));
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
    m_reading.erase(batch);
    touch();

    for (const QOpcUaReadResult &result : results) {
        const auto it = m_index.constFind(result.nodeId());
        if (it != m_index.constEnd())
            applyReadResult(m_nodes[it.value()], result);
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
        node.attributeErrors.append(QStringLiteral("Read: %1").arg(reason));
    }
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

    if (attribute == QOpcUa::NodeAttribute::Value)
        node.valueStatus = static_cast<quint32>(status);

    if (isBad(status)) {
        if (attribute == QOpcUa::NodeAttribute::ArrayDimensions)
            return;
        node.attributeErrors.append(
            QStringLiteral("%1: %2").arg(attributeName(attribute), QOpcUa::statusToString(status)));
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
    qsizetype reading = 0;
    for (const ReadBatch &batch : m_reading)
        reading += batch.nodeIds.size();
    return int(m_browseQueue.size() + m_browsing.size() + m_readQueue.size() + reading);
}

/*!
 * \internal
 * \brief Ends the traversal once nothing is queued or in flight.
 */
void CloneBrowser::finishIfIdle()
{
    if (m_finished || !m_browseQueue.isEmpty() || !m_browsing.isEmpty()
        || !m_readQueue.isEmpty() || !m_reading.isEmpty())
        return;
    const bool truncated = m_completion.nodeLimitReached || m_completion.depthLimitedNodes > 0;
    finish(truncated ? CloneCompletion::State::Truncated : CloneCompletion::State::Complete);
}

/*!
 * \internal
 * \brief Ends the traversal with \a state and resolves the captured DataType ids.
 *
 * Responses arriving afterwards are ignored. A root that could not be browsed
 * with nothing captured turns the result into Failed. A cancelled traversal is
 * published without type resolution.
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

    if (m_rootBrowseFailed && m_nodes.isEmpty() && state != CloneCompletion::State::Cancelled)
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

    qCDebug(lcOpcUaClone).noquote()
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
        typeIds.append(node.dataTypeId);
    }

    if (!m_client || typeIds.isEmpty() || state == CloneCompletion::State::Cancelled) {
        publishResult();
        return;
    }

    m_resolver = new DataTypeResolver(m_client, this);
    connect(m_resolver, &DataTypeResolver::finished, this, &CloneBrowser::publishResult);
    m_resolver->resolve(typeIds);
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
            ResolvedDataType resolved = m_resolver ? m_resolver->result(node.dataTypeId)
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
