#ifndef CLONEDATATYPE_H
#define CLONEDATATYPE_H

#include <QString>
#include <QVariant>

namespace ServerProject {

/** Server Studio data type chosen for a cloned variable. */
struct CloneDataTypeChoice
{
    /** One of builtinDataTypeNames(). */
    QString dataType;
    /** Whether \c dataType replaces a type Server Studio cannot model exactly. */
    bool substituted = false;
};

/**
 * Chooses the Server Studio data type for a cloned variable.
 *
 * \a builtInBase is the OPC UA base type of the source DataType (see
 * ResolvedDataType::builtInBase); empty when unresolved. \a value is the
 * captured value, used as evidence when the base type is abstract, unsupported
 * or unknown. A base type Server Studio supports is kept exactly; Enumeration
 * maps to Int32, its wire type. Otherwise the type of \a value decides, and
 * String is the last resort; such choices are marked as substituted.
 */
CloneDataTypeChoice cloneDataTypeFor(const QString &builtInBase, const QVariant &value);

} // namespace ServerProject

#endif // CLONEDATATYPE_H
