#ifndef NETWORKSCANMODEL_H
#define NETWORKSCANMODEL_H

#include <QAbstractListModel>
#include <QList>

#include "core/networkscanresult.h"

/** Table of network scan results, ordered by numeric IPv4 address and port. */
class NetworkScanModel : public QAbstractListModel
{
    Q_OBJECT

    /** Number of result rows. */
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    /** Item roles exposed to QML. */
    enum Role {
        AddressRole = Qt::UserRole + 1,
        PortRole,
        UrlRole,
        ApplicationNameRole,
        ApplicationUriRole,
        ProductUriRole,
        SecuritySummaryRole,
        AuthSummaryRole,
        ResponseMsRole,
        StatusRole,
        ErrorTextRole
    };
    Q_ENUM(Role)

    /** Creates an empty model owned by \a parent. */
    explicit NetworkScanModel(QObject *parent = nullptr);

    /** Returns the number of result rows. */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** Returns the value of \a role for the row at \a index. */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /** Returns the QML role names. */
    QHash<int, QByteArray> roleNames() const override;

    /** Returns the number of result rows. */
    int count() const;
    /** Removes all rows. */
    void clear();
    /** Inserts \a result in order, or updates the row with the same address and port. */
    void upsert(const NetworkScanResult &result);
    /** Returns the result at \a row, or a default result when \a row is out of range. */
    NetworkScanResult resultAt(int row) const;

signals:
    /** Emitted when the number of rows changes. */
    void countChanged();

private:
    /** Rows in address/port order. */
    QList<NetworkScanResult> m_rows;
};

#endif // NETWORKSCANMODEL_H
