#include "AgriculturalSprayPluginTest.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <memory>

#include "AgriculturalSprayComplexItem.h"
#include "GeoFenceController.h"
#include "MissionController.h"
#include "PlanMasterController.h"
#include "QGCOptions.h"
#include "QGCFenceCircle.h"
#include "QGCFencePolygon.h"
#include "QGCCorePlugin.h"
#include "QmlObjectListModel.h"
#include "SurveyComplexItem.h"

namespace {

AgriculturalSprayComplexItem* insertSpray(MissionController* missionController)
{
    return qobject_cast<AgriculturalSprayComplexItem*>(missionController->insertComplexMissionItem(
        AgriculturalSprayComplexItem::canonicalName, QGeoCoordinate(47.397742, 8.545594), -1, false));
}

QList<QGeoCoordinate> square(const QGeoCoordinate& center)
{
    return {
        center.atDistanceAndAzimuth(50.0, -45.0),
        center.atDistanceAndAzimuth(50.0, 45.0),
        center.atDistanceAndAzimuth(50.0, 135.0),
        center.atDistanceAndAzimuth(50.0, -135.0),
    };
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

void AgriculturalSprayPluginTest::_interactiveCreationBindsNewPolygonButImportDoesNot()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    GeoFenceController* const fenceController = planController()->geoFenceController();
    QVERIFY(fenceController);
    fenceController->addInclusionPolygon(center.atDistanceAndAzimuth(50.0, -45.0),
                                         center.atDistanceAndAzimuth(50.0, 135.0));
    QmlObjectListModel* const polygons = fenceController->polygons();
    QCOMPARE(polygons->count(), 1);
    QGCFencePolygon* const existing = polygons->value<QGCFencePolygon*>(0);
    QVERIFY(existing);
    existing->setInteractive(true);
    QGCFenceCircle* const existingCircle = fenceController->addCircle(center, 10.0, true);
    QVERIFY(existingCircle);
    existingCircle->setInteractive(true);
    QCOMPARE(fenceController->circles()->count(), 1);

    QSignalSpy editLayerSpy(missionController(), &MissionController::planEditLayerRequested);
    QVERIFY(editLayerSpy.isValid());
    AgriculturalSprayComplexItem* const interactive = insertSpray(missionController());
    QVERIFY(interactive);
    QCOMPARE(polygons->count(), 2);
    QCOMPARE(editLayerSpy.count(), 1);
    QCOMPARE(editLayerSpy.constFirst().constFirst().toString(), QStringLiteral("fenceGroup"));

    QGCFencePolygon* const source = polygons->value<QGCFencePolygon*>(1);
    QVERIFY(source);
    QVERIFY(source->inclusion());
    QVERIFY(source->interactive());
    QVERIFY(source->traceMode());
    QVERIFY(source->coordinateList().isEmpty());
    QVERIFY(!existing->interactive());
    QVERIFY(!existing->traceMode());
    QVERIFY(!existingCircle->interactive());

    missionController()->removeVisualItem(1);
    QVERIFY_TRUE_WAIT(missionController()->visualItems()->count() == 1, TestTimeout::shortMs());
    AgriculturalSprayComplexItem* const imported = qobject_cast<AgriculturalSprayComplexItem*>(
        missionController()->insertComplexMissionItemFromKMLOrSHP(
            AgriculturalSprayComplexItem::canonicalName, QString(), -1, false));
    QVERIFY(imported);
    QCOMPARE(polygons->count(), 2);
    QCOMPARE(editLayerSpy.count(), 1);
}

void AgriculturalSprayPluginTest::_traceCompletionBuildsRouteAndRestoresMissionLayer()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    GeoFenceController* const fenceController = planController()->geoFenceController();
    QVERIFY(fenceController);
    fenceController->addInclusionPolygon(center.atDistanceAndAzimuth(50.0, -45.0),
                                         center.atDistanceAndAzimuth(50.0, 135.0));
    QmlObjectListModel* const polygons = fenceController->polygons();
    QGCFencePolygon* const unrelated = polygons->value<QGCFencePolygon*>(0);
    QVERIFY(unrelated);

    QSignalSpy editLayerSpy(missionController(), &MissionController::planEditLayerRequested);
    QVERIFY(editLayerSpy.isValid());
    AgriculturalSprayComplexItem* const item = insertSpray(missionController());
    QVERIFY(item);
    QGCFencePolygon* const source = polygons->value<QGCFencePolygon*>(1);
    QVERIFY(source);
    QVERIFY(source->traceMode());

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*trace is incomplete")));
    source->setPath(square(center));
    QCOMPARE_TRUE_WAIT(item->status(), AgriculturalSprayComplexItem::InvalidArea, TestTimeout::shortMs());
    verifyExpectedLogMessage();

    source->setTraceMode(false);
    QVERIFY_TRUE_WAIT(item->status() == AgriculturalSprayComplexItem::Ready && editLayerSpy.count() == 2,
                      TestTimeout::shortMs());
    QVERIFY(!item->routeCoordinates().isEmpty());
    QCOMPARE(missionController()->currentPlanViewItem(), item);
    QCOMPARE(editLayerSpy.count(), 2);
    QCOMPARE(editLayerSpy.constFirst().constFirst().toString(), QStringLiteral("fenceGroup"));
    QCOMPARE(editLayerSpy.constLast().constFirst().toString(), QStringLiteral("missionGroup"));

    QSignalSpy routeSpy(item, &AgriculturalSprayComplexItem::routeCoordinatesChanged);
    QVERIFY(routeSpy.isValid());
    unrelated->setTraceMode(true);
    unrelated->setTraceMode(false);

    QTimer eventLoopTurn;
    eventLoopTurn.setSingleShot(true);
    QSignalSpy eventLoopTurnSpy(&eventLoopTurn, &QTimer::timeout);
    QVERIFY(eventLoopTurnSpy.isValid());
    eventLoopTurn.start(0);
    QVERIFY_SIGNAL_WAIT(eventLoopTurnSpy, TestTimeout::shortMs());
    QCOMPARE(editLayerSpy.count(), 2);
    QCOMPARE(routeSpy.count(), 0);
}

void AgriculturalSprayPluginTest::_customAndStockPlanOptionsRemainDistinct()
{
    QGCOptions stockOptions;
    QGCOptions* const customOptions = QGCCorePlugin::instance()->options();
    QVERIFY(customOptions);

    QVERIFY(stockOptions.showPlanInfo());
    QVERIFY(stockOptions.showPlanDefaults());
    QVERIFY(stockOptions.showInitialCameraSettings());
    QVERIFY(stockOptions.showRallyPoints());
    QVERIFY(stockOptions.showTransform());
    QVERIFY(stockOptions.showRallyLayer());
    QVERIFY(stockOptions.newGeoFenceCircleInclusion());
    QCOMPARE(stockOptions.newGeoFenceCircleRadius(), 0.0);

    QVERIFY(!customOptions->showPlanInfo());
    QVERIFY(!customOptions->showPlanDefaults());
    QVERIFY(!customOptions->showInitialCameraSettings());
    QVERIFY(!customOptions->showRallyPoints());
    QVERIFY(!customOptions->showTransform());
    QVERIFY(!customOptions->showRallyLayer());
    QVERIFY(!customOptions->newGeoFenceCircleInclusion());
    QCOMPARE(customOptions->newGeoFenceCircleRadius(), 3.0);
    QCOMPARE(customOptions->geoFencePolygonBorderColor(), QColorConstants::Blue);

    const QGeoCoordinate center(47.397742, 8.545594);
    QGCFenceCircle* const customCircle = geoFenceController()->addCircle(
        center, customOptions->newGeoFenceCircleRadius(), customOptions->newGeoFenceCircleInclusion());
    QVERIFY(customCircle);
    QCOMPARE(customCircle->center(), center);
    QCOMPARE(customCircle->radius()->rawValue().toDouble(), 3.0);
    QVERIFY(!customCircle->inclusion());

    geoFenceController()->addInclusionCircle(center.atDistanceAndAzimuth(50.0, -45.0),
                                              center.atDistanceAndAzimuth(50.0, 135.0));
    QGCFenceCircle* const stockCircle =
        geoFenceController()->circles()->value<QGCFenceCircle*>(geoFenceController()->circles()->count() - 1);
    QVERIFY(stockCircle);
    QVERIFY(stockCircle->inclusion());
    QVERIFY(stockCircle->radius()->rawValue().toDouble() > customOptions->newGeoFenceCircleRadius());
}

UT_REGISTER_TEST(AgriculturalSprayPluginTest, TestLabel::Unit, TestLabel::MissionManager)
