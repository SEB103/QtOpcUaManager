#ifndef STRUCTUREDVALUEFORMATTER_H
#define STRUCTUREDVALUEFORMATTER_H

#include <QString>

#include "core/opcuavaluetree.h"

/**
 * Serializes a decoded OPC UA value tree into readable JSON or XML text.
 *
 * The formatter is a pure, stateless helper used by the GUI facade to render the
 * structured value of the selected node in the right-hand View panel. Both
 * outputs describe every node by its OPC UA type name and value, recursing into
 * arrays and structures.
 *
 * JSON shape (recursive), each node described as an object:
 * \code
 * { "type": <typeName>, "value": <value> [, "valueRank": <n>] }
 * \endcode
 * where a scalar value is a JSON primitive, a structure value is an object of
 * field-name to described child, and an array value is an array of described
 * children.
 *
 * XML shape (recursive): a scalar is \c <Value>, a structure is \c <Struct>, and
 * an array is \c <Array>. Each element carries a \c type attribute, an optional
 * \c name attribute for structure fields, and an optional \c valueRank attribute
 * for arrays.
 */
class StructuredValueFormatter
{
public:
    /** Output format selectable in the View menu. */
    enum class Format {
        /** Indented JSON. */
        Json = 0,
        /** Indented XML. */
        Xml
    };

    /** Returns \a root serialized in \a format. */
    static QString format(const OpcUaValueTreeNode &root, Format format);

private:
    /** Returns \a root serialized as indented JSON text. */
    static QString toJson(const OpcUaValueTreeNode &root);

    /** Returns \a root serialized as indented XML text. */
    static QString toXml(const OpcUaValueTreeNode &root);
};

#endif // STRUCTUREDVALUEFORMATTER_H
