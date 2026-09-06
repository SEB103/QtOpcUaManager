#include "attributesmodel.h"

#include <QCoreApplication>
#include <qopcuatype.h>

#include "core/opcuaaccesslevel.h"

namespace {

/*!
 * \internal
 * \brief Placeholder shown for an attribute the server did not provide.
 *
 * An attribute that is simply left out reads as "this node has no such
 * information", which is different from an attribute whose value is empty. The
 * panel used to drop absent attributes silently, so a missing AccessLevel was
 * indistinguishable from one that was never asked for.
 */
const QString &notProvided()
{
    static const QString text = QStringLiteral("—");
    return text;
}

/*!
 * \internal
 * \brief Returns \a text, or the not-provided placeholder when it is empty.
 */
QString orPlaceholder(const QString &text)
{
    return text.isEmpty() ? notProvided() : text;
}

/*!
 * \internal
 * \brief Returns a readable description of the OPC UA ValueRank \a valueRank.
 *
 * The special negative ranks of OPC UA part 3 carry meaning that the bare
 * number does not convey.
 */
QString valueRankToString(int valueRank)
{
    switch (valueRank) {
    case -3:
        return QCoreApplication::translate("AttributesModel",
                                           "Scalar or one dimension (-3)");
    case -2:
        return QCoreApplication::translate("AttributesModel", "Any (-2)");
    case -1:
        return QCoreApplication::translate("AttributesModel", "Scalar (-1)");
    case 0:
        return QCoreApplication::translate("AttributesModel",
                                           "One or more dimensions (0)");
    default:
        break;
    }

    if (valueRank < -3)
        return notProvided();

    return QCoreApplication::translate("AttributesModel", "%n dimension(s)", nullptr, valueRank);
}

} // namespace

/*!
 * \brief Constructs an empty attributes model.
 */
AttributesModel::AttributesModel(QObject *parent)
    : QAbstractListModel(parent)
{}

/*!
 * \brief Replaces all rows with the attributes described by \a data.
 *
 * Identity attributes are always listed. The value-related group is listed only
 * for Variable nodes, where it is meaningful; within a listed group an attribute
 * the server did not provide is shown with a placeholder rather than omitted, so
 * the panel distinguishes "absent" from "empty".
 */
void AttributesModel::setAttributes(const OpcUaAttributeData &data)
{
    QList<Entry> entries;
    const auto append = [&entries](const QString &name, const QString &value) {
        entries.push_back(Entry{name, value});
    };

    append(QStringLiteral("NodeId"), orPlaceholder(data.nodeId));
    append(QStringLiteral("NodeClass"), orPlaceholder(data.nodeClassName));
    append(QStringLiteral("BrowseName"), orPlaceholder(data.browseName));
    append(QStringLiteral("DisplayName"), orPlaceholder(data.displayName));
    append(QStringLiteral("Description"), orPlaceholder(data.description));

    if (data.nodeClass == int(QOpcUa::NodeClass::Variable)) {
        append(QStringLiteral("Value"), orPlaceholder(data.value));
        append(QStringLiteral("DataType"), orPlaceholder(data.dataType));
        append(QStringLiteral("ValueRank"), valueRankToString(data.valueRank));
        append(QStringLiteral("ArrayDimensions"), orPlaceholder(data.arrayDimensions));

        // AccessLevel decides whether the value editor is offered at all, so it
        // is spelled out rather than shown as a bare bit mask.
        append(QStringLiteral("AccessLevel"),
               orPlaceholder(OpcUaAccessLevel::toString(data.accessLevel)));
        append(QStringLiteral("UserAccessLevel"),
               orPlaceholder(OpcUaAccessLevel::toString(data.userAccessLevel)));

        append(QStringLiteral("Historizing"),
               data.historizing < 0
                   ? notProvided()
                   : (data.historizing != 0 ? QStringLiteral("true") : QStringLiteral("false")));
        append(QStringLiteral("MinimumSamplingInterval"),
               data.minimumSamplingInterval < 0.0
                   ? notProvided()
                   : tr("%1 ms").arg(data.minimumSamplingInterval));

        append(QStringLiteral("SourceTimestamp"), orPlaceholder(data.sourceTimestamp));
        append(QStringLiteral("ServerTimestamp"), orPlaceholder(data.serverTimestamp));
        append(QStringLiteral("StatusCode"), orPlaceholder(data.statusCode));
    }

    append(QStringLiteral("WriteMask"),
           data.writeMask < 0 ? notProvided() : QString::number(data.writeMask));

    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

/*!
 * \brief Removes all rows.
 */
void AttributesModel::clear()
{
    if (m_entries.isEmpty())
        return;

    beginResetModel();
    m_entries.clear();
    endResetModel();
}

/*!
 * \brief Returns the value shown for \a attribute, or an empty string.
 */
QString AttributesModel::valueFor(const QString &attribute) const
{
    for (const Entry &entry : m_entries) {
        if (entry.name == attribute)
            return entry.value;
    }
    return {};
}

/*!
 * \brief Returns model data for \a index and \a role.
 */
QVariant AttributesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case AttributeRole: return entry.name;
    case ValueRole: return entry.value;
    default: return {};
    }
}

/*!
 * \brief Returns the number of rows below \a parent.
 */
int AttributesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return int(m_entries.size());
}

/*!
 * \brief Returns the role names exposed to QML.
 */
QHash<int, QByteArray> AttributesModel::roleNames() const
{
    return {
        {AttributeRole, "attribute"},
        {ValueRole, "value"}
    };
}
