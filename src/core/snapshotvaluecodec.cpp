#include "snapshotvaluecodec.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QUuid>

#include <QOpcUaExpandedNodeId>
#include <QOpcUaExtensionObject>
#include <QOpcUaLocalizedText>
#include <QOpcUaMultiDimensionalArray>
#include <QOpcUaQualifiedName>
#include <QtOpcUa/qopcuatype.h>

#include <cmath>

/*!
    \namespace SnapshotValueCodec
    \inmodule OpcUaManager
    \brief Encodes OPC UA values read by Qt OPC UA as typed JSON for address-space snapshots.

    Qt OPC UA delivers values as QVariant with a Qt type per OPC UA built-in
    type. The snapshot keeps them in a JSON form that loses nothing silently:

    \list
    \li Boolean is a JSON boolean; SByte to UInt32, Float and Double are numbers.
        NaN and the infinities, which JSON cannot hold, are the strings
        \c NaN, \c Infinity and \c -Infinity.
    \li Int64 and UInt64 are decimal strings, because a JSON number is a double
        and would lose precision above 2^53.
    \li DateTime is ISO-8601 UTC with milliseconds; an invalid DateTime is null.
    \li ByteString is Base64, Guid the UUID text without braces.
    \li LocalizedText is an object with \c locale and \c text, QualifiedName
        one with \c namespaceIndex and \c name, ExpandedNodeId one with
        \c namespaceUri, \c nodeId and \c serverIndex; StatusCode is its name.
    \li ExtensionObject is an object with \c encodingTypeId, \c encoding and
        \c body (Base64); it is not decoded, because servers such as CODESYS
        publish no DataTypeDefinition for their structures.
    \li Arrays are JSON arrays; a multi-dimensional array is an object with
        \c dimensions and \c elements, the elements in row-major order.
    \li Any other Qt type is an object with \c unsupported (the Qt type name)
        and \c text.
    \endlist
*/

namespace {

/*!
 * \internal
 * \brief Returns \a value as a JSON number, or as a string for NaN and the infinities.
 */
QJsonValue encodeFloating(double value)
{
    if (std::isnan(value))
        return QStringLiteral("NaN");
    if (std::isinf(value))
        return value > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    return value;
}

/*!
 * \internal
 * \brief Returns the text of ExtensionObject \a encoding.
 */
QString encodingName(QOpcUaExtensionObject::Encoding encoding)
{
    switch (encoding) {
    case QOpcUaExtensionObject::ByteString: return QStringLiteral("ByteString");
    case QOpcUaExtensionObject::Xml: return QStringLiteral("Xml");
    case QOpcUaExtensionObject::NoBody: break;
    }
    return QStringLiteral("NoBody");
}

/*!
 * \internal
 * \brief Returns the OPC UA built-in type name of the scalar Qt type \a type, or empty.
 */
QString scalarTypeName(const QMetaType &type)
{
    switch (type.id()) {
    case QMetaType::Bool: return QStringLiteral("Boolean");
    case QMetaType::SChar:
    case QMetaType::Char: return QStringLiteral("SByte");
    case QMetaType::UChar: return QStringLiteral("Byte");
    case QMetaType::Short: return QStringLiteral("Int16");
    case QMetaType::UShort: return QStringLiteral("UInt16");
    case QMetaType::Int: return QStringLiteral("Int32");
    case QMetaType::UInt: return QStringLiteral("UInt32");
    case QMetaType::LongLong: return QStringLiteral("Int64");
    case QMetaType::ULongLong: return QStringLiteral("UInt64");
    case QMetaType::Float: return QStringLiteral("Float");
    case QMetaType::Double: return QStringLiteral("Double");
    case QMetaType::QString: return QStringLiteral("String");
    case QMetaType::QDateTime: return QStringLiteral("DateTime");
    case QMetaType::QByteArray: return QStringLiteral("ByteString");
    case QMetaType::QUuid: return QStringLiteral("Guid");
    default: break;
    }
    if (type == QMetaType::fromType<QOpcUaLocalizedText>())
        return QStringLiteral("LocalizedText");
    if (type == QMetaType::fromType<QOpcUaQualifiedName>())
        return QStringLiteral("QualifiedName");
    if (type == QMetaType::fromType<QOpcUaExpandedNodeId>())
        return QStringLiteral("ExpandedNodeId");
    if (type == QMetaType::fromType<QOpcUaExtensionObject>())
        return QStringLiteral("ExtensionObject");
    if (type == QMetaType::fromType<QOpcUa::UaStatusCode>())
        return QStringLiteral("StatusCode");
    return {};
}

/*!
 * \internal
 * \brief Returns the JSON form of the scalar \a value.
 */
QJsonValue encodeScalar(const QVariant &value)
{
    const QMetaType type = value.metaType();
    switch (type.id()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::SChar:
    case QMetaType::Char:
    case QMetaType::Short:
    case QMetaType::Int: return value.toInt();
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt: return double(value.toUInt());
    case QMetaType::LongLong: return QString::number(value.toLongLong());
    case QMetaType::ULongLong: return QString::number(value.toULongLong());
    case QMetaType::Float:
    case QMetaType::Double: return encodeFloating(value.toDouble());
    case QMetaType::QString: return value.toString();
    case QMetaType::QDateTime: {
        const QDateTime dateTime = value.toDateTime();
        if (!dateTime.isValid())
            return QJsonValue::Null;
        return dateTime.toUTC().toString(Qt::ISODateWithMs);
    }
    case QMetaType::QByteArray: return QString::fromLatin1(value.toByteArray().toBase64());
    case QMetaType::QUuid: return value.toUuid().toString(QUuid::WithoutBraces);
    default: break;
    }

    if (type == QMetaType::fromType<QOpcUaLocalizedText>()) {
        const auto text = value.value<QOpcUaLocalizedText>();
        return QJsonObject{{QStringLiteral("locale"), text.locale()},
                           {QStringLiteral("text"), text.text()}};
    }
    if (type == QMetaType::fromType<QOpcUaQualifiedName>()) {
        const auto name = value.value<QOpcUaQualifiedName>();
        return QJsonObject{{QStringLiteral("namespaceIndex"), int(name.namespaceIndex())},
                           {QStringLiteral("name"), name.name()}};
    }
    if (type == QMetaType::fromType<QOpcUaExpandedNodeId>()) {
        const auto id = value.value<QOpcUaExpandedNodeId>();
        return QJsonObject{{QStringLiteral("namespaceUri"), id.namespaceUri()},
                           {QStringLiteral("nodeId"), id.nodeId()},
                           {QStringLiteral("serverIndex"), double(id.serverIndex())}};
    }
    if (type == QMetaType::fromType<QOpcUaExtensionObject>()) {
        const auto object = value.value<QOpcUaExtensionObject>();
        return QJsonObject{
            {QStringLiteral("encodingTypeId"), object.encodingTypeId()},
            {QStringLiteral("encoding"), encodingName(object.encoding())},
            {QStringLiteral("body"), QString::fromLatin1(object.encodedBody().toBase64())}};
    }
    if (type == QMetaType::fromType<QOpcUa::UaStatusCode>())
        return QOpcUa::statusToString(value.value<QOpcUa::UaStatusCode>());

    return QJsonObject{{QStringLiteral("unsupported"), QString::fromLatin1(type.name())},
                       {QStringLiteral("text"), value.toString()}};
}

} // namespace

/*!
 * \brief Returns the JSON form of \a value; an invalid value yields null.
 *
 * Lists become JSON arrays and QOpcUaMultiDimensionalArray becomes an object
 * with \c dimensions and \c elements; their elements use the scalar encoding.
 */
QJsonValue SnapshotValueCodec::encode(const QVariant &value)
{
    if (!value.isValid())
        return QJsonValue::Null;

    if (value.metaType() == QMetaType::fromType<QOpcUaMultiDimensionalArray>()) {
        const auto array = value.value<QOpcUaMultiDimensionalArray>();
        QJsonArray dimensions;
        for (const quint32 dimension : array.arrayDimensions())
            dimensions.append(double(dimension));
        QJsonArray elements;
        for (const QVariant &element : array.valueArray())
            elements.append(encode(element));
        return QJsonObject{{QStringLiteral("dimensions"), dimensions},
                           {QStringLiteral("elements"), elements}};
    }

    if (value.typeId() == QMetaType::QVariantList) {
        QJsonArray elements;
        for (const QVariant &element : value.toList())
            elements.append(encode(element));
        return elements;
    }

    return encodeScalar(value);
}

/*!
 * \brief Returns the JSON form of \a value for a variable with \a valueRank.
 *
 * Qt OPC UA delivers a one-element array as a scalar. For a ValueRank of 1 or
 * more such a scalar is wrapped into a one-element array again, so the
 * snapshot shows the variable's real shape. Other ranks keep the value as read.
 */
QJsonValue SnapshotValueCodec::encodeForValueRank(const QVariant &value, int valueRank)
{
    const bool isArray = value.typeId() == QMetaType::QVariantList
                         || value.metaType() == QMetaType::fromType<QOpcUaMultiDimensionalArray>();
    if (valueRank >= 1 && value.isValid() && !isArray)
        return QJsonArray{encode(value)};
    return encode(value);
}

/*!
 * \brief Returns the OPC UA built-in type name of \a value as received.
 *
 * For a list or a multi-dimensional array the type of its first element is
 * returned; an empty array, an invalid value and a Qt type without an OPC UA
 * counterpart yield an empty string.
 */
QString SnapshotValueCodec::variantTypeName(const QVariant &value)
{
    if (!value.isValid())
        return {};
    if (value.metaType() == QMetaType::fromType<QOpcUaMultiDimensionalArray>()) {
        const QVariantList elements = value.value<QOpcUaMultiDimensionalArray>().valueArray();
        return elements.isEmpty() ? QString() : variantTypeName(elements.first());
    }
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList elements = value.toList();
        return elements.isEmpty() ? QString() : variantTypeName(elements.first());
    }
    return scalarTypeName(value.metaType());
}
