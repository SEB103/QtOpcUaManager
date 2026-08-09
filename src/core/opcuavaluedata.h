#ifndef OPCUAVALUEDATA_H
#define OPCUAVALUEDATA_H

#include <QMetaType>
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
};

/** Registers one value-attribute update for QVariant and queued signal delivery. */
Q_DECLARE_METATYPE(OpcUaValueUpdate)

/** Registers one node attribute snapshot for QVariant and queued signal delivery. */
Q_DECLARE_METATYPE(OpcUaAttributeData)

#endif // OPCUAVALUEDATA_H
