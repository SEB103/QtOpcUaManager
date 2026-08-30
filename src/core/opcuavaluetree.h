#ifndef OPCUAVALUETREE_H
#define OPCUAVALUETREE_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariant>

/**
 * Recursive snapshot of a decoded OPC UA value for transfer into the GUI.
 *
 * The OPC UA service reads a variable's value and reconstructs its logical
 * layout as a tree of these nodes: a scalar is a leaf, an array carries one
 * child per element, and a structure carries one child per field. Nested cases
 * (array of structures, structure of arrays, structure of structures) are
 * represented by further nesting. The tree carries only value types so that no
 * QOpcUaNode or other protocol object is exposed to the GUI thread or QML.
 */
struct OpcUaValueTreeNode
{
    /** Category of a decoded value node. */
    enum class Kind {
        /** A single scalar value. */
        Scalar = 0,
        /** An array whose \c children hold the elements. */
        Array,
        /** A structure whose \c children hold the fields. */
        Struct
    };

    /** Field name for a structure member; empty for the root and array elements. */
    QString name;

    /** Human-readable OPC UA type name (e.g. "UInt32", "String", or a struct type name). */
    QString typeName;

    /** DataType node id (e.g. "ns=0;i=6") when known; empty otherwise. */
    QString typeId;

    /** OPC UA ValueRank; -1 means scalar, 0 or more indicates an array. */
    int valueRank {-1};

    /** Category of this node. */
    Kind kind {Kind::Scalar};

    /**
     * Typed scalar value for \c Kind::Scalar nodes, so numeric and boolean
     * values keep their type in JSON output. Invalid for arrays and structures.
     */
    QVariant scalarValue;

    /** Display text for a scalar value; empty for arrays and structures. */
    QString scalarText;

    /** Whether decoding this node failed and \c scalarText holds a fallback. */
    bool decodeError {false};

    /** Struct fields or array elements, in declaration/index order. */
    QList<OpcUaValueTreeNode> children;
};

/** Registers one decoded value tree for QVariant and queued signal delivery. */
Q_DECLARE_METATYPE(OpcUaValueTreeNode)

#endif // OPCUAVALUETREE_H
