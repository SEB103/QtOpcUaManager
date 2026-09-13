#include "serverprojectnodeset.h"

#include <QFile>
#include <QHash>
#include <QVariantList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace ServerProject {

namespace {

/*! The uax types namespace used for scalar/array Value contents. */
const QString kUaxNs = QStringLiteral("http://opcfoundation.org/UA/2008/02/Types.xsd");
/*! The UANodeSet document namespace. */
const QString kNodeSetNs = QStringLiteral("http://opcfoundation.org/UA/2011/03/UANodeSet.xsd");

/*!
 * \internal
 * \brief Maps a built-in data type name to its OPC UA namespace-0 numeric id.
 */
int builtinToNs0Id(const QString &name)
{
    static const QHash<QString, int> map = {
        {QStringLiteral("Boolean"), 1}, {QStringLiteral("SByte"), 2},
        {QStringLiteral("Byte"), 3},    {QStringLiteral("Int16"), 4},
        {QStringLiteral("UInt16"), 5},  {QStringLiteral("Int32"), 6},
        {QStringLiteral("UInt32"), 7},  {QStringLiteral("Int64"), 8},
        {QStringLiteral("UInt64"), 9},  {QStringLiteral("Float"), 10},
        {QStringLiteral("Double"), 11}, {QStringLiteral("String"), 12},
    };
    return map.value(name, -1);
}

/*!
 * \internal
 * \brief Maps an OPC UA namespace-0 numeric id back to a built-in type name.
 */
QString ns0IdToBuiltin(int id)
{
    static const QHash<int, QString> map = {
        {1, QStringLiteral("Boolean")}, {2, QStringLiteral("SByte")},
        {3, QStringLiteral("Byte")},    {4, QStringLiteral("Int16")},
        {5, QStringLiteral("UInt16")},  {6, QStringLiteral("Int32")},
        {7, QStringLiteral("UInt32")},  {8, QStringLiteral("Int64")},
        {9, QStringLiteral("UInt64")},  {10, QStringLiteral("Float")},
        {11, QStringLiteral("Double")}, {12, QStringLiteral("String")},
    };
    return map.value(id);
}

/*! Returns the "<ns>:<name>" browse-name string for a node id. */
QString browseNameString(const QString &nodeId, const QString &name)
{
    return QStringLiteral("%1:%2").arg(parseNodeId(nodeId).ns).arg(name);
}

/*! Returns the local part of a "<ns>:<name>" browse-name string. */
QString browseNameLocal(const QString &browseName)
{
    const int colon = browseName.indexOf(QLatin1Char(':'));
    return colon >= 0 ? browseName.mid(colon + 1) : browseName;
}

/*! Returns the OPC UA type name used for a variable's Value contents. */
QString uaxTypeName(const Node &node)
{
    if (!node.enumTypeId.isEmpty())
        return QStringLiteral("Int32");
    return node.dataType.isEmpty() ? QStringLiteral("Int32") : node.dataType;
}

/*! Formats a scalar QVariant for a uax value element. */
QString scalarToText(const QString &typeName, const QVariant &value)
{
    if (typeName == QLatin1String("Boolean"))
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.toString();
}

/*! Parses a uax value text into a QVariant of the named type. */
QVariant textToScalar(const QString &typeName, const QString &text)
{
    if (typeName == QLatin1String("Boolean"))
        return text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
               || text == QLatin1String("1");
    if (typeName == QLatin1String("Float") || typeName == QLatin1String("Double"))
        return text.toDouble();
    if (typeName == QLatin1String("String"))
        return text;
    if (typeName.startsWith(QLatin1String("UInt")) || typeName == QLatin1String("Byte"))
        return text.toULongLong();
    return text.toLongLong();
}

/*! Writes one Reference element. */
void writeReference(QXmlStreamWriter &xml, const QString &type, bool isForward,
                    const QString &target)
{
    xml.writeStartElement(QStringLiteral("Reference"));
    xml.writeAttribute(QStringLiteral("ReferenceType"), type);
    if (!isForward)
        xml.writeAttribute(QStringLiteral("IsForward"), QStringLiteral("false"));
    xml.writeCharacters(target);
    xml.writeEndElement();
}

/*! Writes the Value element of a variable node. */
void writeValue(QXmlStreamWriter &xml, const Node &node)
{
    const QString typeName = uaxTypeName(node);
    if (node.valueRank == 1) {
        xml.writeStartElement(QStringLiteral("Value"));
        xml.writeStartElement(kUaxNs, QStringLiteral("ListOf%1").arg(typeName));
        for (const QVariant &item : node.initialValue.toList())
            xml.writeTextElement(kUaxNs, typeName, scalarToText(typeName, item));
        xml.writeEndElement();
        xml.writeEndElement();
    } else if (node.initialValue.isValid()) {
        xml.writeStartElement(QStringLiteral("Value"));
        xml.writeTextElement(kUaxNs, typeName, scalarToText(typeName, node.initialValue));
        xml.writeEndElement();
    }
}

/*! Writes one folder/object/variable node. */
void writeNode(QXmlStreamWriter &xml, const Node &node)
{
    const bool isVariable = node.kind == NodeKind::Variable;
    xml.writeStartElement(isVariable ? QStringLiteral("UAVariable") : QStringLiteral("UAObject"));
    xml.writeAttribute(QStringLiteral("NodeId"), node.nodeId);
    xml.writeAttribute(QStringLiteral("BrowseName"), browseNameString(node.nodeId, node.browseName));

    if (isVariable) {
        QString dataTypeRef;
        if (!node.enumTypeId.isEmpty())
            dataTypeRef = node.enumTypeId;
        else
            dataTypeRef = QStringLiteral("i=%1").arg(builtinToNs0Id(node.dataType));
        xml.writeAttribute(QStringLiteral("DataType"), dataTypeRef);
        if (node.valueRank == 1)
            xml.writeAttribute(QStringLiteral("ValueRank"), QStringLiteral("1"));
    }

    xml.writeTextElement(QStringLiteral("DisplayName"),
                         node.displayName.isEmpty() ? node.browseName : node.displayName);
    if (!node.description.isEmpty())
        xml.writeTextElement(QStringLiteral("Description"), node.description);

    xml.writeStartElement(QStringLiteral("References"));
    const QString typeDefinition = isVariable
        ? QStringLiteral("i=63")
        : (node.kind == NodeKind::Folder ? QStringLiteral("i=61") : QStringLiteral("i=58"));
    writeReference(xml, QStringLiteral("HasTypeDefinition"), true, typeDefinition);
    const QString parent = node.parentNodeId.isEmpty() ? QStringLiteral("i=85") : node.parentNodeId;
    const QString hierarchical =
        node.kind == NodeKind::Folder ? QStringLiteral("Organizes") : QStringLiteral("HasComponent");
    writeReference(xml, hierarchical, false, parent);
    xml.writeEndElement(); // References

    if (isVariable)
        writeValue(xml, node);

    xml.writeEndElement();
}

/*! Writes one enumeration DataType node with its Definition fields. */
void writeEnumType(QXmlStreamWriter &xml, const EnumType &enumType)
{
    xml.writeStartElement(QStringLiteral("UADataType"));
    xml.writeAttribute(QStringLiteral("NodeId"), enumType.nodeId);
    xml.writeAttribute(QStringLiteral("BrowseName"),
                       browseNameString(enumType.nodeId, enumType.name));
    xml.writeTextElement(QStringLiteral("DisplayName"), enumType.name);

    xml.writeStartElement(QStringLiteral("References"));
    // Enumeration (i=29) is the supertype; correct for interoperability.
    writeReference(xml, QStringLiteral("HasSubtype"), false, QStringLiteral("i=29"));
    xml.writeEndElement();

    xml.writeStartElement(QStringLiteral("Definition"));
    xml.writeAttribute(QStringLiteral("Name"), enumType.name);
    for (const EnumEntry &entry : enumType.entries) {
        xml.writeStartElement(QStringLiteral("Field"));
        xml.writeAttribute(QStringLiteral("Name"), entry.name);
        xml.writeAttribute(QStringLiteral("Value"), QString::number(entry.value));
        xml.writeEndElement();
    }
    xml.writeEndElement(); // Definition
    xml.writeEndElement(); // UADataType
}

// --- Import helpers --------------------------------------------------------

/*! Parses a Value element into a QVariant, setting \a valueRank. */
QVariant readValue(QXmlStreamReader &xml, int &valueRank)
{
    QVariant result;
    while (xml.readNextStartElement()) {
        const QString element = xml.name().toString();
        if (element.startsWith(QLatin1String("ListOf"))) {
            const QString typeName = element.mid(6);
            QVariantList list;
            while (xml.readNextStartElement())
                list.append(textToScalar(typeName, xml.readElementText()));
            valueRank = 1;
            result = list;
        } else {
            valueRank = -1;
            result = textToScalar(element, xml.readElementText());
        }
    }
    return result;
}

} // namespace

/*!
 * \brief Writes \a data's address space to \a filePath as NodeSet2 XML.
 */
NodeSet::Result NodeSet::exportToFile(const QString &filePath, const ProjectData &data)
{
    Result result;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorString = QStringLiteral("Cannot open file for writing: %1")
                                 .arg(file.errorString());
        return result;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("UANodeSet"));
    xml.writeDefaultNamespace(kNodeSetNs);
    xml.writeNamespace(kUaxNs, QStringLiteral("uax"));

    if (!data.namespaces.isEmpty()) {
        xml.writeStartElement(QStringLiteral("NamespaceUris"));
        for (const Namespace &ns : data.namespaces)
            xml.writeTextElement(QStringLiteral("Uri"), ns.uri);
        xml.writeEndElement();
    }

    for (const EnumType &enumType : data.enumTypes)
        writeEnumType(xml, enumType);
    for (const Node &node : data.nodes)
        writeNode(xml, node);

    xml.writeEndElement(); // UANodeSet
    xml.writeEndDocument();

    if (xml.hasError() || file.error() != QFile::NoError) {
        result.errorString = QStringLiteral("Failed to write the NodeSet2 document.");
        return result;
    }
    result.ok = true;
    return result;
}

/*!
 * \brief Parses the NodeSet2 XML at \a filePath into an address space.
 */
NodeSet::ImportResult NodeSet::importFromFile(const QString &filePath)
{
    ImportResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorString = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return result;
    }

    // First pass: collect <Aliases> so that DataType and reference values given
    // as alias names (for example DataType="Int32") resolve to real node ids,
    // regardless of where the Aliases block appears in the document.
    QHash<QString, QString> aliases;
    {
        QXmlStreamReader aliasXml(&file);
        while (!aliasXml.atEnd()) {
            if (aliasXml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (aliasXml.name() == QLatin1String("Aliases")) {
                while (aliasXml.readNextStartElement()) {
                    if (aliasXml.name() == QLatin1String("Alias")) {
                        const QString alias =
                            aliasXml.attributes().value(QStringLiteral("Alias")).toString();
                        const QString id = aliasXml.readElementText().trimmed();
                        if (!alias.isEmpty())
                            aliases.insert(alias, id);
                    } else {
                        aliasXml.skipCurrentElement();
                    }
                }
            }
        }
    }
    file.seek(0);

    // Resolves an id reference: a raw node id passes through, an alias name is
    // looked up (falling back to the literal when unknown).
    const auto resolveId = [&aliases](const QString &ref) -> QString {
        if (ref.isEmpty() || ref.startsWith(QLatin1String("i="))
            || ref.startsWith(QLatin1String("ns=")) || ref.startsWith(QLatin1String("g="))
            || ref.startsWith(QLatin1String("b="))) {
            return ref;
        }
        return aliases.value(ref, ref);
    };

    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;

        const QString element = xml.name().toString();
        if (element == QLatin1String("Aliases")) {
            // Already collected in the first pass.
            xml.skipCurrentElement();
        } else if (element == QLatin1String("NamespaceUris")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("Uri"))
                    result.namespaces.append({xml.readElementText()});
                else
                    xml.skipCurrentElement();
            }
        } else if (element == QLatin1String("UADataType")) {
            EnumType enumType;
            enumType.nodeId = xml.attributes().value(QStringLiteral("NodeId")).toString();
            enumType.name =
                browseNameLocal(xml.attributes().value(QStringLiteral("BrowseName")).toString());
            bool isEnum = false;
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("DisplayName")) {
                    enumType.name = xml.readElementText();
                } else if (xml.name() == QLatin1String("Definition")) {
                    isEnum = true;
                    while (xml.readNextStartElement()) {
                        if (xml.name() == QLatin1String("Field")) {
                            EnumEntry entry;
                            entry.name =
                                xml.attributes().value(QStringLiteral("Name")).toString();
                            entry.value =
                                xml.attributes().value(QStringLiteral("Value")).toInt();
                            enumType.entries.append(entry);
                        }
                        xml.skipCurrentElement();
                    }
                } else {
                    xml.skipCurrentElement();
                }
            }
            if (isEnum && !enumType.nodeId.isEmpty()) {
                result.enumTypes.append(enumType);
            } else {
                // A non-enumeration data type cannot be represented by the model.
                result.skippedKinds[QStringLiteral("UADataType")] += 1;
                result.skippedCount += 1;
            }
        } else if (element == QLatin1String("UAObject")
                   || element == QLatin1String("UAVariable")) {
            const bool isVariable = element == QLatin1String("UAVariable");
            Node node;
            node.kind = isVariable ? NodeKind::Variable : NodeKind::Object;
            node.nodeId = xml.attributes().value(QStringLiteral("NodeId")).toString();
            node.browseName =
                browseNameLocal(xml.attributes().value(QStringLiteral("BrowseName")).toString());
            node.displayName = node.browseName;

            QString dataTypeRef;
            QString typeDefinition;
            QString hierarchicalRef;
            if (isVariable) {
                dataTypeRef =
                    resolveId(xml.attributes().value(QStringLiteral("DataType")).toString());
                if (xml.attributes().value(QStringLiteral("ValueRank")).toInt() == 1)
                    node.valueRank = 1;
            }

            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("DisplayName")) {
                    node.displayName = xml.readElementText();
                } else if (xml.name() == QLatin1String("Description")) {
                    node.description = xml.readElementText();
                } else if (xml.name() == QLatin1String("References")) {
                    while (xml.readNextStartElement()) {
                        if (xml.name() == QLatin1String("Reference")) {
                            const QString refType =
                                xml.attributes().value(QStringLiteral("ReferenceType")).toString();
                            const bool isForward =
                                xml.attributes().value(QStringLiteral("IsForward")).toString()
                                != QLatin1String("false");
                            const QString target = resolveId(xml.readElementText());
                            if (refType == QLatin1String("HasTypeDefinition") && isForward)
                                typeDefinition = target;
                            else if (!isForward
                                     && (refType == QLatin1String("Organizes")
                                         || refType == QLatin1String("HasComponent"))) {
                                node.parentNodeId = target;
                                hierarchicalRef = refType;
                            }
                        } else {
                            xml.skipCurrentElement();
                        }
                    }
                } else if (xml.name() == QLatin1String("Value")) {
                    int rank = -1;
                    node.initialValue = readValue(xml, rank);
                    if (rank == 1)
                        node.valueRank = 1;
                } else {
                    xml.skipCurrentElement();
                }
            }

            // A parent of the Objects folder maps to the top level.
            if (node.parentNodeId == QLatin1String("i=85"))
                node.parentNodeId.clear();

            if (!isVariable) {
                node.kind = (typeDefinition == QLatin1String("i=61")
                             || hierarchicalRef == QLatin1String("Organizes"))
                                ? NodeKind::Folder
                                : NodeKind::Object;
            } else {
                if (dataTypeRef.startsWith(QLatin1String("ns="))) {
                    node.enumTypeId = dataTypeRef;
                    node.dataType = QStringLiteral("Int32");
                } else if (dataTypeRef.startsWith(QLatin1String("i="))) {
                    node.dataType = ns0IdToBuiltin(dataTypeRef.mid(2).toInt());
                } else {
                    node.dataType = dataTypeRef;
                }
                if (node.dataType.isEmpty() && node.enumTypeId.isEmpty())
                    node.dataType = QStringLiteral("Int32");
            }

            if (!node.nodeId.isEmpty())
                result.nodes.append(node);
        } else if (element == QLatin1String("UAObjectType")
                   || element == QLatin1String("UAVariableType")
                   || element == QLatin1String("UAMethod")
                   || element == QLatin1String("UAReferenceType")
                   || element == QLatin1String("UAView")) {
            // Node classes the Server Studio model cannot represent yet: count
            // them so the import can report exactly what it dropped.
            result.skippedKinds[element] += 1;
            result.skippedCount += 1;
            xml.skipCurrentElement();
        }
    }

    if (xml.hasError()) {
        result.errorString = QStringLiteral("Invalid NodeSet2 XML: %1").arg(xml.errorString());
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace ServerProject
