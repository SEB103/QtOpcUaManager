#ifndef DATAACCESSMODEL_H
#define DATAACCESSMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

#include "core/opcuaaccesslevel.h"
#include "core/opcuavaluedata.h"
#include "persistence/nodedatabase.h"

/**
 * Table model backing the Data Access View.
 *
 * Each row represents one monitored node. Persistent identity and metadata come
 * from MonitoredNodeRecord; live value, timestamps, and status are filled in from
 * OpcUaValueUpdate snapshots as the server reports data changes.
 *
 * The model is column-oriented so the view can offer per-column sorting, sizing,
 * and visibility. Column 0 carries the cell text through Qt::DisplayRole; every
 * custom role describes the whole row and therefore returns the same value for
 * any column of that row, which lets a delegate read row identity from the cell
 * it is rendering.
 */
class DataAccessModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /** Table columns in fixed logical order. */
    enum Column {
        /** One-based row number. */
        RowNumberColumn = 0,
        /** Localized display name. */
        DisplayNameColumn,
        /** Formatted current value text. */
        ValueColumn,
        /** Human-readable data type text. */
        DataTypeColumn,
        /** Status code text reported with the last value. */
        StatusColumn,
        /** Requested sampling interval in milliseconds. */
        IntervalColumn,
        /** Source timestamp text. */
        SourceTimestampColumn,
        /** Server timestamp text. */
        ServerTimestampColumn,
        /** Human-readable browse path. */
        NodePathColumn,
        /** OPC UA node id string. */
        NodeIdColumn,
        /** Server identity the node belongs to. */
        ServerColumn,
        /** Number of columns; not a column itself. */
        ColumnCount
    };
    Q_ENUM(Column)

    /** Whether the value of a row can be written, as far as the server has said. */
    enum Writability {
        /** The server has not reported an AccessLevel for this node yet. */
        WritabilityUnknown = 0,
        /** The AccessLevel grants CurrentWrite. */
        WritabilityWritable,
        /** The AccessLevel withholds CurrentWrite, so a write would be rejected. */
        WritabilityReadOnly
    };
    Q_ENUM(Writability)

    /** Coarse quality classification derived from the OPC UA status code text. */
    enum StatusSeverity {
        /** No status reported yet, for example before the first value arrives. */
        StatusUnknown = 0,
        /** The server reported a Good status. */
        StatusGood,
        /** The server reported an Uncertain status. */
        StatusUncertain,
        /** The server reported a Bad status. */
        StatusBad
    };
    Q_ENUM(StatusSeverity)

    /** Custom roles exposed to QML table delegates; all describe the whole row. */
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
        StatusCodeRole,
        /** Coarse quality as a StatusSeverity value. */
        StatusSeverityRole,
        /** Requested sampling interval in milliseconds; 0 means the default. */
        SamplingIntervalRole,
        /**
         * Milliseconds since the epoch of the last applied value update, or 0
         * when no value has arrived yet.
         *
         * The view uses it only to distinguish "nothing has arrived" from "the
         * value is an empty string". It is deliberately not used to flag a row
         * as outdated: an OPC UA subscription reports data changes, so a
         * constant variable legitimately produces no updates at all.
         */
        LastUpdateMsRole,
        /** Sort key for the cell's own column, typed for numeric comparison. */
        SortValueRole,
        /** OPC UA AccessLevel bit mask of the row's node; -1 while unknown. */
        AccessLevelRole,
        /** Whether the row can be written, as a Writability value. */
        WritabilityRole
    };
    Q_ENUM(Role)

    /** Creates an empty model. */
    explicit DataAccessModel(QObject *parent = nullptr);

    /** Replaces all rows with \a records, clearing any live values. */
    void setRecords(const QList<MonitoredNodeRecord> &records);

    /** Returns the persistent identity and metadata of every row, in display order. */
    QList<MonitoredNodeRecord> records() const;

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

    /** Returns the browse path at \a row, or an empty string when out of range. */
    Q_INVOKABLE QString nodePathAt(int row) const;

    /** Returns the data-type text at \a row, or an empty string when out of range. */
    Q_INVOKABLE QString dataTypeAt(int row) const;

    /** Returns the display name at  row, or an empty string when out of range. */
    Q_INVOKABLE QString displayNameAt(int row) const;

    /** Returns the sampling interval at \a row in milliseconds, or 0 when out of range. */
    Q_INVOKABLE int samplingIntervalAt(int row) const;

    /** Returns the AccessLevel at \a row, or -1 when unknown or out of range. */
    Q_INVOKABLE int accessLevelAt(int row) const;

    /** Returns the Writability of \a row. */
    Q_INVOKABLE int writabilityAt(int row) const;

    /**
     * Records the AccessLevel \a accessLevel for the row holding \a nodeId.
     * \return \c true when a row was updated.
     *
     * The access level is runtime information read from the server rather than
     * project state, so it is never persisted and starts out unknown for rows
     * restored from a project file.
     */
    bool setAccessLevelForNode(const QString &nodeId, int accessLevel);

    /**
     * Sets the sampling interval at \a row to \a intervalMs milliseconds.
     * \return \c true when the value changed, so the caller can re-subscribe.
     *
     * A non-positive interval is stored as 0, which selects the service default.
     */
    bool setSamplingIntervalAt(int row, int intervalMs);

    /** Returns the translated header title of \a column. */
    Q_INVOKABLE QString columnTitle(int column) const;

    /** Returns data for \a index and \a role. */
    QVariant data(const QModelIndex &index, int role) const override;
    /** Returns horizontal header titles. */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    /** Returns the number of rows below \a parent. */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns the number of columns below \a parent. */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
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
        /** Milliseconds since the epoch of the last applied update; 0 when none. */
        qint64 lastUpdateMs {0};
        /** OPC UA AccessLevel of the node; -1 while the server has not said. */
        int accessLevel {OpcUaAccessLevel::Unknown};
    };

    /** Returns the row index for \a nodeId, or -1 when not present. */
    int indexForNodeId(const QString &nodeId) const;

    /** Returns the display text of \a column for \a row. */
    QString cellText(const Row &row, int column, int rowNumber) const;

    /**
     * Returns a typed sort key for \a column of \a row.
     *
     * Numeric columns return numbers so the view sorts 9 before 10, and the
     * value column returns a number whenever the current value parses as one.
     */
    QVariant sortValue(const Row &row, int column, int rowNumber) const;

    /** Owned rows in display order. */
    QList<Row> m_rows;
};

#endif // DATAACCESSMODEL_H
