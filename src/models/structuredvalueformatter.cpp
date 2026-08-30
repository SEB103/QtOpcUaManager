#include "structuredvalueformatter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QXmlStreamWriter>

/*!
 * \class StructuredValueFormatter
 * \brief Serializes a decoded OPC UA value tree into JSON or XML text.
 */

namespace {

/*!
 * \internal
 * \brief Returns the JSON object describing \a node as { type, value[, valueRank] }.
 *
 * Scalars use the typed QVariant so numbers and booleans keep their JSON type.
 * Structures map each field name to its described child; arrays list described
 * children in index order.
 */
QJsonObject describeNode(const OpcUaValueTreeNode &node)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), node.typeName);

    switch (node.kind) {
    case OpcUaValueTreeNode::Kind::Struct: {
        QJsonObject fields;
        for (const OpcUaValueTreeNode &child : node.children)
            fields.insert(child.name, describeNode(child));
        object.insert(QStringLiteral("value"), fields);
        break;
    }
    case OpcUaValueTreeNode::Kind::Array: {
        QJsonArray elements;
        for (const OpcUaValueTreeNode &child : node.children)
            elements.append(describeNode(child));
        object.insert(QStringLiteral("valueRank"), node.valueRank);
        object.insert(QStringLiteral("value"), elements);
        break;
    }
    case OpcUaValueTreeNode::Kind::Scalar:
        object.insert(QStringLiteral("value"), QJsonValue::fromVariant(node.scalarValue));
        break;
    }

    return object;
}

/*!
 * \internal
 * \brief Writes \a node into \a writer as a Struct, Array, or Value element.
 *
 * A structure field passes its name so the element gains a \c name attribute;
 * array elements and the root pass an empty name.
 */
void writeNode(QXmlStreamWriter &writer, const OpcUaValueTreeNode &node)
{
    switch (node.kind) {
    case OpcUaValueTreeNode::Kind::Struct:
        writer.writeStartElement(QStringLiteral("Struct"));
        if (!node.name.isEmpty())
            writer.writeAttribute(QStringLiteral("name"), node.name);
        writer.writeAttribute(QStringLiteral("type"), node.typeName);
        for (const OpcUaValueTreeNode &child : node.children)
            writeNode(writer, child);
        writer.writeEndElement();
        break;
    case OpcUaValueTreeNode::Kind::Array:
        writer.writeStartElement(QStringLiteral("Array"));
        if (!node.name.isEmpty())
            writer.writeAttribute(QStringLiteral("name"), node.name);
        writer.writeAttribute(QStringLiteral("type"), node.typeName);
        writer.writeAttribute(QStringLiteral("valueRank"), QString::number(node.valueRank));
        for (const OpcUaValueTreeNode &child : node.children)
            writeNode(writer, child);
        writer.writeEndElement();
        break;
    case OpcUaValueTreeNode::Kind::Scalar:
        writer.writeStartElement(QStringLiteral("Value"));
        if (!node.name.isEmpty())
            writer.writeAttribute(QStringLiteral("name"), node.name);
        writer.writeAttribute(QStringLiteral("type"), node.typeName);
        writer.writeCharacters(node.scalarText);
        writer.writeEndElement();
        break;
    }
}

} // namespace

/*!
 * \brief Returns \a root serialized in \a format.
 */
QString StructuredValueFormatter::format(const OpcUaValueTreeNode &root, Format format)
{
    switch (format) {
    case Format::Xml:
        return toXml(root);
    case Format::Json:
        break;
    }
    return toJson(root);
}

/*!
 * \brief Returns \a root serialized as indented JSON text.
 */
QString StructuredValueFormatter::toJson(const OpcUaValueTreeNode &root)
{
    const QJsonDocument document(describeNode(root));
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

/*!
 * \brief Returns \a root serialized as indented XML text.
 */
QString StructuredValueFormatter::toXml(const OpcUaValueTreeNode &root)
{
    QString output;
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writeNode(writer, root);
    writer.writeEndDocument();
    return output;
}
