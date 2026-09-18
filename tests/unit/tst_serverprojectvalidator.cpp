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

QTEST_GUILESS_MAIN(ServerProjectValidatorTest)

#include "tst_serverprojectvalidator.moc"
