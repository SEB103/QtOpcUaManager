#include "clonebrowser.h"

#include <QLoggingCategory>
#include <QOpcUaClient>
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
    m_options.leafProbeCount = std::max(0, m_options.leafProbeCount);
    m_options.maxArrayElements = std::max(0, m_options.maxArrayElements);
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
    // values, e.g. released by a DataType lookup, gets a request of its own;
    // leftover values are read when no metadata is queued and no read runs.
    const auto take = [](QStringList &queue, int count) {
        const qsizetype n = std::min<qsizetype>(count, queue.size());
        QStringList batch = queue.mid(0, n);
        queue.remove(0, n);
        return batch;
    };
    while (m_reading.size() < m_options.maxConcurrentReads) {
        QStringList metadata;
        QStringList values;
        if (m_valueQueue.size() >= m_options.valuesPerRead) {
            values = take(m_valueQueue, m_options.valuesPerRead);
        } else if (!m_readQueue.isEmpty()) {
            metadata = take(m_readQueue, m_options.variablesPerRead);
            values = take(m_valueQueue, m_options.variablesPerRead);
        } else if (!m_valueQueue.isEmpty() && m_reading.isEmpty()) {
            values = take(m_valueQueue, m_options.valuesPerRead);
        } else {
            break;
        }
        startRead(metadata, values);
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
    const QString parentId = parentIsRoot ? QString() : item.nodeId;

    // Large arrays (e.g. a 1000-row recipe table of structures) hold most of
    // a PLC's nodes; their elements can be limited.
    bool limitElements = false;
    if (m_options.maxArrayElements > 0 && !parentIsRoot) {
        const auto parent = m_index.constFind(item.nodeId);
        if (parent != m_index.constEnd()) {
            const CloneNode &array = m_nodes.at(parent.value());
            limitElements = array.isVariable
                            && (array.valueRank >= 1 || !array.arrayDimensions.isEmpty());
        }
    }
    int elements = 0;
    int skippedElements = 0;
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
        if (childDepth >= m_options.maxDepth)
            ++m_completion.depthLimitedNodes;
        else if (child.isVariable)
            m_pendingBrowseDepth.insert(childId, childDepth);
        else
            m_browseQueue.enqueue({childId, childDepth});
        if (child.isVariable)
            m_readQueue.append(childId);
    }

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
 * \brief Issues one batched read of the metadata of \a metadataIds and the Value of \a valueIds.
 *
 * Either list may be empty. The request is claimed later by the union of both
 * lists, which is unique among the requests in flight: a variable's Value is
 * only queued after its metadata request has been answered.
 */
void CloneBrowser::startRead(const QStringList &metadataIds, const QStringList &valueIds)
{
    QList<QOpcUaReadItem> items;
    items.reserve(metadataIds.size() * kMetadataAttributes.size() + valueIds.size());
    for (const QString &nodeId : metadataIds) {
        for (const QOpcUa::NodeAttribute attribute : kMetadataAttributes)
            items.append(QOpcUaReadItem(nodeId, attribute));
    }
    for (const QString &nodeId : valueIds)
        items.append(QOpcUaReadItem(nodeId, QOpcUa::NodeAttribute::Value));

    QSet<QString> keySet(metadataIds.cbegin(), metadataIds.cend());
    keySet.unite(QSet<QString>(valueIds.cbegin(), valueIds.cend()));
    m_reading.append({metadataIds, valueIds, keySet, m_elapsed.elapsed()});
    ++m_completion.readRequests;
    if (!m_client->readNodeAttributes(items)) {
        m_reading.removeLast();
        failRead(metadataIds, QStringLiteral("Read: read request could not be started"));
        failRead(valueIds, QStringLiteral("Value: read request could not be started"));
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
        const qsizetype size = batch.metadataIds.size() + batch.valueIds.size();
        if (serviceResult == QOpcUa::UaStatusCode::BadTooManyOperations && size > 1) {
            if (!batch.metadataIds.isEmpty())
                m_options.variablesPerRead = std::max<int>(1, int(batch.metadataIds.size() / 2));
            else
                m_options.valuesPerRead = std::max<int>(1, int(batch.valueIds.size() / 2));
            m_readQueue = batch.metadataIds + m_readQueue;
            m_valueQueue = batch.valueIds + m_valueQueue;
        } else {
            const QString status = QOpcUa::statusToString(serviceResult);
            failRead(batch.metadataIds, QStringLiteral("Read: %1").arg(status));
            failRead(batch.valueIds, QStringLiteral("Value: %1").arg(status));
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
    for (const QString &nodeId : metadataIds)
        routeVariable(nodeId);
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
 * is browsed only while its TypeDefinition is being probed or after a probe
 * found children; otherwise it is counted as not browsed. Every other
 * variable is browsed.
 */
void CloneBrowser::settleVariable(CloneNode &node, const ResolvedDataType &type)
{
    if (type.builtInBase == kStructureBase)
        node.valueSkipped = true;
    else
        m_valueQueue.append(node.nodeId);

    if (!isScalarLeaf(node, type)) {
        browseVariable(node.nodeId);
        return;
    }

    LeafProbe &probe = m_leafProbes[node.typeDefinitionId];
    if (!probe.hasChildren && probe.answered >= m_options.leafProbeCount) {
        if (m_pendingBrowseDepth.remove(node.nodeId))
            ++m_completion.leafVariablesNotBrowsed;
        return;
    }
    if (!probe.hasChildren && probe.issued < m_options.leafProbeCount
        && m_pendingBrowseDepth.contains(node.nodeId)) {
        ++probe.issued;
        m_probeNodes.insert(node.nodeId, node.typeDefinitionId);
    }
    browseVariable(node.nodeId);
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
    } else if (!probe.hasChildren && probe.answered == m_options.leafProbeCount) {
        qCInfo(lcOpcUaClone).noquote()
            << probe.answered << "scalar variables of TypeDefinition" << typeDefinition
            << "had no children; further ones are not browsed";
    }
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
    snapshot.captured = int(m_nodes.size());
    snapshot.browseQueued = int(m_browseQueue.size());
    snapshot.browsing = int(m_browsing.size());
    snapshot.metadataQueued = int(m_readQueue.size());
    snapshot.valuesQueued = int(m_valueQueue.size());
    for (const QStringList &nodeIds : m_waitingForType)
        snapshot.waitingForType += int(nodeIds.size());
    for (const ReadBatch &batch : m_reading)
        snapshot.reading += int(batch.metadataIds.size() + batch.valueIds.size());
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

    if (attribute == QOpcUa::NodeAttribute::Value)
        node.valueStatus = static_cast<quint32>(status);

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
        << m_completion.arrayElementsSkipped << "elapsedMs" << m_completion.elapsedMs;

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

    if (!m_client || typeIds.isEmpty() || state == CloneCompletion::State::Cancelled) {
        publishResult();
        return;
    }

    m_resolver = new DataTypeResolver(m_client, this);
    m_resolver->setTimeoutMs(m_options.typeLookupTimeoutMs);
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
