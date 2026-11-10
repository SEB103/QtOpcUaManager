#include <QtTest>

#include "models/nodecontext.h"

/*! Verifies the Kontext column text derived from a node's identity. */
class NodeContextTest : public QObject
{
    Q_OBJECT

private slots:
    /*! Verifies that a CODESYS member gets the path of its parent structure. */
    void codesysSymbolGivesParentPath();

    /*! Verifies that a CODESYS GVL variable gets the GVL name. */
    void codesysGvlVariableGivesGvl();

    /*! Verifies that an array element node keeps the path of the array's parent. */
    void arrayElementNodeKeepsIndex();

    /*! Verifies that other servers derive the context from the browse path. */
    void otherServersUseBrowsePath();

    /*! Verifies that a node directly below Objects has no context. */
    void nodeBelowObjectsHasNoContext();
};

/*!
 * \brief Verifies that a CODESYS member gets the path of its parent structure.
 */
void NodeContextTest::codesysSymbolGivesParentPath()
{
    QCOMPARE(NodeContext::contextOf(
                 QStringLiteral("ns=4;s=|var|CODESYS Control Win V3 x64.Application.V_HMI."
                                "astTestPrepContainer[1].stRefOffset.value"),
                 QStringLiteral("Objects/DeviceSet/CODESYS Control Win V3 x64/Resources/"
                                "Application/GlobalVars/V_HMI/astTestPrepContainer/"
                                "astTestPrepContainer[1]/stRefOffset/value"),
                 QStringLiteral("value")),
             QStringLiteral("V_HMI.astTestPrepContainer[1].stRefOffset"));
}

/*!
 * \brief Verifies that a CODESYS GVL variable gets the GVL name.
 */
void NodeContextTest::codesysGvlVariableGivesGvl()
{
    QCOMPARE(NodeContext::contextOf(
                 QStringLiteral("ns=4;s=|var|CODESYS Control Win V3 x64.Application.V_HMI."
                                "xGroupLevel1"),
                 QString(), QStringLiteral("xGroupLevel1")),
             QStringLiteral("V_HMI"));
}

/*!
 * \brief Verifies that an array element node keeps the path of the array's parent.
 */
void NodeContextTest::arrayElementNodeKeepsIndex()
{
    QCOMPARE(NodeContext::contextOf(
                 QStringLiteral("ns=4;s=|var|CODESYS Control Win V3 x64.Application.V_HMI."
                                "astTestPrepContainer[1]"),
                 QString(), QStringLiteral("astTestPrepContainer[1]")),
             QStringLiteral("V_HMI"));
}

/*!
 * \brief Verifies that other servers derive the context from the browse path.
 */
void NodeContextTest::otherServersUseBrowsePath()
{
    QCOMPARE(NodeContext::contextOf(QStringLiteral("ns=2;i=5"),
                                    QStringLiteral("Objects/Boiler/Drum/Level"),
                                    QStringLiteral("Level")),
             QStringLiteral("Boiler.Drum"));
}

/*!
 * \brief Verifies that a node directly below Objects has no context.
 */
void NodeContextTest::nodeBelowObjectsHasNoContext()
{
    QCOMPARE(NodeContext::contextOf(QStringLiteral("i=2253"), QStringLiteral("Objects/Server"),
                                    QStringLiteral("Server")),
             QString());
    QCOMPARE(NodeContext::contextOf(QStringLiteral("i=2253"), QString(),
                                    QStringLiteral("Server")),
             QString());
}

QTEST_GUILESS_MAIN(NodeContextTest)

#include "tst_nodecontext.moc"
