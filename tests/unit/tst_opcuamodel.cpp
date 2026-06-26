#include <QSignalSpy>
#include <QtTest>

#include "models/opcuamodel.h"

class OpcUaModelTest : public QObject
{
    Q_OBJECT

private slots:
    void initialStateIsEmpty();
    void connectionRequestsRootFolderBrowse();
};

void OpcUaModelTest::initialStateIsEmpty()
{
    OpcUaModel model;

    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.canFetchMore(QModelIndex()));
}

void OpcUaModelTest::connectionRequestsRootFolderBrowse()
{
    OpcUaModel model;
    QSignalSpy fetchSpy(&model, &OpcUaModel::fetchChildrenRequested);

    model.setConnectionActive(true);

    QTRY_COMPARE(fetchSpy.count(), 1);
    QCOMPARE(fetchSpy.takeFirst().constFirst().toString(), QStringLiteral("ns=0;i=84"));
}

QTEST_GUILESS_MAIN(OpcUaModelTest)

#include "tst_opcuamodel.moc"
