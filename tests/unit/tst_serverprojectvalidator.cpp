// Unit tests for the .uaserver project validator.

#include <QtTest>

#include "serverproject/serverprojectdata.h"
#include "serverproject/serverprojectvalidator.h"

using namespace ServerProject;

/*! Verifies the validator accepts good projects and flags each defect class. */
class ServerProjectValidatorTest : public QObject
{
    Q_OBJECT

private slots:
    /*! A well-formed project validates cleanly. */
    void acceptsValidProject();

    /*! A malformed node id is reported. */
    void rejectsMalformedNodeId();

    /*! A duplicate node id is reported. */
    void rejectsDuplicateNodeId();

    /*! A dangling parent reference is reported. */
    void rejectsMissingParent();

    /*! An unsupported variable data type is reported. */
    void rejectsUnknownDataType();

    /*! A namespace index beyond the declared namespaces is reported. */
    void rejectsNamespaceOutOfRange();

    /*! Disabling anonymous access with no user accounts is reported. */
    void rejectsNoAuthenticationMethod();

    /*! A rule whose trigger and targets are variables validates cleanly. */
    void acceptsValidRule();

    /*! A rule triggering on a non-variable node is reported. */
    void rejectsRuleWithNonVariableTrigger();

    /*! A rule writing a missing target node is reported. */
    void rejectsRuleWithMissingTarget();

private:
    /*! Returns a minimal valid project with a folder and one scalar variable. */
    static ProjectData makeValidProject();

private slots:
    /*! A structure variable with a declared structure type and a 2-D array are valid. */
    void acceptsStructuresAndMultiDimensionalArrays();

    /*! A structure variable must reference a declared structure type. */
    void rejectsMissingStructureType();

    /*! Array dimensions must match the value rank and a 2-D value its dimensions. */
    void rejectsInconsistentArrayDimensions();
};

ProjectData ServerProjectValidatorTest::makeValidProject()
{
    ProjectData data;
    data.displayName = QStringLiteral("Valid");
    data.namespaces.append({QStringLiteral("urn:opcuamanager:demo")});

    Node folder;
    folder.kind = NodeKind::Folder;
    folder.nodeId = QStringLiteral("ns=1;s=Test");
    folder.browseName = QStringLiteral("Test");
    data.nodes.append(folder);

    Node variable;
    variable.kind = NodeKind::Variable;
    variable.nodeId = QStringLiteral("ns=1;s=Test.Value");
    variable.parentNodeId = folder.nodeId;
    variable.browseName = QStringLiteral("Value");
    variable.dataType = QStringLiteral("Int32");
    variable.valueRank = -1;
    data.nodes.append(variable);

    return data;
}

void ServerProjectValidatorTest::acceptsValidProject()
{
    const Validator::Result result = Validator::validate(makeValidProject());
    QVERIFY2(result.ok, qPrintable(result.errors.join(QLatin1String("; "))));
}

void ServerProjectValidatorTest::rejectsMalformedNodeId()
{
    ProjectData data = makeValidProject();
    data.nodes[1].nodeId = QStringLiteral("not-a-node-id");
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::rejectsDuplicateNodeId()
{
    ProjectData data = makeValidProject();
    data.nodes[1].nodeId = data.nodes[0].nodeId;
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::rejectsMissingParent()
{
    ProjectData data = makeValidProject();
    data.nodes[1].parentNodeId = QStringLiteral("ns=1;s=DoesNotExist");
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::rejectsUnknownDataType()
{
    ProjectData data = makeValidProject();
    data.nodes[1].dataType = QStringLiteral("NotAType");
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::rejectsNamespaceOutOfRange()
{
    ProjectData data = makeValidProject();
    data.nodes[0].nodeId = QStringLiteral("ns=5;s=Test");
    data.nodes[1].parentNodeId = data.nodes[0].nodeId;
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::rejectsNoAuthenticationMethod()
{
    ProjectData data = makeValidProject();
    data.security.allowAnonymous = false;
    data.security.users.clear();
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::acceptsValidRule()
{
    ProjectData data = makeValidProject();
    Rule rule;
    rule.triggerNodeId = QStringLiteral("ns=1;s=Test.Value");
    rule.actions.append({QStringLiteral("ns=1;s=Test.Value"), RuleValueMode::CopyTrigger,
                         QVariant(), 0.0});
    data.rules.append(rule);
    const Validator::Result result = Validator::validate(data);
    QVERIFY2(result.ok, qPrintable(result.errors.join(QLatin1String("; "))));
}

void ServerProjectValidatorTest::rejectsRuleWithNonVariableTrigger()
{
    ProjectData data = makeValidProject();
    Rule rule;
    // The Test folder is not a variable, so it cannot be a trigger.
    rule.triggerNodeId = QStringLiteral("ns=1;s=Test");
    rule.actions.append({QStringLiteral("ns=1;s=Test.Value"), RuleValueMode::CopyTrigger,
                         QVariant(), 0.0});
    data.rules.append(rule);
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::rejectsRuleWithMissingTarget()
{
    ProjectData data = makeValidProject();
    Rule rule;
    rule.triggerNodeId = QStringLiteral("ns=1;s=Test.Value");
    rule.actions.append({QStringLiteral("ns=1;s=Missing"), RuleValueMode::Literal,
                         QVariant(1), 0.0});
    data.rules.append(rule);
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.ok);
}

void ServerProjectValidatorTest::acceptsStructuresAndMultiDimensionalArrays()
{
    ProjectData data = makeValidProject();
    data.structTypes.append({QStringLiteral("ST_Button"), QStringLiteral("ns=1;s=Type.Button"),
                             QString()});

    Node button;
    button.kind = NodeKind::Variable;
    button.nodeId = QStringLiteral("ns=1;s=Test.Button");
    button.parentNodeId = QStringLiteral("ns=1;s=Test");
    button.browseName = QStringLiteral("Button");
    button.structTypeId = QStringLiteral("ns=1;s=Type.Button");
    data.nodes.append(button);

    Node matrix;
    matrix.kind = NodeKind::Variable;
    matrix.nodeId = QStringLiteral("ns=1;s=Test.Matrix");
    matrix.parentNodeId = QStringLiteral("ns=1;s=Test");
    matrix.browseName = QStringLiteral("Matrix");
    matrix.dataType = QStringLiteral("Int32");
    matrix.valueRank = 2;
    matrix.arrayDimensions = {2, 2};
    matrix.initialValue = QVariantList{1, 2, 3, 4};
    data.nodes.append(matrix);

    const Validator::Result result = Validator::validate(data);
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QStringLiteral("; "))));
}

void ServerProjectValidatorTest::rejectsMissingStructureType()
{
    ProjectData data = makeValidProject();
    data.nodes[1].structTypeId = QStringLiteral("ns=1;s=Type.Missing");
    const Validator::Result result = Validator::validate(data);
    QVERIFY(!result.errors.filter(QStringLiteral("missing structure type")).isEmpty());
}

void ServerProjectValidatorTest::rejectsInconsistentArrayDimensions()
{
    ProjectData data = makeValidProject();
    data.nodes[1].valueRank = 2;
    data.nodes[1].arrayDimensions = {3};
    Validator::Result result = Validator::validate(data);
    QVERIFY(!result.errors.filter(QStringLiteral("array dimension(s)")).isEmpty());

    data.nodes[1].arrayDimensions = {2, 2};
    data.nodes[1].initialValue = QVariantList{1, 2, 3};
    result = Validator::validate(data);
    QVERIFY(!result.errors.filter(QStringLiteral("initial value(s)")).isEmpty());
}

QTEST_GUILESS_MAIN(ServerProjectValidatorTest)

#include "tst_serverprojectvalidator.moc"
