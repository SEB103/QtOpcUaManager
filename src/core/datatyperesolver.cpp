#include "datatyperesolver.h"

#include <algorithm>

#include <QOpcUaBrowseRequest>
#include <QOpcUaClient>
#include <QOpcUaNode>
#include <QOpcUaQualifiedName>
#include <QOpcUaReferenceDescription>
#include <QTimer>

/*!
 * \class DataTypeResolver
 * \inmodule OpcUaManager
 * \brief Resolves server DataType ids to their OPC UA base types.
 *
 * The client tree and the Data View derive a variable's displayed type from
 * the runtime value, so they look right even for vendor DataType aliases. A
 * clone or snapshot works from the DataType attribute instead, which for
 * CODESYS is often a vendor alias (for example the IEC STRING type in the
 * CODESYSSPV3/3S/IecVarAccess namespace) or a user structure. This resolver
 * follows the inverse HasSubtype references of such types until a
 * namespace-0 base type is reached, so the original id, its name and its base
 * type can be kept apart.
 */

namespace {

/*!
 * \internal
 * \brief Maximum number of supertype steps followed for one type.
 */
constexpr int kMaxHierarchyDepth = 32;

/*!
 * \internal
 * \brief Maximum number of DataType lookups in flight at the same time.
 */
constexpr int kMaxInFlight = 8;

/*!
 * \internal
 * \brief Safety timeout after which pending lookups are reported as failed (milliseconds).
 */
constexpr int kTimeoutMs = 15000;

/*!
 * \internal
 * \brief Namespace URI of namespace 0, accepted in "nsu=" node ids.
 */
const QString kNs0Uri = QStringLiteral("http://opcfoundation.org/UA/");

/*!
 * \internal
 * \brief Returns the numeric identifier of namespace-0 id \a typeId, or -1.
 *
 * \a typeId must be normalized ("ns=0;i=<n>").
 */
int ns0Numeric(const QString &typeId)
{
    if (!typeId.startsWith(QLatin1String("ns=0;i=")))
        return -1;
    bool ok = false;
    const int value = typeId.mid(7).toInt(&ok);
    return ok ? value : -1;
}

/*!
 * \internal
 * \brief Name and base type of one well-known namespace-0 subtype.
 */
struct KnownSubtype
{
    QString name; //!< BrowseName of the subtype.
    QString base; //!< Name of its base type.
};

/*!
 * \internal
 * \brief Returns the table of well-known namespace-0 subtypes keyed by numeric id.
 *
 * These standard subtypes appear on every server (for example Duration in
 * ServerCapabilities, LocaleId in LocaleIdArray, UtcTime in ServerStatus).
 * Resolving them locally saves a browse per clone.
 */
const QHash<int, KnownSubtype> &knownSubtypes()
{
    static const QHash<int, KnownSubtype> table = {
        {30, {QStringLiteral("Image"), QStringLiteral("ByteString")}},
        {288, {QStringLiteral("IntegerId"), QStringLiteral("UInt32")}},
        {289, {QStringLiteral("Counter"), QStringLiteral("UInt32")}},
        {290, {QStringLiteral("Duration"), QStringLiteral("Double")}},
        {291, {QStringLiteral("NumericRange"), QStringLiteral("String")}},
        {292, {QStringLiteral("Time"), QStringLiteral("String")}},
        {293, {QStringLiteral("Date"), QStringLiteral("DateTime")}},
        {294, {QStringLiteral("UtcTime"), QStringLiteral("DateTime")}},
        {295, {QStringLiteral("LocaleId"), QStringLiteral("String")}},
        {311, {QStringLiteral("ApplicationInstanceCertificate"), QStringLiteral("ByteString")}},
        {388, {QStringLiteral("SessionAuthenticationToken"), QStringLiteral("NodeId")}},
        {521, {QStringLiteral("ContinuationPoint"), QStringLiteral("ByteString")}},
        {2000, {QStringLiteral("ImageBMP"), QStringLiteral("ByteString")}},
        {2001, {QStringLiteral("ImageGIF"), QStringLiteral("ByteString")}},
        {2002, {QStringLiteral("ImageJPG"), QStringLiteral("ByteString")}},
        {2003, {QStringLiteral("ImagePNG"), QStringLiteral("ByteString")}},
        {12877, {QStringLiteral("NormalizedString"), QStringLiteral("String")}},
        {12878, {QStringLiteral("DecimalString"), QStringLiteral("String")}},
        {12879, {QStringLiteral("DurationString"), QStringLiteral("String")}},
        {12880, {QStringLiteral("TimeString"), QStringLiteral("String")}},
        {12881, {QStringLiteral("DateString"), QStringLiteral("String")}},
        {16307, {QStringLiteral("AudioDataType"), QStringLiteral("ByteString")}},
    };
    return table;
}

} // namespace

/*!
 * \namespace DataTypeIds
 * \inmodule OpcUaManager
 * \brief Server-independent helpers for OPC UA DataType node ids.
 *
 * Normalizes node id notations and resolves namespace-0 base types and
 * well-known subtypes without a server round trip.
 */

/*!
 * \brief Returns \a nodeId in the "ns=<n>;<t>=<id>" form used by Qt OPC UA.
 *
 * Accepts the short namespace-0 form ("i=12"), the explicit form
 * ("ns=0;i=12") and the namespace-0 URI form
 * ("nsu=http://opcfoundation.org/UA/;i=12"). Other "nsu=" ids are returned
 * unchanged because their index is server-specific.
 */
QString DataTypeIds::normalize(const QString &nodeId)
{
    const QString trimmed = nodeId.trimmed();
    if (trimmed.isEmpty())
        return {};
    if (trimmed.startsWith(QLatin1String("ns=")))
        return trimmed;
    if (trimmed.startsWith(QLatin1String("nsu="))) {
        const qsizetype separator = trimmed.indexOf(QLatin1Char(';'));
        if (separator > 4 && trimmed.mid(4, separator - 4) == kNs0Uri)
            return QStringLiteral("ns=0;") + trimmed.mid(separator + 1);
        return trimmed;
    }
    // "i=", "s=", "g=", "b=" without a namespace refer to namespace 0.
    if (trimmed.size() > 2 && trimmed.at(1) == QLatin1Char('='))
        return QStringLiteral("ns=0;") + trimmed;
    return trimmed;
}

/*!
 * \brief Returns the name of the namespace-0 base type \a typeId, or an empty string.
 *
 * Covers the built-in types of OPC UA Part 6 (Boolean .. DiagnosticInfo) and
 * the abstract bases Number, Integer, UInteger and Enumeration. \a typeId may
 * be in any form accepted by normalize().
 */
QString DataTypeIds::baseTypeName(const QString &typeId)
{
    static const QHash<int, QString> names = {
        {1, QStringLiteral("Boolean")},        {2, QStringLiteral("SByte")},
        {3, QStringLiteral("Byte")},           {4, QStringLiteral("Int16")},
        {5, QStringLiteral("UInt16")},         {6, QStringLiteral("Int32")},
        {7, QStringLiteral("UInt32")},         {8, QStringLiteral("Int64")},
        {9, QStringLiteral("UInt64")},         {10, QStringLiteral("Float")},
        {11, QStringLiteral("Double")},        {12, QStringLiteral("String")},
        {13, QStringLiteral("DateTime")},      {14, QStringLiteral("Guid")},
        {15, QStringLiteral("ByteString")},    {16, QStringLiteral("XmlElement")},
        {17, QStringLiteral("NodeId")},        {18, QStringLiteral("ExpandedNodeId")},
        {19, QStringLiteral("StatusCode")},    {20, QStringLiteral("QualifiedName")},
        {21, QStringLiteral("LocalizedText")}, {22, QStringLiteral("Structure")},
        {23, QStringLiteral("DataValue")},     {24, QStringLiteral("BaseDataType")},
        {25, QStringLiteral("DiagnosticInfo")},{26, QStringLiteral("Number")},
        {27, QStringLiteral("Integer")},       {28, QStringLiteral("UInteger")},
        {29, QStringLiteral("Enumeration")},
    };
    return names.value(ns0Numeric(normalize(typeId)));
}

/*!
 * \brief Returns the base type of the well-known namespace-0 subtype \a typeId.
 *
 * Returns an empty string for ids not in the table; their base is then found
 * through the server hierarchy.
 */
QString DataTypeIds::knownSubtypeBase(const QString &typeId)
{
    return knownSubtypes().value(ns0Numeric(normalize(typeId))).base;
}

/*!
 * \brief Resolves \a typeId from the namespace-0 tables only.
 *
 * Returns Resolution::BuiltIn for a base type, Resolution::KnownSubtype for a
 * well-known subtype and Resolution::None for every other id (vendor types,
 * user types, less common namespace-0 subtypes).
 */
ResolvedDataType DataTypeIds::resolveLocally(const QString &typeId)
{
    ResolvedDataType result;
    result.typeId = normalize(typeId);

    const QString base = baseTypeName(result.typeId);
    if (!base.isEmpty()) {
        result.typeName = base;
        result.builtInBase = base;
        result.resolution = ResolvedDataType::Resolution::BuiltIn;
        return result;
    }

    const KnownSubtype subtype = knownSubtypes().value(ns0Numeric(result.typeId));
    if (!subtype.base.isEmpty()) {
        result.typeName = subtype.name;
        result.builtInBase = subtype.base;
        result.resolution = ResolvedDataType::Resolution::KnownSubtype;
    }
    return result;
}

/*!
 * \brief Returns the type name embedded in the CODESYS DataType id \a typeId.
 *
 * CODESYS string DataType ids contain the marker "|type|" followed by the
 * dotted path of the type ("...Application.AGX.ST_ValueList"), sometimes with
 * a "#" before the name. The last segment is the user type name. Returns an
 * empty string for every other id.
 */
QString DataTypeIds::nameFromTypeId(const QString &typeId)
{
    if (!typeId.contains(QLatin1String("|type|"), Qt::CaseInsensitive))
        return {};
    const qsizetype hash = typeId.lastIndexOf(QLatin1Char('#'));
    const qsizetype dot = typeId.lastIndexOf(QLatin1Char('.'));
    if (hash != -1 && hash > dot)
        return typeId.mid(hash + 1);
    if (dot != -1)
        return typeId.mid(dot + 1);
    return {};
}

/*!
 * \class DataTypeHierarchy
 * \inmodule OpcUaManager
 * \brief Supertype relations of DataType nodes collected from a server.
 */

/*!
 * \brief Records that \a typeId (BrowseName \a typeName) is a direct subtype of \a superTypeId.
 * \param typeId DataType node id of the subtype.
 * \param superTypeId DataType node id of its direct supertype.
 * \param typeName BrowseName of the subtype; may be empty.
 */
void DataTypeHierarchy::addType(const QString &typeId, const QString &superTypeId,
                                const QString &typeName)
{
    m_entries.insert(DataTypeIds::normalize(typeId),
                     {DataTypeIds::normalize(superTypeId), typeName, QString()});
}

/*!
 * \brief Records that the supertype of \a typeId could not be determined.
 * \param typeId DataType node id that failed.
 * \param typeName BrowseName of the type, if it was read; may be empty.
 * \param reason Human-readable failure reason (status code or timeout).
 */
void DataTypeHierarchy::addFailure(const QString &typeId, const QString &typeName,
                                   const QString &reason)
{
    m_entries.insert(DataTypeIds::normalize(typeId), {QString(), typeName, reason});
}

/*!
 * \brief Returns whether \a typeId has an entry.
 * \param typeId DataType node id in any accepted form.
 */
bool DataTypeHierarchy::contains(const QString &typeId) const
{
    return m_entries.contains(DataTypeIds::normalize(typeId));
}

/*!
 * \brief Resolves \a typeId to its OPC UA base type.
 * \param typeId DataType node id in any accepted form.
 *
 * Walks the recorded supertype chain, starting with \a typeId itself, until
 * an id is covered by DataTypeIds::resolveLocally(). The original id and its
 * BrowseName are kept in the result; \c superTypeIds lists the visited
 * supertypes. The walk stops with Resolution::Unresolved when an id has no
 * entry, a recorded failure, no supertype, forms a cycle, or exceeds
 * kMaxHierarchyDepth steps.
 */
ResolvedDataType DataTypeHierarchy::resolve(const QString &typeId) const
{
    const QString start = DataTypeIds::normalize(typeId);
    ResolvedDataType result;
    result.typeId = start;

    if (start.isEmpty()) {
        result.resolution = ResolvedDataType::Resolution::Unresolved;
        result.reason = QStringLiteral("no DataType id");
        return result;
    }

    const auto startEntry = m_entries.constFind(start);
    if (startEntry != m_entries.constEnd())
        result.typeName = startEntry->typeName;

    QSet<QString> visited;
    QString current = start;
    for (int depth = 0; depth <= kMaxHierarchyDepth; ++depth) {
        const ResolvedDataType local = DataTypeIds::resolveLocally(current);
        if (local.resolution != ResolvedDataType::Resolution::None) {
            result.builtInBase = local.builtInBase;
            if (current == start) {
                result.resolution = local.resolution;
                if (result.typeName.isEmpty())
                    result.typeName = local.typeName;
            } else {
                result.resolution = ResolvedDataType::Resolution::Hierarchy;
            }
            return result;
        }

        visited.insert(current);
        const auto entry = m_entries.constFind(current);
        if (entry == m_entries.constEnd()) {
            result.resolution = ResolvedDataType::Resolution::Unresolved;
            result.reason = QStringLiteral("supertype of %1 not available").arg(current);
            return result;
        }
        if (!entry->failure.isEmpty()) {
            result.resolution = ResolvedDataType::Resolution::Unresolved;
            result.reason = entry->failure;
            return result;
        }
        if (entry->superTypeId.isEmpty()) {
            result.resolution = ResolvedDataType::Resolution::Unresolved;
            result.reason = QStringLiteral("%1 has no supertype").arg(current);
            return result;
        }
        if (visited.contains(entry->superTypeId)) {
            result.resolution = ResolvedDataType::Resolution::Unresolved;
            result.reason = QStringLiteral("cyclic HasSubtype hierarchy at %1").arg(current);
            return result;
        }
        result.superTypeIds.append(entry->superTypeId);
        current = entry->superTypeId;
    }

    result.resolution = ResolvedDataType::Resolution::Unresolved;
    result.reason = QStringLiteral("type hierarchy deeper than %1 levels").arg(kMaxHierarchyDepth);
    return result;
}

/*!
 * \brief Constructs the resolver for \a client.
 * \param client Connected client used for the lookups; must outlive the resolver.
 * \param parent Owning QObject.
 */
DataTypeResolver::DataTypeResolver(QOpcUaClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_timeoutMs(kTimeoutMs)
{
}

/*!
 * \brief Releases the transient protocol nodes of unfinished lookups.
 */
DataTypeResolver::~DataTypeResolver()
{
    for (const Lookup &lookup : std::as_const(m_inFlight)) {
        if (lookup.node)
            lookup.node->deleteLater();
    }
}

/*!
 * \brief Starts resolving \a typeIds.
 *
 * Ids covered by the namespace-0 tables need no request. Every other id is
 * looked up on the server, followed by its supertypes. finished() is emitted
 * once all lookups settled or the safety timeout elapsed; it is emitted
 * asynchronously even when no request is needed.
 */
void DataTypeResolver::resolve(const QStringList &typeIds)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        // Report every unanswered lookup as failed so resolve() still settles.
        m_timedOut = true;
        const QStringList pending = m_inFlight.keys() + m_queue;
        for (const QString &typeId : pending)
            m_hierarchy.addFailure(typeId, m_inFlight.value(typeId).typeName,
                                   QStringLiteral("DataType lookup timed out"));
        finish();
    });
    m_timeout->start(m_timeoutMs);

    for (const QString &typeId : typeIds)
        enqueue(DataTypeIds::normalize(typeId));

    startNext();
    // Defer the idle check so finished() never fires inside resolve().
    QTimer::singleShot(0, this, &DataTypeResolver::finishIfIdle);
}

/*!
 * \brief Sets the safety timeout of resolve() to \a timeoutMs milliseconds.
 * \param timeoutMs Duration after which unanswered lookups are reported as failed;
 *        values below 1 are raised to 1. The default is kTimeoutMs.
 *
 * A lookup that runs while the server is busy with other requests, such as
 * during a clone traversal, needs a longer timeout than one on an idle server.
 */
void DataTypeResolver::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = std::max(1, timeoutMs);
}

/*!
 * \brief Returns the resolution of \a typeId with the data collected so far.
 */
ResolvedDataType DataTypeResolver::result(const QString &typeId) const
{
    return m_hierarchy.resolve(typeId);
}

/*!
 * \internal
 * \brief Queues \a typeId for a server lookup unless it is known or already queued.
 */
void DataTypeResolver::enqueue(const QString &typeId)
{
    if (typeId.isEmpty() || m_seen.contains(typeId))
        return;
    m_seen.insert(typeId);
    if (DataTypeIds::resolveLocally(typeId).resolution != ResolvedDataType::Resolution::None)
        return;
    m_queue.append(typeId);
}

/*!
 * \internal
 * \brief Starts queued lookups while fewer than kMaxInFlight are running.
 */
void DataTypeResolver::startNext()
{
    while (!m_finished && !m_queue.isEmpty() && m_inFlight.size() < kMaxInFlight)
        startLookup(m_queue.takeFirst());
}

/*!
 * \internal
 * \brief Reads the BrowseName of DataType \a typeId and browses its supertype.
 *
 * Both requests are issued on one transient QOpcUaNode; completeLookup() runs
 * once both have settled, whatever their status.
 */
void DataTypeResolver::startLookup(const QString &typeId)
{
    QOpcUaNode *node = m_client ? m_client->node(typeId) : nullptr;
    if (!node) {
        m_hierarchy.addFailure(typeId, QString(),
                               QStringLiteral("no OPC UA node for %1").arg(typeId));
        return;
    }
    node->setParent(this);
    m_inFlight.insert(typeId, Lookup{node});

    connect(node, &QOpcUaNode::attributeRead, this,
            [this, typeId, node](QOpcUa::NodeAttributes attributes) {
                auto it = m_inFlight.find(typeId);
                if (it == m_inFlight.end() || it->nameDone
                    || !attributes.testFlag(QOpcUa::NodeAttribute::BrowseName))
                    return;
                it->nameDone = true;
                if (node->attributeError(QOpcUa::NodeAttribute::BrowseName)
                    == QOpcUa::UaStatusCode::Good) {
                    it->typeName = node->attribute(QOpcUa::NodeAttribute::BrowseName)
                                       .value<QOpcUaQualifiedName>()
                                       .name();
                }
                if (it->browseDone)
                    completeLookup(typeId);
            });

    connect(node, &QOpcUaNode::browseFinished, this,
            [this, typeId](const QList<QOpcUaReferenceDescription> &references,
                           QOpcUa::UaStatusCode status) {
                auto it = m_inFlight.find(typeId);
                if (it == m_inFlight.end() || it->browseDone)
                    return;
                it->browseDone = true;
                if (status != QOpcUa::UaStatusCode::Good) {
                    it->failure = QStringLiteral("browse of %1 failed: %2")
                                      .arg(typeId, QOpcUa::statusToString(status));
                } else if (references.isEmpty()) {
                    it->failure = QStringLiteral("%1 has no supertype").arg(typeId);
                } else {
                    it->superTypeId = references.first().targetNodeId().nodeId();
                }
                if (it->nameDone)
                    completeLookup(typeId);
            },
            Qt::SingleShotConnection);

    if (!node->readAttributes(QOpcUa::NodeAttribute::BrowseName))
        m_inFlight[typeId].nameDone = true;

    QOpcUaBrowseRequest request;
    request.setBrowseDirection(QOpcUaBrowseRequest::BrowseDirection::Inverse);
    request.setReferenceTypeId(QOpcUa::ReferenceTypeId::HasSubtype);
    request.setIncludeSubtypes(false);
    request.setNodeClassMask(QOpcUa::NodeClass::DataType);
    if (!node->browse(request)) {
        Lookup &lookup = m_inFlight[typeId];
        lookup.browseDone = true;
        lookup.failure = QStringLiteral("browse of %1 could not be started").arg(typeId);
    }

    const Lookup &lookup = m_inFlight[typeId];
    if (lookup.nameDone && lookup.browseDone)
        completeLookup(typeId);
}

/*!
 * \internal
 * \brief Records the outcome of the lookup of \a typeId and continues with its supertype.
 */
void DataTypeResolver::completeLookup(const QString &typeId)
{
    const Lookup lookup = m_inFlight.take(typeId);
    if (lookup.node)
        lookup.node->deleteLater();
    if (m_finished)
        return;

    if (lookup.failure.isEmpty()) {
        m_hierarchy.addType(typeId, lookup.superTypeId, lookup.typeName);
        enqueue(DataTypeIds::normalize(lookup.superTypeId));
    } else {
        m_hierarchy.addFailure(typeId, lookup.typeName, lookup.failure);
    }

    startNext();
    finishIfIdle();
}

/*!
 * \internal
 * \brief Emits finished() when no lookup is running or queued.
 */
void DataTypeResolver::finishIfIdle()
{
    if (m_inFlight.isEmpty() && m_queue.isEmpty())
        finish();
}

/*!
 * \internal
 * \brief Stops the timeout and emits finished() exactly once.
 */
void DataTypeResolver::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_timeout)
        m_timeout->stop();
    emit finished();
}
