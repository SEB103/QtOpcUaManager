#include <QtTest>

#include "models/dataviewnames.h"

using DataViewNames::Entry;
using DataViewNames::Name;

/*!
 * \internal
 * \brief Builds an entry for \a path on \a server whose display name is the last path segment.
 */
static Entry entry(const QString &path, const QString &server = QStringLiteral("srv"))
{
    return Entry {server, path, path.section(QLatin1Char('/'), -1)};
}

/*!
 * \internal
 * \brief Returns the full text of every name in \a names.
 */
static QStringList texts(const QList<Name> &names)
{
    QStringList result;
    for (const Name &name : names)
        result.append(name.text());
    return result;
}

/*! Verifies the shortest-unique-tail naming of Data View rows. */
class DataViewNamesTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that array indexes become tokens of their own. */
    void tokensSplitArrayIndex();

    /*! Verifies that a name without a collision is shown unchanged. */
    void uniqueNamesStayUnchanged();

    /*! Verifies that the array index alone qualifies elements of one array. */
    void indexQualifiesArrayElements();

    /*! Verifies that a tail that collides again grows by exactly one token. */
    void collidingTailGrowsByOneToken();

    /*! Verifies that a name shrinks back once its collision is gone. */
    void removingRowsShrinksNames();

    /*! Verifies that a row watching a whole array element keeps that segment together. */
    void structureElementRowUsesWholeSegment();

    /*! Verifies that identical paths on different servers are told apart by server. */
    void identicalPathsOnDifferentServersGetServerSuffix();

    /*! Verifies that rows without a stored path fall back to their display name. */
    void emptyPathFallsBackToDisplayName();
};

/*!
 * \brief Verifies that array indexes become tokens of their own.
 */
void DataViewNamesTest::tokensSplitArrayIndex()
{
    QCOMPARE(DataViewNames::tokens(
                 QStringLiteral("A/V_HMI/astTestPrepContainer[1]/stRefOffset/value"),
                 QStringLiteral("value")),
             (QStringList {QStringLiteral("A"), QStringLiteral("V_HMI"),
                           QStringLiteral("astTestPrepContainer"), QStringLiteral("[1]"),
                           QStringLiteral("stRefOffset"), QStringLiteral("value")}));
    QCOMPARE(DataViewNames::tokens(QStringLiteral("A/m[1,2]"), QStringLiteral("m[1,2]")),
             (QStringList {QStringLiteral("A"), QStringLiteral("m"), QStringLiteral("[1,2]")}));
    QCOMPARE(DataViewNames::tokens(QStringLiteral("A/[3]"), QStringLiteral("[3]")),
             (QStringList {QStringLiteral("A"), QStringLiteral("[3]")}));
}

/*!
 * \brief Verifies that a name without a collision is shown unchanged.
 */
void DataViewNamesTest::uniqueNamesStayUnchanged()
{
    const QList<Name> names =
        DataViewNames::structuredNames({entry(QStringLiteral("V_HMI/xGroupLevel1"))});

    QCOMPARE(names.size(), 1);
    QCOMPARE(names.at(0).text(), QStringLiteral("xGroupLevel1"));
    QVERIFY(names.at(0).prefix.isEmpty());
    QVERIFY(names.at(0).suffix.isEmpty());
}

/*!
 * \brief Verifies that the array index alone qualifies elements of one array.
 */
void DataViewNamesTest::indexQualifiesArrayElements()
{
    const QList<Name> names = DataViewNames::structuredNames({
        entry(QStringLiteral("Application/V_HMI/xGroupLevel1")),
        entry(QStringLiteral("Application/V_HMI/astTestPrepContainer[1]/stRefOffset/value")),
        entry(QStringLiteral("Application/V_HMI/astTestPrepContainer[2]/stRefOffset/value")),
        entry(QStringLiteral("Application/V_HMI/astTestPrepContainer[1]/stTolerance/value")),
    });

    QCOMPARE(texts(names),
             (QStringList {QStringLiteral("xGroupLevel1"), QStringLiteral("[1].stRefOffset.value"),
                           QStringLiteral("[2].stRefOffset.value"),
                           QStringLiteral("stTolerance.value")}));
    QCOMPARE(names.at(1).prefix, QStringLiteral("[1].stRefOffset."));
    QCOMPARE(names.at(1).leaf, QStringLiteral("value"));
}

/*!
 * \brief Verifies that a tail that collides again grows by exactly one token.
 */
void DataViewNamesTest::collidingTailGrowsByOneToken()
{
    const QList<Name> names = DataViewNames::structuredNames({
        entry(QStringLiteral("Application/V_HMI/astTestPrepContainer[1]/stRefOffset/value")),
        entry(QStringLiteral("Application/V_HMI/astTestPrepContainer[2]/stRefOffset/value")),
        entry(QStringLiteral("Application/V_HMI/astOtherContainer[1]/stRefOffset/value")),
    });

    QCOMPARE(texts(names),
             (QStringList {QStringLiteral("astTestPrepContainer[1].stRefOffset.value"),
                           QStringLiteral("[2].stRefOffset.value"),
                           QStringLiteral("astOtherContainer[1].stRefOffset.value")}));
}

/*!
 * \brief Verifies that a name shrinks back once its collision is gone.
 */
void DataViewNamesTest::removingRowsShrinksNames()
{
    const QList<Name> names = DataViewNames::structuredNames({
        entry(QStringLiteral("Application/V_HMI/xGroupLevel1")),
        entry(QStringLiteral("Application/V_HMI/astTestPrepContainer[1]/stRefOffset/value")),
    });

    QCOMPARE(texts(names),
             (QStringList {QStringLiteral("xGroupLevel1"), QStringLiteral("value")}));
}

/*!
 * \brief Verifies that a row watching a whole array element keeps that segment together.
 */
void DataViewNamesTest::structureElementRowUsesWholeSegment()
{
    const QList<Name> names = DataViewNames::structuredNames({
        entry(QStringLiteral("G1/aMotors[1]")),
        entry(QStringLiteral("G2/aMotors[1]")),
    });

    QCOMPARE(texts(names),
             (QStringList {QStringLiteral("G1.aMotors[1]"), QStringLiteral("G2.aMotors[1]")}));
    QCOMPARE(names.at(0).prefix, QStringLiteral("G1."));
    QCOMPARE(names.at(0).leaf, QStringLiteral("aMotors[1]"));
}

/*!
 * \brief Verifies that identical paths on different servers are told apart by server.
 */
void DataViewNamesTest::identicalPathsOnDifferentServersGetServerSuffix()
{
    const QList<Name> names = DataViewNames::structuredNames({
        entry(QStringLiteral("Objects/x/value"), QStringLiteral("PLC-1")),
        entry(QStringLiteral("Objects/x/value"), QStringLiteral("PLC-2")),
    });

    QCOMPARE(names.at(0).suffix, QStringLiteral(" (PLC-1)"));
    QCOMPARE(names.at(1).suffix, QStringLiteral(" (PLC-2)"));
    QCOMPARE(names.at(0).leaf, QStringLiteral("value"));
}

/*!
 * \brief Verifies that rows without a stored path fall back to their display name.
 */
void DataViewNamesTest::emptyPathFallsBackToDisplayName()
{
    const Entry lonely {QStringLiteral("srv"), QString(), QStringLiteral("value")};
    QCOMPARE(texts(DataViewNames::structuredNames({lonely})),
             QStringList {QStringLiteral("value")});

    const Entry onA {QStringLiteral("a"), QString(), QStringLiteral("value")};
    const Entry onB {QStringLiteral("b"), QString(), QStringLiteral("value")};
    QCOMPARE(texts(DataViewNames::structuredNames({onA, onB})),
             (QStringList {QStringLiteral("value (a)"), QStringLiteral("value (b)")}));
}

QTEST_GUILESS_MAIN(DataViewNamesTest)

#include "tst_dataviewnames.moc"
