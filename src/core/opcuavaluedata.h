#ifndef OPCUAVALUEDATA_H
#define OPCUAVALUEDATA_H

#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>

/**
 * Immutable snapshot of a single OPC UA value-attribute update.
 *
 * The OPC UA service emits this value type from the worker thread whenever a
 * monitored node reports a data change. It carries only formatted text so that
 * no QOpcUaNode or other protocol object is exposed to the GUI thread or QML.
 */
struct OpcUaValueUpdate
{
    /** OPC UA node id string identifying the monitored node. */
    QString nodeId;

    /** Formatted current value text. */
    QString value;

    /** Human-readable data type text derived from the value. */
    QString dataType;

    /** Source timestamp text, or empty when unavailable. */
    QString sourceTimestamp;

    /** Server timestamp text, or empty when unavailable. */
    QString serverTimestamp;

    /** Status code text reported for the value attribute. */
    QString statusCode;
};

/**
 * Immutable snapshot of the main attributes of one OPC UA node.
 *
 * The OPC UA service reads these attributes on demand and emits the result to
 * the GUI thread, where AttributesModel expands it into name/value rows for the
 * Attributes panel.
 */
struct OpcUaAttributeData
{
    /** OPC UA node id string. */
    QString nodeId;

    /** Integer value of QOpcUa::NodeClass. */
    int nodeClass {0};

    /** Text form of the OPC UA node class. */
    QString nodeClassName;

    /** Browse name reported by the server. */
    QString browseName;

    /** Localized display name. */
    QString displayName;

    /** Description text, or empty when unavailable. */
    QString description;

    /** Formatted current value text. */
    QString value;

    /** Human-readable data type text derived from the value. */
    QString dataType;

    /** Source timestamp text, or empty when unavailable. */
    QString sourceTimestamp;

    /** Server timestamp text, or empty when unavailable. */
    QString serverTimestamp;

    /** Status code text reported for the value attribute. */
    QString statusCode;

    /**
     * OPC UA AccessLevel bit mask, or -1 when the attribute was not reported.
     * Bit 1 (CurrentWrite) decides whether a write can succeed at all.
     */
    int accessLevel {-1};

    /** OPC UA UserAccessLevel bit mask for the current session, or -1 when unknown. */
    int userAccessLevel {-1};

    /** OPC UA ValueRank, or the scalar/unknown sentinel -1. */
    int valueRank {-1};

    /** Array dimensions as reported by the server; empty for scalars. */
    QString arrayDimensions;

    /** Whether the server historizes this variable; -1 when the attribute is absent. */
    int historizing {-1};

    /** Fastest sampling interval the server supports, in ms; -1 when unknown. */
    double minimumSamplingInterval {-1.0};

    /** OPC UA WriteMask bit mask, or -1 when the attribute was not reported. */
    int writeMask {-1};

    /**
     * Symbolic names of an enumeration data type, as value/name pairs.
     *
     * Filled only when the node's data type is an enumeration the server
     * describes, so the value editor can offer the names instead of raw
     * integers. Empty for every other data type.
     */
    QList<QPair<qint64, QString>> enumOptions;
};

/** Registers one value-attribute update for QVariant and queued signal delivery. */
Q_DECLARE_METATYPE(OpcUaValueUpdate)

/** Registers one node attribute snapshot for QVariant and queued signal delivery. */
Q_DECLARE_METATYPE(OpcUaAttributeData)

#endif // OPCUAVALUEDATA_H
