#include "clonedatatype.h"

#include "serverprojectdata.h"

#include <QVariantList>

namespace ServerProject {

namespace {

/*!
 * \internal
 * \brief Returns the Server Studio type matching the Qt type of \a value, or an empty string.
 *
 * Arrays are judged by their first element. Values whose Qt type has no
 * Server Studio counterpart (date/time, byte arrays, Qt OPC UA value types,
 * an invalid value) yield an empty string.
 */
QString dataTypeForValue(const QVariant &value)
{
    QVariant sample = value;
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        if (list.isEmpty())
            return {};
        sample = list.first();
    }

    switch (sample.typeId()) {
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
    default: return {};
    }
}

} // namespace

/*!
 * \brief Chooses the Server Studio data type for a cloned variable.
 * \param builtInBase OPC UA base type of the source DataType; empty when unresolved.
 * \param value Captured value of the variable, used when the base type is not supported.
 *
 * Order of decision:
 * \list 1
 * \li A base type contained in builtinDataTypeNames() is used unchanged.
 * \li Enumeration maps to Int32, the type enumeration values have on the wire.
 * \li Otherwise (structures, DateTime, ByteString, LocalizedText, abstract
 *     bases, unresolved types) the Qt type of \a value decides.
 * \li String is the last resort, for example for an ExtensionObject value.
 * \endlist
 *
 * Every choice other than the first two is marked as substituted so the
 * caller can report that the clone differs from the source.
 */
CloneDataTypeChoice cloneDataTypeFor(const QString &builtInBase, const QVariant &value)
{
    if (builtinDataTypeNames().contains(builtInBase))
        return {builtInBase, false};
    if (builtInBase == QLatin1String("Enumeration"))
        return {QStringLiteral("Int32"), false};

    const QString fromValue = dataTypeForValue(value);
    if (!fromValue.isEmpty())
        return {fromValue, true};
    return {QStringLiteral("String"), true};
}

} // namespace ServerProject
