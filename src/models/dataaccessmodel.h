#ifndef DATAACCESSMODEL_H
#define DATAACCESSMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>

#include "core/opcuavaluedata.h"
#include "persistence/nodedatabase.h"

/**
 * List model backing the Data Access View table.
 *
 * Each row represents one monitored node. Persistent identity and metadata come
 * from MonitoredNodeRecord; live value, timestamps, and status are filled in from
 * OpcUaValueUpdate snapshots as the server reports data changes.
 */
class DataAccessModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /** Custom roles exposed to QML table delegates. */
    enum Role {
        /** One-based row number shown in the first column. */
        RowNumberRole = Qt::UserRole + 1,
        /** Server identity the node belongs to. */
        ServerRole,
        /** OPC UA node id string. */
        NodeIdRole,
        /** Human-readable browse path. */
        NodePathRole,
        /** Localized display name. */
        DisplayNameRole,
        /** Formatted current value text. */
        ValueRole,
        /** Human-readable data type text. */
        DataTypeRole,
        /** Source timestamp text. */
        SourceTimestampRole,
        /** Server timestamp text. */
        ServerTimestampRole,
        /** Status code text. */
        StatusCodeRole
    };
    Q_ENUM(Role)

    /** Creates an empty model. */
    explicit DataAccessModel(QObject *parent = nullptr);

    /** Replaces all rows with \a records, clearing any live values. */
    void setRecords(const QList<MonitoredNodeRecord> &records);

    /** Appends \a record when it is not already present. Returns \c true if a row was added. */
    bool addRow(const MonitoredNodeRecord &record);

    /** Removes the row at \a row. */
    void removeAt(int row);

    /** Applies a live value \a update to the row matching its node id. */
    void updateValue(const OpcUaValueUpdate &update);

    /** Clears live value, timestamp, and status text for every row. */
    void clearValues();

    /** Returns whether a row with \a server and \a nodeId already exists. */
    bool contains(const QString &server, const QString &nodeId) const;

    /** Returns the node id at \a row, or an empty string when out of range. */
    Q_INVOKABLE QString nodeIdAt(int row) const;

    /** Returns the server at \a row, or an empty string when out of range. */
    Q_INVOKABLE QString serverAt(int row) const;

    /** Returns the current value text at \a row, or an empty string when out of range. */
    Q_INVOKABLE QString valueAt(int row) const;

    /** Returns data for \a index and \a role. */
    QVariant data(const QModelIndex &index, int role) const override;
    /** Returns the number of rows below \a parent. */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns role names exposed to QML. */
    QHash<int, QByteArray> roleNames() const override;

private:
    /** One monitored-node row combining persistent metadata and live value data. */
    struct Row
    {
        /** Persistent identity and display metadata. */
        MonitoredNodeRecord record;
        /** Formatted current value text. */
        QString value;
        /** Source timestamp text. */
        QString sourceTimestamp;
        /** Server timestamp text. */
        QString serverTimestamp;
        /** Status code text. */
        QString statusCode;
    };

    /** Returns the row index for \a nodeId, or -1 when not present. */
    int indexForNodeId(const QString &nodeId) const;

    /** Owned rows in display order. */
    QList<Row> m_rows;
};

#endif // DATAACCESSMODEL_H
