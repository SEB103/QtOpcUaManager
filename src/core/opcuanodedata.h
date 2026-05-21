#ifndef OPCUANODEDATA_H
#define OPCUANODEDATA_H

#include <QList>
#include <QMetaType>
#include <QString>

/*!
 * \struct OpcUaNodeData
 * \brief Immutable snapshot of one OPC UA node for transfer into the GUI model.
 * \details The OPC UA service emits lists of this value type after asynchronous
 * browse requests. OpcUaModel consumes those snapshots without exposing
 * QOpcUaNode instances or other protocol objects to QML.
 */
struct OpcUaNodeData
{
    /*! OPC UA node id string used for follow-up browse requests. */
    QString nodeId;

    /*! Browse name reported by the server. */
    QString browseName;

    /*! Display name shown in the tree model. */
    QString displayName;

    /*! Integer value of QOpcUa::NodeClass. */
    int nodeClass {0};

    /*! Whether the node should be treated as expandable by the GUI model. */
    bool hasChildren {false};
};

Q_DECLARE_METATYPE(OpcUaNodeData)
Q_DECLARE_METATYPE(QList<OpcUaNodeData>)

#endif // OPCUANODEDATA_H
