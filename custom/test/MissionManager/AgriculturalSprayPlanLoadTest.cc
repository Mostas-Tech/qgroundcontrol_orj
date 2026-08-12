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
    QGCFencePolygon* const sourcePolygon = polygons->value<QGCFencePolygon*>(polygons->count() - 1);
    QVERIFY(sourcePolygon);
    QVERIFY(sourcePolygon->traceMode());
    sourcePolygon->setPath(square(center));
    sourcePolygon->setTraceMode(false);
    QCOMPARE_TRUE_WAIT(created->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    QVERIFY(!created->routeCoordinates().isEmpty());

    const QJsonObject mission = planController()->saveToJson().object()
                                    .value(PlanMasterController::kJsonMissionObjectKey)
                                    .toObject();
    const QJsonArray items = mission.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.count(), 1);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("sourcePolygonIndex")).toInt(), 1);

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
    QVERIFY(!loaded->routeCoordinates().isEmpty());
    QVERIFY(loaded->complexDistance() > 0.0);

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*out of range")));
    planController()->loadFromFile(emptyFencePlanPath);
    loaded = sprayItem(missionController());
    QVERIFY(loaded);
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::GenerationError, TestTimeout::shortMs());
    QVERIFY(loaded->errorText().contains(QStringLiteral("out of range")));
    QVERIFY(loaded->routeCoordinates().isEmpty());
    QCOMPARE(loaded->complexDistance(), 0.0);
    verifyExpectedLogMessage();
}

UT_REGISTER_TEST(AgriculturalSprayPlanLoadTest, TestLabel::Unit, TestLabel::MissionManager)
