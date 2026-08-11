#include "AgriculturalSprayPluginTest.h"

#include <QtCore/QRegularExpression>
#include <memory>

#include "AgriculturalSprayComplexItem.h"
#include "MissionController.h"
#include "PlanMasterController.h"
#include "QmlObjectListModel.h"
#include "SurveyComplexItem.h"

namespace {

AgriculturalSprayComplexItem* insertSpray(MissionController* missionController)
{
    return qobject_cast<AgriculturalSprayComplexItem*>(missionController->insertComplexMissionItem(
        AgriculturalSprayComplexItem::canonicalName, QGeoCoordinate(47.397742, 8.545594), -1, false));
}

}  // namespace

void AgriculturalSprayPluginTest::_singleSprayPolicyAndOtherComplexItems()
{
    AgriculturalSprayComplexItem* const first = insertSpray(missionController());
    QVERIFY(first);

    VisualMissionItem* const survey = missionController()->insertComplexMissionItem(
        SurveyComplexItem::canonicalName, QGeoCoordinate(47.397742, 8.545594), -1, false);
    QVERIFY(qobject_cast<SurveyComplexItem*>(survey));

    expectLogMessage("PlanManager.MissionController", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Complex mission item creation denied")));
    expectLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("Only one Agricultural Spray item")));
    QVERIFY(!insertSpray(missionController()));
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QCOMPARE(missionController()->visualItems()->count(), 3);

    missionController()->removeVisualItem(1);
    QVERIFY_TRUE_WAIT(missionController()->visualItems()->count() == 2, TestTimeout::shortMs());
    AgriculturalSprayComplexItem* const replacement = insertSpray(missionController());
    QVERIFY(replacement);
    QCOMPARE(missionController()->visualItems()->count(), 3);
}

void AgriculturalSprayPluginTest::_sprayPolicyIsPerPlan()
{
    AgriculturalSprayComplexItem* const firstPlanItem = insertSpray(missionController());
    QVERIFY(firstPlanItem);

    auto secondPlan = std::make_unique<PlanMasterController>();
    secondPlan->setFlyView(false);
    secondPlan->start();
    MissionController* const secondMissionController = secondPlan->missionController();
    QVERIFY(secondMissionController);
    QVERIFY(insertSpray(secondMissionController));
    expectLogMessage("PlanManager.MissionController", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Complex mission item creation denied")));
    expectLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("Only one Agricultural Spray item")));
    QVERIFY(!insertSpray(missionController()));
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    expectLogMessage("PlanManager.MissionController", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Complex mission item creation denied")));
    expectLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("Only one Agricultural Spray item")));
    QVERIFY(!insertSpray(secondMissionController));
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
}

UT_REGISTER_TEST(AgriculturalSprayPluginTest, TestLabel::Unit, TestLabel::MissionManager)
