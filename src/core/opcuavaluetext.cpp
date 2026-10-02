#include "opcuavaluetext.h"

namespace {

/*!
 * \internal
 * \brief Returns the kind of the scalar value \a value.
 *
 * Mirrors QJsonValue::fromVariant(), which the structured Value panel uses: a
 * boolean becomes a JSON literal, a numeric type a JSON number, and any other
 * value its string form, which is Empty when the string is empty.
 */
OpcUaValueKind scalarKind(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return OpcUaValueKind::Empty;

    switch (value.typeId()) {
    case QMetaType::Bool:
        return OpcUaValueKind::Boolean;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Float:
    case QMetaType::Double:
        return OpcUaValueKind::Number;
    default:
        break;
    }

    return value.toString().isEmpty() ? OpcUaValueKind::Empty : OpcUaValueKind::Text;
}

/*!
 * \internal
 * \brief Returns whether \a value is an array value rather than a scalar.
 *
 * A string can convert to a list, so it is excluded explicitly.
 */
bool isArrayValue(const QVariant &value)
{
    return value.isValid() && value.typeId() != QMetaType::QString
           && value.canConvert<QVariantList>();
}

} // namespace

/*!
 * \brief Returns the coarse kind of the OPC UA value \a value.
 *
 * An array value reports the kind of its first non-empty element; an array
 * without such an element is Empty.
 */
OpcUaValueKind opcUaValueKind(const QVariant &value)
{
    if (isArrayValue(value)) {
        const QVariantList list = value.toList();
        for (const QVariant &element : list) {
            const OpcUaValueKind kind = scalarKind(element);
            if (kind != OpcUaValueKind::Empty)
                return kind;
        }
        return OpcUaValueKind::Empty;
    }

    return scalarKind(value);
}

/*!
 * \brief Returns the display text of every element of the array value \a value.
 *
 * Each element uses QVariant string conversion. A scalar value, a string, or an
 * empty array yields an empty list.
 */
QStringList opcUaArrayElementTexts(const QVariant &value)
{
    if (!isArrayValue(value))
        return {};

    const QVariantList list = value.toList();
    QStringList texts;
    texts.reserve(list.size());
    for (const QVariant &element : list)
        texts.push_back(element.toString());
    return texts;
}
