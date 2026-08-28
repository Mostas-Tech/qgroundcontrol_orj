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
    created->setDirectionVertexIndex(2);
    QCOMPARE_TRUE_WAIT(created->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    QCOMPARE(created->directionVertexIndex(), 2);
    QCOMPARE(created->sourcePolygonCoordinates().count(), 4);
    const QGeoCoordinate createdEntry = created->routeCoordinates().front();
    const double createdSelectedCornerDistance = createdEntry.distanceTo(created->sourcePolygonCoordinates().at(2));
    QVERIFY(createdSelectedCornerDistance >= created->boundaryMargin()->rawValue().toDouble());
    for (int index = 0; index < created->sourcePolygonCoordinates().count(); ++index) {
        if (index != 2) {
            QVERIFY(createdSelectedCornerDistance <
                    createdEntry.distanceTo(created->sourcePolygonCoordinates().at(index)));
        }
    }

    const QJsonObject mission =
        planController()->saveToJson().object().value(PlanMasterController::kJsonMissionObjectKey).toObject();
    const QJsonArray items = mission.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.count(), 1);
    int sourcePolygonIndex = -1;
    for (int index = 0; index < polygons->count(); ++index) {
        if (polygons->value<QGCFencePolygon*>(index) == sourcePolygon) {
            sourcePolygonIndex = index;
            break;
        }
    }
    QVERIFY(sourcePolygonIndex >= 0);
    QVERIFY(items.at(0).toObject().value(QStringLiteral("sourcePolygonIndex")).toInt() >= 0);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("version")).toInt(), 7);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("directionVertexIndex")).toInt(), 2);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("BoundaryMargin")).toDouble(), 1.0);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("BoundaryMarginScope")).toInt(), 1);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("marginEdgeIndex")).toInt(), 0);
    const QJsonArray savedMarginEdges = items.at(0).toObject().value(QStringLiteral("marginEdgeIndices")).toArray();
    QCOMPARE(savedMarginEdges.size(), 1);
    QCOMPARE(savedMarginEdges.at(0).toInt(), 0);
    const QJsonArray savedMarginValues = items.at(0).toObject().value(QStringLiteral("marginEdgeMargins")).toArray();
    QCOMPARE(savedMarginValues.size(), 1);
    QCOMPARE(savedMarginValues.at(0).toDouble(), 1.0);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("exclusionMargins")).toArray().size(), 0);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("nonSprayPolygons")).toArray().size(), 0);
    QVERIFY(!items.at(0).toObject().contains(QStringLiteral("GridAngle")));
    QVERIFY(!items.at(0).toObject().contains(QStringLiteral("EntryCorner")));

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
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    QVERIFY(!loaded->routeCoordinates().isEmpty());
    QVERIFY(loaded->complexDistance() > 0.0);
    QCOMPARE(loaded->directionVertexIndex(), 2);
    QCOMPARE(loaded->sourcePolygonCoordinates().count(), 4);
    const QGeoCoordinate loadedEntry = loaded->routeCoordinates().front();
    const double loadedSelectedCornerDistance = loadedEntry.distanceTo(loaded->sourcePolygonCoordinates().at(2));
    QVERIFY(loadedSelectedCornerDistance >= loaded->boundaryMargin()->rawValue().toDouble());
    for (int index = 0; index < loaded->sourcePolygonCoordinates().count(); ++index) {
        if (index != 2) {
            QVERIFY(loadedSelectedCornerDistance <
                    loadedEntry.distanceTo(loaded->sourcePolygonCoordinates().at(index)));
        }
    }

    QJsonObject invalidItem = items.at(0).toObject();
    invalidItem[QStringLiteral("directionVertexIndex")] = 99;
    QString invalidDirectionError;
    QVERIFY(loaded->load(invalidItem, loaded->sequenceNumber(), invalidDirectionError));
    QVERIFY(invalidDirectionError.isEmpty());
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::InvalidArea, TestTimeout::mediumMs());
    QCOMPARE(loaded->directionVertexIndex(), 99);
    QVERIFY(loaded->errorText().contains(QStringLiteral("outside")));
    QVERIFY(loaded->routeCoordinates().isEmpty());

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*out of range")));
    planController()->loadFromFile(emptyFencePlanPath);
    loaded = sprayItem(missionController());
    QVERIFY(loaded);
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::GenerationError, TestTimeout::mediumMs());
    QVERIFY(loaded->errorText().contains(QStringLiteral("out of range")));
    QVERIFY(loaded->routeCoordinates().isEmpty());
    QCOMPARE(loaded->complexDistance(), 0.0);
    verifyExpectedLogMessage();
}

UT_REGISTER_TEST(AgriculturalSprayPlanLoadTest, TestLabel::Unit, TestLabel::MissionManager)
