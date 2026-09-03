#include <QHash>
#include <QMetaEnum>

#include "treeitem.h"
#include "opcuamodel.h"

namespace {

/*!
 * \internal
 * \brief Coarse data-type category used to choose a variable icon.
 */
enum class DataTypeCategory {
    Unknown,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Real,
    String,
    DateTime,
    ByteString,
    Structured
};

/*!
 * \internal
 * \brief IEC-style type name plus coarse category for a DataType node id.
 */
struct DataTypeInfo
{
    QString name;
    DataTypeCategory category {DataTypeCategory::Unknown};
};

/*!
 * \internal
 * \brief Resolves an OPC UA DataType node id into an IEC name and category.
 *
 * Namespace-0 ids follow the OPC UA Part 6 built-in type numbering. The
 * CODESYS vendor namespace additionally exposes IEC 61131 aliases
 * (BYTE/UINT/UDINT/TIME/STRING/DATE_AND_TIME); the vendor namespace index is
 * assumed to be 3, matching the reference server. Any other resolvable,
 * non built-in type id is reported as a structured/custom type, which is
 * enough to select a struct icon. An empty or unresolved id yields Unknown.
 */
DataTypeInfo resolveDataType(const QString &dataTypeId)
{
    static const QHash<QString, DataTypeInfo> table = {
        {QStringLiteral("ns=0;i=1"),  {QStringLiteral("BOOL"),          DataTypeCategory::Boolean}},
        {QStringLiteral("ns=0;i=2"),  {QStringLiteral("SINT"),          DataTypeCategory::SignedInteger}},
        {QStringLiteral("ns=0;i=3"),  {QStringLiteral("BYTE"),          DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=0;i=4"),  {QStringLiteral("INT"),           DataTypeCategory::SignedInteger}},
        {QStringLiteral("ns=0;i=5"),  {QStringLiteral("UINT"),          DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=0;i=6"),  {QStringLiteral("DINT"),          DataTypeCategory::SignedInteger}},
        {QStringLiteral("ns=0;i=7"),  {QStringLiteral("UDINT"),         DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=0;i=8"),  {QStringLiteral("LINT"),          DataTypeCategory::SignedInteger}},
        {QStringLiteral("ns=0;i=9"),  {QStringLiteral("ULINT"),         DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=0;i=10"), {QStringLiteral("REAL"),          DataTypeCategory::Real}},
        {QStringLiteral("ns=0;i=11"), {QStringLiteral("LREAL"),         DataTypeCategory::Real}},
        {QStringLiteral("ns=0;i=12"), {QStringLiteral("STRING"),        DataTypeCategory::String}},
        {QStringLiteral("ns=0;i=13"), {QStringLiteral("DATE_AND_TIME"), DataTypeCategory::DateTime}},
        {QStringLiteral("ns=0;i=15"), {QStringLiteral("BYTES"),         DataTypeCategory::ByteString}},
        {QStringLiteral("ns=0;i=21"), {QStringLiteral("STRING"),        DataTypeCategory::String}},
        {QStringLiteral("ns=0;i=22"), {QStringLiteral("STRUCT"),        DataTypeCategory::Structured}},
        {QStringLiteral("ns=3;i=3001"), {QStringLiteral("BYTE"),          DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=3;i=3002"), {QStringLiteral("UINT"),          DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=3;i=3003"), {QStringLiteral("UDINT"),         DataTypeCategory::UnsignedInteger}},
        {QStringLiteral("ns=3;i=3005"), {QStringLiteral("TIME"),          DataTypeCategory::DateTime}},
        {QStringLiteral("ns=3;i=3007"), {QStringLiteral("DATE_AND_TIME"), DataTypeCategory::DateTime}},
        {QStringLiteral("ns=3;i=3013"), {QStringLiteral("STRING"),        DataTypeCategory::String}},
    };

    const auto it = table.constFind(dataTypeId);
    if (it != table.constEnd())
        return it.value();

    if (dataTypeId.isEmpty())
        return {};

    // Any other resolvable, non built-in type is treated as structured/custom.
    if (!dataTypeId.startsWith(QStringLiteral("ns=0;")))
        return {QString(), DataTypeCategory::Structured};

    return {};
}

} // namespace

/*!
 * \brief Creates the invisible root item.
 * The root item is a pure GUI-thread container. It represents the logical
 * RootFolder browse point and is never exposed directly to QML.
 */
TreeItem::TreeItem(OpcUaModel *model)
    : TreeItem(model, QStringLiteral("ns=0;i=84"), QStringLiteral("RootFolder"))
{
}

/*!
 * \brief Creates the invisible root item anchored at \a nodeId.
 * \param nodeId The OPC UA node id the model browses from.
 * \param displayName The display name used for the root browse point.
 * The root item is a pure GUI-thread container. It represents the logical browse
 * point (the server RootFolder by default, or a focus node for a re-rooted model)
 * and is never exposed directly to QML.
 */
TreeItem::TreeItem(OpcUaModel *model, const QString &nodeId, const QString &displayName)
    : m_model(model)
    , m_nodeId(nodeId)
    , m_browseName(displayName)
    , m_displayName(displayName)
    , m_hasChildren(true)
{
}

/*!
 * \brief Creates a visible child item from snapshot data.
 * The item copies immutable browse metadata received from OpcUaService.
 * It does not allocate QOpcUaNode or any other live backend object.
 */
TreeItem::TreeItem(const OpcUaNodeData &data, OpcUaModel *model, TreeItem *parent)
    : m_model(model)
    , m_parentItem(parent)
    , m_nodeId(data.nodeId)
    , m_browseName(data.browseName)
    , m_displayName(data.displayName)
    , m_nodeClass(data.nodeClass)
    , m_typeDefinitionId(data.typeDefinitionId)
    , m_dataTypeId(data.dataTypeId)
    , m_valueRank(data.valueRank)
    , m_hasChildren(data.hasChildren)
{
}

/*!
 * \brief Destroys the item and all children.
 */
TreeItem::~TreeItem() = default;

/*!
 * \brief Returns the child at \a row.
 */
TreeItem *TreeItem::child(int row) const
{
    if (row < 0 || row >= int(m_children.size()))
        return nullptr;

    return m_children.at(size_t(row)).get();
}

/*!
 * \brief Returns the row index of \a child.
 */
int TreeItem::childIndex(const TreeItem *child) const
{
    if (!child)
        return -1;

    for (int i = 0, size = int(m_children.size()); i < size; ++i) {
        if (m_children.at(size_t(i)).get() == child)
            return i;
    }

    return -1;
}

/*!
 * \brief Returns the number of children.
 */
int TreeItem::childCount() const
{
    return int(m_children.size());
}

/*!
 * \brief Returns the row index in the parent.
 */
int TreeItem::row() const
{
    if (!m_parentItem)
        return 0;

    return m_parentItem->childIndex(this);
}

/*!
 * \brief Returns the column display data.
 * \param column The column index (0-based).
 * The GUI model keeps the same basic browser layout as before:
 * column 0 = name, column 1 = value, column 2 = type/class, column 3 = node id.
 * Value and type strings stay empty in this snapshot-only version until a
 * dedicated value adapter is added.
 */
QVariant TreeItem::columnData(int column) const
{
    switch (column) {
    case 0:
        return !m_displayName.isEmpty() ? m_displayName : m_browseName;
    case 1:
        return m_valueString;
    case 2:
        return nodeClassName();
    case 3:
        return m_nodeId;
    default:
        return {};
    }
}

/*!
 * \brief Returns the node class name.
 */
QString TreeItem::nodeClassName() const
{
    const QMetaEnum metaEnum = QMetaEnum::fromType<QOpcUa::NodeClass>();
    const char *name = metaEnum.valueToKey(m_nodeClass);
    return name ? QString::fromLatin1(name) : QStringLiteral("Undefined");
}

/*!
 * \brief Returns whether the node is an Object of the standard FolderType.
 * FolderType is the well-known node ns=0;i=61. Objects of any other type
 * definition are reported as structured objects instead of folders.
 */
bool TreeItem::isFolder() const
{
    return QOpcUa::NodeClass(m_nodeClass) == QOpcUa::NodeClass::Object
        && m_typeDefinitionId == QStringLiteral("ns=0;i=61");
}

/*!
 * \brief Returns whether the node is a variable whose value is an array.
 * ValueRank follows OPC UA Part 3: -1 is scalar, while 0 (one-or-more
 * dimensions) or any positive rank denotes an array.
 */
bool TreeItem::isArray() const
{
    return QOpcUa::NodeClass(m_nodeClass) == QOpcUa::NodeClass::Variable
        && m_valueRank >= 0;
}

/*!
 * \brief Returns the IEC-style data-type name for a variable node.
 */
QString TreeItem::dataTypeName() const
{
    if (QOpcUa::NodeClass(m_nodeClass) != QOpcUa::NodeClass::Variable)
        return {};
    return resolveDataType(m_dataTypeId).name;
}

/*!
 * \brief Returns the icon key used by the QML tree.
 * The key encodes the node kind (folder, object, method, type nodes) and, for
 * variables, the value shape (array) or the resolved data-type category. The
 * QML delegate maps the key to a glyph and a tint color.
 */
QString TreeItem::iconName() const
{
    switch (QOpcUa::NodeClass(m_nodeClass)) {
    case QOpcUa::NodeClass::Object:
        return isFolder() ? QStringLiteral("folder") : QStringLiteral("object");
    case QOpcUa::NodeClass::Method:
        return QStringLiteral("method");
    case QOpcUa::NodeClass::ObjectType:
        return QStringLiteral("objectType");
    case QOpcUa::NodeClass::VariableType:
        return QStringLiteral("variableType");
    case QOpcUa::NodeClass::DataType:
        return QStringLiteral("dataType");
    case QOpcUa::NodeClass::ReferenceType:
        return QStringLiteral("referenceType");
    case QOpcUa::NodeClass::View:
        return QStringLiteral("view");
    case QOpcUa::NodeClass::Variable:
        break;
    default:
        return QStringLiteral("node");
    }

    // Variable nodes: an array marker takes precedence, otherwise the icon is
    // chosen from the resolved data-type category.
    if (isArray())
        return QStringLiteral("array");

    switch (resolveDataType(m_dataTypeId).category) {
    case DataTypeCategory::Boolean:         return QStringLiteral("var-bool");
    case DataTypeCategory::SignedInteger:   return QStringLiteral("var-int");
    case DataTypeCategory::UnsignedInteger: return QStringLiteral("var-uint");
    case DataTypeCategory::Real:            return QStringLiteral("var-real");
    case DataTypeCategory::String:          return QStringLiteral("var-string");
    case DataTypeCategory::DateTime:        return QStringLiteral("var-time");
    case DataTypeCategory::Structured:      return QStringLiteral("var-struct");
    case DataTypeCategory::ByteString:
    case DataTypeCategory::Unknown:
        break;
    }
    return QStringLiteral("var-generic");
}

/*!
 * \brief Returns whether the node supports monitoring.
 * In the snapshot-based model monitoring is currently only advertised for
 * variable nodes. Real subscription logic remains service-owned and can be
 * wired later through explicit service commands.
 */
bool TreeItem::supportsMonitoring() const
{
    return QOpcUa::NodeClass(m_nodeClass) == QOpcUa::NodeClass::Variable;
}

/*!
 * \brief Returns whether the node can request more children.
 */
bool TreeItem::canFetchMore() const
{
    return m_hasChildren
        && (m_fetchState == FetchState::NotFetched || m_fetchState == FetchState::Error);
}

/*!
 * \brief Marks the node as currently fetching.
 */
void TreeItem::markFetching()
{
    m_fetchState = FetchState::Fetching;
}

/*!
 * \brief Applies a new fetch state.
 * \param state The new fetch state.
 */
void TreeItem::setFetchState(FetchState state)
{
    m_fetchState = state;
}

/*!
 * \brief Replaces all current children with \a children.
 * Ownership stays entirely inside the GUI-thread model. The service only
 * provides the raw snapshot list from which these child items were built.
 */
void TreeItem::replaceChildren(std::vector<std::unique_ptr<TreeItem>> children)
{
    m_children = std::move(children);

    if (!m_hasChildren)
        m_fetchState = FetchState::Fetched;
    else if (m_children.empty())
        m_fetchState = FetchState::Fetched;
}
