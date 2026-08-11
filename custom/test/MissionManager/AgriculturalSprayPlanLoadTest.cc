#include "AgriculturalSprayPlanLoadTest.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryDir>

#include "AgriculturalSprayComplexItem.h"
#include "Fact.h"
#include "GeoFenceController.h"
#include "MissionController.h"
#include "PlanMasterController.h"
#include "QGCFencePolygon.h"
#include "QmlObjectListModel.h"

namespace {

QList<QGeoCoordinate> square(const QGeoCoordinate& center)
{
    return {
        center.atDistanceAndAzimuth(50.0, -45.0),
        center.atDistanceAndAzimuth(50.0, 45.0),
        center.atDistanceAndAzimuth(50.0, 135.0),
        center.atDistanceAndAzimuth(50.0, -135.0),
    };
}

AgriculturalSprayComplexItem* sprayItem(MissionController* missionController)
{
    for (int index = 1; index < missionController->visualItems()->count(); ++index) {
        if (AgriculturalSprayComplexItem* const item =
                missionController->visualItems()->value<AgriculturalSprayComplexItem*>(index)) {
            return item;
        }
    }
    return nullptr;
}

bool writePlan(const QString& path, const QJsonObject& plan)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(plan).toJson()) != -1;
}

}  // namespace

void AgriculturalSprayPlanLoadTest::_missionLoadedBeforeFenceBuildsFinalRouteAndClearsStaleRoute()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    QmlObjectListModel* const polygons = geoFenceController()->polygons();
    geoFenceController()->addInclusionPolygon(center.atDistanceAndAzimuth(50.0, -45.0),
                                              center.atDistanceAndAzimuth(50.0, 135.0));
    QGCFencePolygon* const polygon = polygons->value<QGCFencePolygon*>(0);
    QVERIFY(polygon);
    polygon->setPath(square(center));

    AgriculturalSprayComplexItem* const created = qobject_cast<AgriculturalSprayComplexItem*>(
        missionController()->insertComplexMissionItem(AgriculturalSprayComplexItem::canonicalName, center, -1, false));
    QVERIFY(created);
    QCOMPARE_TRUE_WAIT(created->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    const QList<QGeoCoordinate> expectedRoute = created->routeCoordinates();

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString validPlanPath = temporaryDirectory.filePath(QStringLiteral("with-fence.plan"));
    const QString emptyFencePlanPath = temporaryDirectory.filePath(QStringLiteral("without-fence.plan"));
    const QJsonObject validPlan = planController()->saveToJson().object();
    QVERIFY(writePlan(validPlanPath, validPlan));

    QJsonObject emptyFencePlan = validPlan;
    QJsonObject emptyFence = emptyFencePlan.value(PlanMasterController::kJsonGeoFenceObjectKey).toObject();
    emptyFence[QStringLiteral("polygons")] = QJsonArray();
    emptyFence[QStringLiteral("circles")] = QJsonArray();
    emptyFencePlan[PlanMasterController::kJsonGeoFenceObjectKey] = emptyFence;
    QVERIFY(writePlan(emptyFencePlanPath, emptyFencePlan));

    planController()->loadFromFile(validPlanPath);
    AgriculturalSprayComplexItem* loaded = sprayItem(missionController());
    QVERIFY(loaded);
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    QCOMPARE(loaded->routeCoordinates(), expectedRoute);

    planController()->loadFromFile(emptyFencePlanPath);
    loaded = sprayItem(missionController());
    QVERIFY(loaded);
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::NoInclusion, TestTimeout::shortMs());
    QVERIFY(loaded->routeCoordinates().isEmpty());
    QCOMPARE(loaded->complexDistance(), 0.0);
}

void AgriculturalSprayPlanLoadTest::_duplicateSprayLoadIsRejectedAtomically()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    geoFenceController()->addInclusionPolygon(center.atDistanceAndAzimuth(50.0, -45.0),
                                              center.atDistanceAndAzimuth(50.0, 135.0));
    QGCFencePolygon* const polygon = geoFenceController()->polygons()->value<QGCFencePolygon*>(0);
    QVERIFY(polygon);
    polygon->setPath(square(center));

    AgriculturalSprayComplexItem* const liveSpray = qobject_cast<AgriculturalSprayComplexItem*>(
        missionController()->insertComplexMissionItem(AgriculturalSprayComplexItem::canonicalName, center, -1, false));
    QVERIFY(liveSpray);
    liveSpray->altitude()->setRawValue(62.0);
    QCOMPARE_TRUE_WAIT(liveSpray->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());

    const int liveItemCount = missionController()->visualItems()->count();
    const QGeoCoordinate liveCoordinate = liveSpray->coordinate();
    const double liveAltitude = liveSpray->altitude()->rawValue().toDouble();
    const QJsonDocument livePlan = planController()->saveToJson();
    const QPointer<AgriculturalSprayComplexItem> liveSprayGuard(liveSpray);
    const QList<AgriculturalSprayComplexItem*> liveSprayObjects =
        planController()->findChildren<AgriculturalSprayComplexItem*>();
    QCOMPARE(liveItemCount, 2);
    QVERIFY(liveCoordinate.isValid());
    QCOMPARE(liveSprayObjects.count(), 1);
    QCOMPARE(liveSprayObjects.constFirst(), liveSpray);

    QJsonObject duplicatePlan = livePlan.object();
    QJsonObject mission = duplicatePlan.value(PlanMasterController::kJsonMissionObjectKey).toObject();
    const QJsonArray liveItems = mission.value(QStringLiteral("items")).toArray();
    QCOMPARE(liveItems.count(), 1);

    QJsonObject firstDuplicate = liveItems.at(0).toObject();
    QCOMPARE(firstDuplicate.value(QStringLiteral("complexItemType")).toString(),
             QString::fromLatin1(AgriculturalSprayComplexItem::jsonComplexItemTypeValue));
    firstDuplicate[QString::fromLatin1(AgriculturalSprayComplexItem::altitudeName)] = 75.0;
    QJsonObject secondDuplicate = firstDuplicate;
    secondDuplicate[QString::fromLatin1(AgriculturalSprayComplexItem::altitudeName)] = 85.0;
    mission[QStringLiteral("items")] = QJsonArray({firstDuplicate, secondDuplicate});
    duplicatePlan[PlanMasterController::kJsonMissionObjectKey] = mission;

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString duplicatePlanPath = temporaryDirectory.filePath(QStringLiteral("duplicate-spray.plan"));
    QVERIFY(writePlan(duplicatePlanPath, duplicatePlan));

    constexpr int rejectedLoadAttempts = 3;
    for (int attempt = 0; attempt < rejectedLoadAttempts; ++attempt) {
        expectLogMessage(
            "PlanManager.MissionController", QtWarningMsg,
            QRegularExpression(QStringLiteral("Complex mission item load denied.*Only one Agricultural Spray")));
        expectLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                         QRegularExpression(QStringLiteral("Error loading Plan file.*Only one Agricultural Spray")));
        planController()->loadFromFile(duplicatePlanPath);
        verifyExpectedLogMessage();
        verifyExpectedLogMessage();

        QVERIFY(liveSprayGuard);
        const QList<AgriculturalSprayComplexItem*> remainingSprays =
            planController()->findChildren<AgriculturalSprayComplexItem*>();
        QCOMPARE(remainingSprays.count(), liveSprayObjects.count());
        QCOMPARE(remainingSprays.constFirst(), liveSprayGuard.data());
        QCOMPARE(missionController()->visualItems()->count(), liveItemCount);
        QCOMPARE(planController()->saveToJson(), livePlan);
    }

    QCOMPARE(liveSprayGuard->coordinate(), liveCoordinate);
    QCOMPARE(liveSprayGuard->altitude()->rawValue().toDouble(), liveAltitude);
}

UT_REGISTER_TEST(AgriculturalSprayPlanLoadTest, TestLabel::Unit, TestLabel::MissionManager)
