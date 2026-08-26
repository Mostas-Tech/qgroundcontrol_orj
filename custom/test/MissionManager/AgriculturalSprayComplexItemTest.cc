#include "AgriculturalSprayComplexItemTest.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <limits>

#include "AgriculturalSprayComplexItem.h"
#include "Fact.h"
#include "FactMetaData.h"
#include "GeoFenceController.h"
#include "MissionController.h"
#include "MissionItem.h"
#include "QGCFenceCircle.h"
#include "QGCFencePolygon.h"
#include "QGCMapPolygon.h"
#include "QmlObjectListModel.h"
#include "SimpleMissionItem.h"

namespace {

QList<QGeoCoordinate> square(const QGeoCoordinate& center, double sideMeters)
{
    const QGeoCoordinate northWest = center.atDistanceAndAzimuth(sideMeters / 2.0, -45.0);
    const QGeoCoordinate northEast = center.atDistanceAndAzimuth(sideMeters / 2.0, 45.0);
    const QGeoCoordinate southEast = center.atDistanceAndAzimuth(sideMeters / 2.0, 135.0);
    const QGeoCoordinate southWest = center.atDistanceAndAzimuth(sideMeters / 2.0, -135.0);
    return {northWest, northEast, southEast, southWest};
}

void verifyFact(Fact* fact, double value, double minimum, double maximum, const QString& units)
{
    QVERIFY(fact);
    QVERIFY(fact->metaData());
    QCOMPARE(fact->rawValue().toDouble(), value);
    QCOMPARE(fact->metaData()->rawMin().toDouble(), minimum);
    QCOMPARE(fact->metaData()->rawMax().toDouble(), maximum);
    QCOMPARE(fact->rawUnits(), units);
}

QJsonObject sprayJson(int dropletClass = AgriculturalSprayComplexItem::Coarse)
{
    return {
        {QStringLiteral("version"), 1},
        {QStringLiteral("type"), QStringLiteral("ComplexItem")},
        {QStringLiteral("complexItemType"), QStringLiteral("AgriculturalSpray")},
        {QStringLiteral("Altitude"), 60.0},
        {QStringLiteral("LineSpacing"), 12.5},
        {QStringLiteral("GridAngle"), -15.0},
        {QStringLiteral("EntryCorner"), static_cast<int>(AgriculturalSprayComplexItem::BottomLeft)},
        {QStringLiteral("DropletClass"), dropletClass},
        {QStringLiteral("ApplicationRate"), 2.5},
    };
}

}  // namespace

void AgriculturalSprayComplexItemTest::init()
{
    OfflineMissionTest::init();
    _item = new AgriculturalSprayComplexItem(planController(), false);
}

void AgriculturalSprayComplexItemTest::cleanup()
{
    if (missionController()->visualItems()->indexOf(_item) >= 0) {
        missionController()->visualItems()->removeOne(_item);
    }
    delete _item;
    _item = nullptr;
    OfflineMissionTest::cleanup();
}

void AgriculturalSprayComplexItemTest::directionVertexDefaultsToZero()
{
    QCOMPARE(_item->directionVertexIndex(), 0);
    QCOMPARE(_item->marginEdgeIndex(), 0);
    QCOMPARE(_item->boundaryMargin()->rawValue().toDouble(), 1.0);
    QCOMPARE(_item->boundaryMarginScope()->rawValue().toUInt(),
             static_cast<uint>(AgriculturalSprayComplexItem::AllEdges));
}

void AgriculturalSprayComplexItemTest::marginEdgeSelectionIsIndependentAndNormalizes()
{
    _makeReadyWithSquare();
    _item->setDirectionVertexIndex(1);
    _item->setMarginEdgeIndex(3);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    QCOMPARE(_item->directionVertexIndex(), 1);
    QCOMPARE(_item->marginEdgeIndex(), 3);
    QCOMPARE(_item->marginEdgeIndices(), QVariantList{QVariant(3)});
    QCOMPARE(_item->marginEdgeStart(), _item->sourcePolygonCoordinates().at(3));

    _item->toggleMarginEdgeIndex(1);
    QCOMPARE(_item->marginEdgeIndices(), QVariantList({QVariant(1), QVariant(3)}));
    _item->toggleMarginEdgeIndex(1);
    QCOMPARE(_item->marginEdgeIndices(), QVariantList{QVariant(3)});

    QGCFencePolygon* const polygon = geoFenceController()->polygons()->value<QGCFencePolygon*>(0);
    QVERIFY(polygon);
    polygon->removeVertex(3);
    QCOMPARE(_item->directionVertexIndex(), 1);
    QCOMPARE(_item->marginEdgeIndex(), 0);
}

void AgriculturalSprayComplexItemTest::boundaryMarginJsonRoundTripAndMigration()
{
    _makeReadyWithSquare();
    _item->boundaryMargin()->setRawValue(2.25);
    _item->boundaryMarginScope()->setRawValue(AgriculturalSprayComplexItem::AllEdges);
    _item->setMarginEdgeIndex(2);
    _item->toggleMarginEdgeIndex(3);
    _item->setFieldMargin(2, 3.0);
    _item->setFieldMargin(3, 1.0);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.first().toObject();
    QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 7);
    QCOMPARE(saved.value(QStringLiteral("BoundaryMargin")).toDouble(), 2.25);
    QCOMPARE(saved.value(QStringLiteral("BoundaryMarginScope")).toInt(),
             static_cast<int>(AgriculturalSprayComplexItem::AllEdges));
    QCOMPARE(saved.value(QStringLiteral("marginEdgeIndex")).toInt(), 3);
    const QJsonArray savedMarginEdges = saved.value(QStringLiteral("marginEdgeIndices")).toArray();
    QCOMPARE(savedMarginEdges.size(), 2);
    QCOMPARE(savedMarginEdges.at(0).toInt(), 2);
    QCOMPARE(savedMarginEdges.at(1).toInt(), 3);
    const QJsonArray savedEdgeMargins = saved.value(QStringLiteral("marginEdgeMargins")).toArray();
    QCOMPARE(savedEdgeMargins.size(), 2);
    QCOMPARE(savedEdgeMargins.at(0).toDouble(), 3.0);
    QCOMPARE(savedEdgeMargins.at(1).toDouble(), 1.0);
    QCOMPARE(saved.value(QStringLiteral("exclusionMargins")).toArray().size(), 0);

    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(loaded.load(saved, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE(loaded.boundaryMargin()->rawValue().toDouble(), 2.25);
    QCOMPARE(loaded.boundaryMarginScope()->rawValue().toUInt(),
             static_cast<uint>(AgriculturalSprayComplexItem::AllEdges));
    QCOMPARE(loaded.marginEdgeIndex(), 3);
    QCOMPARE(loaded.marginEdgeIndices(), QVariantList({QVariant(2), QVariant(3)}));
    QCOMPARE(loaded.fieldMarginRows().at(0).toMap().value(QStringLiteral("margin")).toDouble(), 3.0);
    QCOMPARE(loaded.fieldMarginRows().at(1).toMap().value(QStringLiteral("margin")).toDouble(), 1.0);

    QJsonObject versionFive = saved;
    versionFive[QStringLiteral("version")] = 5;
    versionFive.remove(QStringLiteral("marginEdgeMargins"));
    versionFive.remove(QStringLiteral("nonSprayPolygons"));
    AgriculturalSprayComplexItem migratedVersionFive(planController(), false);
    QVERIFY(migratedVersionFive.load(versionFive, 8, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE(migratedVersionFive.fieldMarginRows().at(0).toMap().value(QStringLiteral("margin")).toDouble(), 2.25);
    QCOMPARE(migratedVersionFive.fieldMarginRows().at(1).toMap().value(QStringLiteral("margin")).toDouble(), 2.25);

    QJsonObject versionFour = saved;
    versionFour[QStringLiteral("version")] = 4;
    versionFour.remove(QStringLiteral("marginEdgeIndices"));
    versionFour.remove(QStringLiteral("marginEdgeMargins"));
    versionFour.remove(QStringLiteral("nonSprayPolygons"));
    AgriculturalSprayComplexItem migratedVersionFour(planController(), false);
    QVERIFY(migratedVersionFour.load(versionFour, 8, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE(migratedVersionFour.marginEdgeIndices(), QVariantList{QVariant(3)});

    QJsonObject versionTwo = saved;
    versionTwo[QStringLiteral("version")] = 2;
    versionTwo.remove(QStringLiteral("BoundaryMargin"));
    versionTwo.remove(QStringLiteral("BoundaryMarginScope"));
    versionTwo.remove(QStringLiteral("marginEdgeIndex"));
    versionTwo.remove(QStringLiteral("marginEdgeIndices"));
    versionTwo.remove(QStringLiteral("marginEdgeMargins"));
    versionTwo.remove(QStringLiteral("exclusionMargins"));
    versionTwo.remove(QStringLiteral("nonSprayPolygons"));
    AgriculturalSprayComplexItem migrated(planController(), false);
    QVERIFY(migrated.load(versionTwo, 8, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE(migrated.boundaryMargin()->rawValue().toDouble(), 0.0);
    QCOMPARE(migrated.boundaryMarginScope()->rawValue().toUInt(),
             static_cast<uint>(AgriculturalSprayComplexItem::SelectedEdge));
    QCOMPARE(migrated.marginEdgeIndex(), 0);
}

void AgriculturalSprayComplexItemTest::exclusionMarginJsonRoundTripAndMigration()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    _makeReadyWithSquare();
    QGCFencePolygon* const polygon = _addPolygon(false);
    QVERIFY(polygon);
    polygon->setPath(square(center.atDistanceAndAzimuth(20.0, 90.0), 12.0));
    QGCFenceCircle* const circle = _addCircle(false);
    QVERIFY(circle);
    circle->setCenter(center.atDistanceAndAzimuth(18.0, 270.0));
    circle->radius()->setRawValue(4.0);

    const QVariantList defaultRows = _item->exclusionMarginRows();
    QCOMPARE(defaultRows.size(), 2);
    QCOMPARE(defaultRows.at(0).toMap().value(QStringLiteral("margin")).toDouble(), 1.0);
    QCOMPARE(defaultRows.at(1).toMap().value(QStringLiteral("margin")).toDouble(), 1.0);

    _item->setExclusionMargin(polygon, 2.5);
    _item->setExclusionMargin(circle, 0.0);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    QCOMPARE(_item->exclusionMarginRows().size(), 2);

    geoFenceController()->polygons()->move(1, 0);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.first().toObject();
    QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 7);
    const QJsonArray margins = saved.value(QStringLiteral("exclusionMargins")).toArray();
    QCOMPARE(margins.size(), 2);
    QCOMPARE(margins.at(0).toObject().value(QStringLiteral("shapeType")).toString(), QStringLiteral("polygon"));
    QCOMPARE(margins.at(0).toObject().value(QStringLiteral("shapeIndex")).toInt(), 0);
    QCOMPARE(margins.at(0).toObject().value(QStringLiteral("margin")).toDouble(), 2.5);
    QCOMPARE(margins.at(1).toObject().value(QStringLiteral("shapeType")).toString(), QStringLiteral("circle"));
    QCOMPARE(margins.at(1).toObject().value(QStringLiteral("shapeIndex")).toInt(), 0);
    QCOMPARE(margins.at(1).toObject().value(QStringLiteral("margin")).toDouble(), 0.0);

    QJsonObject invalidReference = saved;
    QJsonArray invalidMargins = margins;
    QJsonObject changedPolygonMargin = invalidMargins.at(0).toObject();
    changedPolygonMargin[QStringLiteral("margin")] = 9.0;
    invalidMargins[0] = changedPolygonMargin;
    QJsonObject invalidCircleMargin = invalidMargins.at(1).toObject();
    invalidCircleMargin[QStringLiteral("shapeIndex")] = 99;
    invalidMargins[1] = invalidCircleMargin;
    invalidReference[QStringLiteral("exclusionMargins")] = invalidMargins;
    QString errorString;
    QVERIFY(_item->load(invalidReference, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::GenerationError, TestTimeout::shortMs());
    const QVariantList retainedRows = _item->exclusionMarginRows();
    QCOMPARE(retainedRows.size(), 2);
    QCOMPARE(retainedRows.at(0).toMap().value(QStringLiteral("margin")).toDouble(), 2.5);
    QCOMPARE(retainedRows.at(1).toMap().value(QStringLiteral("margin")).toDouble(), 0.0);

    AgriculturalSprayComplexItem* const loaded = new AgriculturalSprayComplexItem(planController(), false);
    missionController()->visualItems()->append(loaded);
    const auto loadedGuard = qScopeGuard([this, loaded]() {
        missionController()->visualItems()->removeOne(loaded);
        delete loaded;
    });
    QVERIFY(loaded->load(saved, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(loaded->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    const QVariantList loadedRows = loaded->exclusionMarginRows();
    QCOMPARE(loadedRows.size(), 2);
    QCOMPARE(loadedRows.at(0).toMap().value(QStringLiteral("margin")).toDouble(), 2.5);
    QCOMPARE(loadedRows.at(1).toMap().value(QStringLiteral("margin")).toDouble(), 0.0);

    QJsonObject versionThree = saved;
    versionThree[QStringLiteral("version")] = 3;
    versionThree.remove(QStringLiteral("exclusionMargins"));
    versionThree.remove(QStringLiteral("marginEdgeIndices"));
    versionThree.remove(QStringLiteral("marginEdgeMargins"));
    versionThree.remove(QStringLiteral("nonSprayPolygons"));
    AgriculturalSprayComplexItem* const migrated = new AgriculturalSprayComplexItem(planController(), false);
    missionController()->visualItems()->append(migrated);
    const auto migratedGuard = qScopeGuard([this, migrated]() {
        missionController()->visualItems()->removeOne(migrated);
        delete migrated;
    });
    QVERIFY(migrated->load(versionThree, 8, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(migrated->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    for (const QVariant& row : migrated->exclusionMarginRows()) {
        QCOMPARE(row.toMap().value(QStringLiteral("margin")).toDouble(), 0.0);
    }
}

void AgriculturalSprayComplexItemTest::exclusionCircleMarginOverflowFailsClosed()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    _makeReadyWithSquare();
    QGCFenceCircle* const circle = _addCircle(false);
    QVERIFY(circle);
    circle->setCenter(center);
    circle->radius()->setRawValue(std::numeric_limits<double>::max());

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*radius or margin is invalid")));
    _item->setExclusionMargin(circle, std::numeric_limits<double>::max());
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::GenerationError, TestTimeout::shortMs());
    QVERIFY(_item->routeCoordinates().isEmpty());
    verifyExpectedLogMessage();
}

void AgriculturalSprayComplexItemTest::exclusionMarginOutsideFenceChangesRoute()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    _makeReadyWithSquare();
    const QList<QGeoCoordinate> initialRoute = _item->routeCoordinates();

    QGCFenceCircle* const outsideCircle = _addCircle(false);
    QVERIFY(outsideCircle);
    outsideCircle->setCenter(center.atDistanceAndAzimuth(41.0, 0.0));
    outsideCircle->radius()->setRawValue(3.0);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    QCOMPARE(_item->routeCoordinates(), initialRoute);

    _item->setExclusionMargin(outsideCircle, 15.0);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != initialRoute,
        TestTimeout::mediumMs());
}

void AgriculturalSprayComplexItemTest::boundaryMarginJsonIsStrict()
{
    _makeReadyWithSquare();
    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.first().toObject();

    const auto verifyRejected = [this](QJsonObject object) {
        expectLogMessage(
            "qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
            QRegularExpression(QStringLiteral("(JSON validation failed|Boundary margin validation failed)")));
        AgriculturalSprayComplexItem loaded(planController(), false);
        QString errorString;
        QVERIFY(!loaded.load(object, 7, errorString));
        QVERIFY(!errorString.isEmpty());
        verifyExpectedLogMessage();
    };

    QJsonObject missingMargin = saved;
    missingMargin.remove(QStringLiteral("BoundaryMargin"));
    verifyRejected(missingMargin);

    QJsonObject fractionalScope = saved;
    fractionalScope[QStringLiteral("BoundaryMarginScope")] = 0.5;
    verifyRejected(fractionalScope);
}

void AgriculturalSprayComplexItemTest::rapidDirectionChangesPublishLatestRoute()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    AgriculturalSprayComplexItem* const item = qobject_cast<AgriculturalSprayComplexItem*>(
        missionController()->insertComplexMissionItem(AgriculturalSprayComplexItem::canonicalName, center, -1, false));
    QVERIFY(item);
    QGCFencePolygon* const polygon =
        geoFenceController()->polygons()->value<QGCFencePolygon*>(geoFenceController()->polygons()->count() - 1);
    QVERIFY(polygon);
    polygon->setPath(square(center, 160.0));
    polygon->setTraceMode(false);
    QCOMPARE_TRUE_WAIT(item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());

    item->setDirectionVertexIndex(1);
    item->setDirectionVertexIndex(3);
    item->setDirectionVertexIndex(2);
    QCOMPARE_TRUE_WAIT(item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());
    QCOMPARE(item->directionVertexIndex(), 2);
    QVERIFY(!item->routeCoordinates().isEmpty());
    QCOMPARE(item->sourcePolygonCoordinates().count(), 4);
    const QGeoCoordinate routeEntry = item->routeCoordinates().front();
    const double selectedCornerDistance = routeEntry.distanceTo(item->sourcePolygonCoordinates().at(2));
    QVERIFY(selectedCornerDistance >= item->boundaryMargin()->rawValue().toDouble());
    for (int index = 0; index < item->sourcePolygonCoordinates().count(); ++index) {
        if (index != 2) {
            QVERIFY(selectedCornerDistance < routeEntry.distanceTo(item->sourcePolygonCoordinates().at(index)));
        }
    }
}

void AgriculturalSprayComplexItemTest::nonSprayAreasSplitSprayStateWithoutRerouting()
{
    _makeReadyWithSquare();
    _item->setSequenceNumber(20);
    const QList<QGeoCoordinate> originalRoute = _item->routeCoordinates();
    const double originalDistance = _item->complexDistance();

    int spraySegmentIndex = -1;
    for (int index = 0; index < _item->routeSegmentTypes().size(); ++index) {
        if (_item->routeSegmentTypes()[index] == AgriculturalSprayComplexItem::SprayLeg &&
            originalRoute[index].distanceTo(originalRoute[index + 1]) > 20.0) {
            spraySegmentIndex = index;
            break;
        }
    }
    QVERIFY(spraySegmentIndex >= 0);
    const QGeoCoordinate start = originalRoute[spraySegmentIndex];
    const QGeoCoordinate end = originalRoute[spraySegmentIndex + 1];
    const QGeoCoordinate midpoint = start.atDistanceAndAzimuth(start.distanceTo(end) / 2.0, start.azimuthTo(end));

    QGCMapPolygon* const polygon = qobject_cast<QGCMapPolygon*>(_item->addNonSprayPolygon());
    QVERIFY(polygon);
    polygon->setPath(square(midpoint, 4.0));
    polygon->setTraceMode(false);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());

    QCOMPARE(_item->entryCoordinate(), originalRoute.front());
    QCOMPARE(_item->exitCoordinate(), originalRoute.back());
    QCOMPARE(_item->complexDistance(), originalDistance);
    QVERIFY(_item->routeCoordinates().size() > originalRoute.size());
    bool foundSplit = false;
    for (int index = 1; index + 1 < _item->routeSegmentTypes().size(); ++index) {
        if (_item->routeSegmentTypes()[index - 1] == AgriculturalSprayComplexItem::SprayLeg &&
            _item->routeSegmentTypes()[index] == AgriculturalSprayComplexItem::NonSpray &&
            _item->routeSegmentTypes()[index + 1] == AgriculturalSprayComplexItem::SprayLeg) {
            foundSplit = true;
            break;
        }
    }
    QVERIFY(foundSplit);

    QList<MissionItem*> missionItems;
    _item->appendMissionItems(missionItems, this);
    QCOMPARE(_item->lastSequenceNumber(), 20 + missionItems.size() - 1);
    QList<int> relayStates;
    for (int index = 0; index < missionItems.size(); ++index) {
        QCOMPARE(missionItems[index]->sequenceNumber(), 20 + index);
        if (static_cast<MAV_CMD>(missionItems[index]->command()) == MAV_CMD_DO_SET_RELAY) {
            QCOMPARE(missionItems[index]->param1(), 0.0);
            relayStates.append(static_cast<int>(missionItems[index]->param2()));
        }
    }
    bool foundPauseAndResume = false;
    for (int index = 1; index + 1 < relayStates.size(); ++index) {
        if (relayStates[index - 1] == 1 && relayStates[index] == 0 && relayStates[index + 1] == 1) {
            foundPauseAndResume = true;
            break;
        }
    }
    QVERIFY(foundPauseAndResume);
    QCOMPARE(relayStates.back(), 0);
}

void AgriculturalSprayComplexItemTest::nonSprayJsonRoundTripIsStrictAndTransactional()
{
    _makeReadyWithSquare();
    QGCMapPolygon* const polygon = qobject_cast<QGCMapPolygon*>(_item->addNonSprayPolygon());
    QVERIFY(polygon);
    polygon->setPath(square(QGeoCoordinate(47.397742, 8.545594), 8.0));
    polygon->setTraceMode(false);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::mediumMs());

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.first().toObject();
    QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 7);
    QCOMPARE(saved.value(QStringLiteral("nonSprayPolygons")).toArray().size(), 1);

    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(loaded.load(saved, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE(loaded.nonSprayPolygons()->count(), 1);
    const QGCMapPolygon* const loadedPolygon = loaded.nonSprayPolygons()->value<QGCMapPolygon*>(0);
    QVERIFY(loadedPolygon);
    QCOMPARE(loadedPolygon->coordinateList(), polygon->coordinateList());

    QJsonObject versionSix = saved;
    versionSix[QStringLiteral("version")] = 6;
    versionSix.remove(QStringLiteral("nonSprayPolygons"));
    AgriculturalSprayComplexItem migrated(planController(), false);
    QVERIFY(migrated.load(versionSix, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE(migrated.nonSprayPolygons()->count(), 0);

    QJsonObject invalid = saved;
    QJsonArray invalidPolygons = invalid.value(QStringLiteral("nonSprayPolygons")).toArray();
    QJsonObject invalidPolygon = invalidPolygons.first().toObject();
    invalidPolygon[QStringLiteral("unexpected")] = true;
    invalidPolygons[0] = invalidPolygon;
    invalid[QStringLiteral("nonSprayPolygons")] = invalidPolygons;
    QGCMapPolygon* const retainedPolygon = _item->nonSprayPolygons()->value<QGCMapPolygon*>(0);
    QVERIFY(!_item->load(invalid, 20, errorString));
    QVERIFY(!errorString.isEmpty());
    QCOMPARE(_item->nonSprayPolygons()->count(), 1);
    QCOMPARE(_item->nonSprayPolygons()->value<QGCMapPolygon*>(0), retainedPolygon);
}

QGCFencePolygon* AgriculturalSprayComplexItemTest::_addPolygon(bool inclusion)
{
    const QGeoCoordinate center(47.397742, 8.545594);
    geoFenceController()->addInclusionPolygon(center.atDistanceAndAzimuth(50.0, -45.0),
                                              center.atDistanceAndAzimuth(50.0, 135.0));
    QmlObjectListModel* const polygons = geoFenceController()->polygons();
    QGCFencePolygon* const polygon = polygons->value<QGCFencePolygon*>(polygons->count() - 1);
    if (!polygon) {
        return nullptr;
    }
    polygon->setInclusion(inclusion);
    return polygon;
}

QGCFenceCircle* AgriculturalSprayComplexItemTest::_addCircle(bool inclusion)
{
    const QGeoCoordinate center(47.397742, 8.545594);
    geoFenceController()->addInclusionCircle(center.atDistanceAndAzimuth(50.0, -45.0),
                                             center.atDistanceAndAzimuth(50.0, 135.0));
    QmlObjectListModel* const circles = geoFenceController()->circles();
    QGCFenceCircle* const circle = circles->value<QGCFenceCircle*>(circles->count() - 1);
    if (!circle) {
        return nullptr;
    }
    circle->setInclusion(inclusion);
    return circle;
}

void AgriculturalSprayComplexItemTest::_waitForStatus(AgriculturalSprayComplexItem::Status status)
{
    QCOMPARE_TRUE_WAIT(_item->status(), status, TestTimeout::shortMs());
}

void AgriculturalSprayComplexItemTest::_makeReadyWithSquare()
{
    if (missionController()->visualItems()->indexOf(_item) < 0) {
        missionController()->visualItems()->append(_item);
    }
    _item->beginInteractiveCreation();
    QGCFencePolygon* const polygon =
        geoFenceController()->polygons()->value<QGCFencePolygon*>(geoFenceController()->polygons()->count() - 1);
    QVERIFY(polygon);
    QVERIFY(polygon->traceMode());
    polygon->setPath(square(QGeoCoordinate(47.397742, 8.545594), 100.0));
    polygon->setTraceMode(false);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
}

void AgriculturalSprayComplexItemTest::_factMetadataAndAreaStates()
{
    _waitForStatus(AgriculturalSprayComplexItem::NoInclusion);

    verifyFact(_item->altitude(), 50.0, 0.0, std::numeric_limits<double>::max(), QStringLiteral("vertical m"));
    QVERIFY(_item->altitude()->metaData()->maxIsDefaultForType());
    verifyFact(_item->lineSpacing(), 10.0, 0.1, std::numeric_limits<double>::max(), QStringLiteral("m"));
    QVERIFY(_item->lineSpacing()->metaData()->maxIsDefaultForType());
    verifyFact(_item->gridAngle(), 0.0, -360.0, 360.0, QStringLiteral("deg"));
    verifyFact(_item->boundaryMargin(), 1.0, 0.0, std::numeric_limits<double>::max(), QStringLiteral("m"));
    verifyFact(_item->applicationRate(), 1.0, 0.1, 100.0, QStringLiteral("L/da"));

    Fact* const boundaryMarginScope = _item->boundaryMarginScope();
    QVERIFY(boundaryMarginScope->metaData());
    QCOMPARE(boundaryMarginScope->rawValue().toUInt(), static_cast<uint>(AgriculturalSprayComplexItem::AllEdges));
    QCOMPARE(boundaryMarginScope->enumStrings(),
             QStringList({QStringLiteral("Selected Edge"), QStringLiteral("All Edges")}));
    QCOMPARE(boundaryMarginScope->enumValues(), QVariantList({0, 1}));

    Fact* const entryCorner = _item->entryCorner();
    QVERIFY(entryCorner->metaData());
    QCOMPARE(entryCorner->rawValue().toUInt(), static_cast<uint>(AgriculturalSprayComplexItem::TopLeft));
    QCOMPARE(entryCorner->metaData()->rawMin().toUInt(), 0U);
    QCOMPARE(entryCorner->metaData()->rawMax().toUInt(), 3U);
    QVERIFY(entryCorner->rawUnits().isEmpty());
    QCOMPARE(entryCorner->enumStrings(), QStringList({QStringLiteral("Top Left"), QStringLiteral("Top Right"),
                                                      QStringLiteral("Bottom Left"), QStringLiteral("Bottom Right")}));
    QCOMPARE(entryCorner->enumValues(), QVariantList({0, 1, 2, 3}));

    Fact* const dropletClass = _item->dropletClass();
    QVERIFY(dropletClass->metaData());
    QCOMPARE(dropletClass->rawValue().toUInt(), static_cast<uint>(AgriculturalSprayComplexItem::Coarse));
    QCOMPARE(dropletClass->metaData()->rawMin().toUInt(), 1U);
    QCOMPARE(dropletClass->metaData()->rawMax().toUInt(), 3U);
    QVERIFY(dropletClass->rawUnits().isEmpty());
    QCOMPARE(dropletClass->enumStrings(),
             QStringList({QStringLiteral("Fine"), QStringLiteral("Medium"), QStringLiteral("Coarse")}));
    QCOMPARE(dropletClass->enumValues(), QVariantList({1, 2, 3}));

    _item->beginInteractiveCreation();
    QGCFencePolygon* const invalid =
        geoFenceController()->polygons()->value<QGCFencePolygon*>(geoFenceController()->polygons()->count() - 1);
    QVERIFY(invalid);
    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Planner did not produce a route")));
    invalid->setPath({QGeoCoordinate(47.397742, 8.545594), QGeoCoordinate(47.398000, 8.546000)});
    _waitForStatus(AgriculturalSprayComplexItem::InvalidArea);
    verifyExpectedLogMessage();
    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*deleted")));
    geoFenceController()->deletePolygon(0);
    _waitForStatus(AgriculturalSprayComplexItem::GenerationError);
    verifyExpectedLogMessage();

    _makeReadyWithSquare();
    QVERIFY(_item->readyForSaveState() == VisualMissionItem::ReadyForSave);
}

void AgriculturalSprayComplexItemTest::_fenceEditsRebuildWhileDirty()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    _makeReadyWithSquare();
    const QList<QGeoCoordinate> initialRoute = _item->routeCoordinates();
    QVERIFY(initialRoute.size() > 2);

    QGCFencePolygon* const unrelatedInclusion = _addPolygon(true);
    QVERIFY(unrelatedInclusion);
    unrelatedInclusion->setPath(square(center.atDistanceAndAzimuth(500.0, 90.0), 100.0));
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
    QCOMPARE(_item->routeCoordinates(), initialRoute);

    QGCFencePolygon* const oversizedExclusion = _addPolygon(false);
    QVERIFY(oversizedExclusion);
    QList<QGeoCoordinate> oversizedPath;
    const QGeoCoordinate outsideCenter = center.atDistanceAndAzimuth(500.0, 90.0);
    for (int index = 0; index < 257; ++index) {
        oversizedPath.append(outsideCenter.atDistanceAndAzimuth(20.0, static_cast<double>(index) * 360.0 / 257.0));
    }
    oversizedExclusion->setPath(oversizedPath);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
    QCOMPARE(_item->routeCoordinates(), initialRoute);

    QGCFencePolygon* const invalidExclusion = _addPolygon(false);
    QVERIFY(invalidExclusion);
    invalidExclusion->setPath(
        {outsideCenter.atDistanceAndAzimuth(10.0, 0.0), outsideCenter.atDistanceAndAzimuth(10.0, 180.0)});
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
    QCOMPARE(_item->routeCoordinates(), initialRoute);

    QGCFencePolygon* const insideExclusion = _addPolygon(false);
    QVERIFY(insideExclusion);
    insideExclusion->setPath(square(center, 20.0));
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != initialRoute,
        TestTimeout::shortMs());
    const QList<QGeoCoordinate> insideExcludedRoute = _item->routeCoordinates();

    insideExclusion->setPath(square(center.atDistanceAndAzimuth(45.0, 90.0), 20.0));
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != insideExcludedRoute,
        TestTimeout::shortMs());
    const QList<QGeoCoordinate> overlappingExcludedRoute = _item->routeCoordinates();

    QGCFenceCircle* const circle = _addCircle(false);
    QVERIFY(circle);
    circle->setCenter(center.atDistanceAndAzimuth(18.0, 90.0));
    circle->radius()->setRawValue(10.0);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != overlappingExcludedRoute,
        TestTimeout::shortMs());
}

void AgriculturalSprayComplexItemTest::_planningInputsAndMetadataOnlyInputs()
{
    _makeReadyWithSquare();
    const QList<QGeoCoordinate> originalRoute = _item->routeCoordinates();
    const double originalDistance = _item->complexDistance();

    _item->lineSpacing()->setRawValue(20.0);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != originalRoute,
        TestTimeout::shortMs());
    const QList<QGeoCoordinate> spacingRoute = _item->routeCoordinates();

    _item->boundaryMargin()->setRawValue(2.0);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != spacingRoute,
        TestTimeout::shortMs());
    const QList<QGeoCoordinate> marginRoute = _item->routeCoordinates();

    _item->boundaryMarginScope()->setRawValue(AgriculturalSprayComplexItem::AllEdges);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != marginRoute,
        TestTimeout::shortMs());
    const QList<QGeoCoordinate> marginScopeRoute = _item->routeCoordinates();

    _item->gridAngle()->setRawValue(45.0);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != marginScopeRoute,
        TestTimeout::shortMs());
    const QList<QGeoCoordinate> angledRoute = _item->routeCoordinates();

    _item->entryCorner()->setRawValue(AgriculturalSprayComplexItem::BottomRight);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->entryCoordinate() != angledRoute.first(),
        TestTimeout::shortMs());
    QVERIFY(_item->complexDistance() > 0.0);
    QVERIFY(_item->complexDistance() != originalDistance);

    const QList<QGeoCoordinate> configuredRoute = _item->routeCoordinates();
    const double configuredDistance = _item->complexDistance();
    _item->dropletClass()->setRawValue(AgriculturalSprayComplexItem::Coarse);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
    QCOMPARE(_item->routeCoordinates(), configuredRoute);
    QCOMPARE(_item->complexDistance(), configuredDistance);

    _item->applicationRate()->setRawValue(20.0);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
    QCOMPARE(_item->routeCoordinates(), configuredRoute);
    QCOMPARE(_item->complexDistance(), configuredDistance);
}

void AgriculturalSprayComplexItemTest::_derivedRouteAndMissionExpansion()
{
    _makeReadyWithSquare();
    _item->altitude()->setRawValue(75.0);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);
    _item->setSequenceNumber(13);

    QCOMPARE(_item->entryCoordinate(), _item->routeCoordinates().first());
    QCOMPARE(_item->exitCoordinate(), _item->routeCoordinates().last());
    QVERIFY(!_item->exitCoordinateSameAsEntry());
    QVERIFY(_item->complexDistance() > 0.0);
    QVERIFY(_item->greatestDistanceTo(QGeoCoordinate(47.397742, 8.545594)) > 0.0);
    QCOMPARE(_item->lastSequenceNumber(), 13 + _item->routeCoordinates().size() - 1);

    QList<MissionItem*> missionItems;
    _item->appendMissionItems(missionItems, this);
    QCOMPARE(missionItems.size(), _item->routeCoordinates().size());
    for (int index = 0; index < missionItems.size(); ++index) {
        const MissionItem* const missionItem = missionItems[index];
        QCOMPARE(static_cast<MAV_CMD>(missionItem->command()), MAV_CMD_NAV_WAYPOINT);
        QCOMPARE(static_cast<MAV_FRAME>(missionItem->frame()), MAV_FRAME_GLOBAL_RELATIVE_ALT);
        QCOMPARE(missionItem->sequenceNumber(), 13 + index);
        QCOMPARE(missionItem->param7(), 75.0);
    }
}

void AgriculturalSprayComplexItemTest::_terrainUpdatesAreOptedOut()
{
    QSignalSpy terrainAltitudeSpy(_item, &VisualMissionItem::terrainAltitudeChanged);
    QSignalSpy terrainQueryFailedSpy(_item, &VisualMissionItem::terrainQueryFailedChanged);
    QSignalSpy coordinateSpy(_item, &VisualMissionItem::coordinateChanged);
    QVERIFY(terrainAltitudeSpy.isValid());
    QVERIFY(terrainQueryFailedSpy.isValid());
    QVERIFY(coordinateSpy.isValid());

    _makeReadyWithSquare();
    QVERIFY(coordinateSpy.count() > 0);
    QVERIFY(qIsNaN(_item->terrainAltitude()));
    QVERIFY(!_item->terrainQueryFailed());

    const QList<QGeoCoordinate> initialRoute = _item->routeCoordinates();
    const QGeoCoordinate initialCoordinate = _item->coordinate();
    coordinateSpy.clear();

    QGCFencePolygon* const polygon = geoFenceController()->polygons()->value<QGCFencePolygon*>(0);
    QVERIFY(polygon);
    const QGeoCoordinate shiftedCenter = QGeoCoordinate(47.397742, 8.545594).atDistanceAndAzimuth(20.0, 90.0);
    polygon->setPath(square(shiftedCenter, 100.0));
    QVERIFY_TRUE_WAIT(_item->status() == AgriculturalSprayComplexItem::Ready &&
                          _item->routeCoordinates() != initialRoute && _item->coordinate() != initialCoordinate,
                      TestTimeout::shortMs());
    QVERIFY(coordinateSpy.count() > 0);

    // Process events beyond VisualMissionItem's terrain-query debounce interval without relying on a fixed sleep.
    QTimer observationWindow;
    observationWindow.setSingleShot(true);
    QSignalSpy observationWindowSpy(&observationWindow, &QTimer::timeout);
    QVERIFY(observationWindowSpy.isValid());
    observationWindow.start(TestTimeout::shortMs());
    QVERIFY_SIGNAL_WAIT(observationWindowSpy, TestTimeout::mediumMs());

    QVERIFY(qIsNaN(_item->terrainAltitude()));
    QVERIFY(!_item->terrainQueryFailed());
    QCOMPARE(terrainAltitudeSpy.count(), 0);
    QCOMPARE(terrainQueryFailedSpy.count(), 0);
}

void AgriculturalSprayComplexItemTest::_stockItemTerrainUpdatesRemainEnabled()
{
    SimpleMissionItem stockItem(planController(), true /* flyView */, false /* forLoad */);
    QSignalSpy terrainAltitudeSpy(&stockItem, &VisualMissionItem::terrainAltitudeChanged);
    QVERIFY(terrainAltitudeSpy.isValid());

    stockItem.setCoordinate(QGeoCoordinate(47.397742, 8.545594));

    QCOMPARE(terrainAltitudeSpy.count(), 2);
    QVERIFY(qIsNaN(stockItem.terrainAltitude()));
    QVERIFY(!stockItem.terrainQueryFailed());
}

void AgriculturalSprayComplexItemTest::_jsonRoundTripIsStrictAndSelfContained()
{
    _makeReadyWithSquare();
    _item->altitude()->setRawValue(60.0);
    _item->lineSpacing()->setRawValue(12.5);
    _item->boundaryMargin()->setRawValue(2.25);
    _item->boundaryMarginScope()->setRawValue(AgriculturalSprayComplexItem::AllEdges);
    _item->setDirectionVertexIndex(2);
    _item->setMarginEdgeIndex(1);
    _item->dropletClass()->setRawValue(AgriculturalSprayComplexItem::Fine);
    _item->applicationRate()->setRawValue(2.5);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.first().toObject();
    QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 7);
    QCOMPARE(saved.size(), 16);
    for (const QString& key :
         {QStringLiteral("version"), QStringLiteral("type"), QStringLiteral("complexItemType"),
          QStringLiteral("Altitude"), QStringLiteral("LineSpacing"), QStringLiteral("BoundaryMargin"),
          QStringLiteral("BoundaryMarginScope"), QStringLiteral("directionVertexIndex"),
          QStringLiteral("marginEdgeIndex"), QStringLiteral("marginEdgeIndices"), QStringLiteral("marginEdgeMargins"),
          QStringLiteral("DropletClass"), QStringLiteral("ApplicationRate"), QStringLiteral("sourcePolygonIndex"),
          QStringLiteral("exclusionMargins"), QStringLiteral("nonSprayPolygons")}) {
        QVERIFY2(saved.contains(key), qPrintable(key));
    }
    QVERIFY(!saved.contains(QStringLiteral("GridAngle")));
    QVERIFY(!saved.contains(QStringLiteral("EntryCorner")));

    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(loaded.load(saved, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(loaded.status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    QCOMPARE(loaded.altitude()->rawValue().toDouble(), 60.0);
    QCOMPARE(loaded.lineSpacing()->rawValue().toDouble(), 12.5);
    QCOMPARE(loaded.boundaryMargin()->rawValue().toDouble(), 2.25);
    QCOMPARE(loaded.boundaryMarginScope()->rawValue().toUInt(),
             static_cast<uint>(AgriculturalSprayComplexItem::AllEdges));
    QCOMPARE(loaded.directionVertexIndex(), 2);
    QCOMPARE(loaded.marginEdgeIndex(), 1);
    QCOMPARE(loaded.dropletClass()->rawValue().toUInt(), static_cast<uint>(AgriculturalSprayComplexItem::Fine));
    QCOMPARE(loaded.applicationRate()->rawValue().toDouble(), 2.5);
}

void AgriculturalSprayComplexItemTest::_sourcePolygonReferenceRoundTripTracksReorder()
{
    const QGeoCoordinate center(47.397742, 8.545594);
    QGCFencePolygon* const unrelated = _addPolygon(true);
    QVERIFY(unrelated);
    unrelated->setPath(square(center.atDistanceAndAzimuth(500.0, 90.0), 100.0));
    _makeReadyWithSquare();

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.at(0).toObject();
    QCOMPARE(saved.value(QStringLiteral("sourcePolygonIndex")).toInt(), 1);

    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(loaded.load(saved, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(loaded.status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());

    geoFenceController()->polygons()->move(1, 0);
    QCOMPARE_TRUE_WAIT(loaded.status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());

    savedItems = {};
    loaded.save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    QCOMPARE(savedItems.at(0).toObject().value(QStringLiteral("sourcePolygonIndex")).toInt(), 0);
}

void AgriculturalSprayComplexItemTest::_legacySourcePolygonReferenceBindsWithWarning()
{
    QGCFencePolygon* const polygon = _addPolygon(true);
    QVERIFY(polygon);
    polygon->setPath(square(QGeoCoordinate(47.397742, 8.545594), 100.0));

    expectLogMessage(
        "qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
        QRegularExpression(QStringLiteral("Legacy Agricultural Spray plan has no source polygon reference")));
    QString errorString;
    QVERIFY(_item->load(sprayJson(), 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    verifyExpectedLogMessage();
    QCOMPARE(_item->boundaryMargin()->rawValue().toDouble(), 0.0);
    QCOMPARE(_item->boundaryMarginScope()->rawValue().toUInt(),
             static_cast<uint>(AgriculturalSprayComplexItem::SelectedEdge));
    QCOMPARE(_item->marginEdgeIndex(), 0);

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    QCOMPARE(savedItems.at(0).toObject().value(QStringLiteral("sourcePolygonIndex")).toInt(), 0);
}

void AgriculturalSprayComplexItemTest::_invalidSourcePolygonReference_data()
{
    QTest::addColumn<bool>("inclusion");
    QTest::addColumn<int>("sourcePolygonIndex");
    QTest::addColumn<QString>("expectedError");

    QTest::newRow("out-of-range") << true << 1 << QStringLiteral("out of range");
    QTest::newRow("wrong-inclusion") << false << 0 << QStringLiteral("not an inclusion");
}

void AgriculturalSprayComplexItemTest::_invalidSourcePolygonReference()
{
    QFETCH(bool, inclusion);
    QFETCH(int, sourcePolygonIndex);
    QFETCH(QString, expectedError);

    QGCFencePolygon* const polygon = _addPolygon(inclusion);
    QVERIFY(polygon);
    polygon->setPath(square(QGeoCoordinate(47.397742, 8.545594), 100.0));
    QJsonObject json = sprayJson();
    json[QStringLiteral("sourcePolygonIndex")] = sourcePolygonIndex;

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*%1").arg(expectedError)));
    QString errorString;
    QVERIFY(_item->load(json, 7, errorString));
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::GenerationError, TestTimeout::shortMs());
    QVERIFY(_item->errorText().contains(expectedError));
    verifyExpectedLogMessage();
}

void AgriculturalSprayComplexItemTest::_deletedSourcePolygonReportsExplicitError()
{
    QGCFencePolygon* const polygon = _addPolygon(true);
    QVERIFY(polygon);
    polygon->setPath(square(QGeoCoordinate(47.397742, 8.545594), 100.0));
    QJsonObject json = sprayJson();
    json[QStringLiteral("sourcePolygonIndex")] = 0;

    QString errorString;
    QVERIFY(_item->load(json, 7, errorString));
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(
                         QStringLiteral("Route input snapshot failed.*selected spray inclusion polygon was deleted")));
    geoFenceController()->deletePolygon(0);
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::GenerationError, TestTimeout::shortMs());
    QVERIFY(_item->errorText().contains(QStringLiteral("deleted")));
    verifyExpectedLogMessage();
}

void AgriculturalSprayComplexItemTest::_dropletClassLoadMigration_data()
{
    QTest::addColumn<int>("storedValue");
    QTest::addColumn<uint>("expectedValue");
    QTest::addColumn<QString>("warning");

    QTest::newRow("legacy-very-fine") << 0 << static_cast<uint>(AgriculturalSprayComplexItem::Fine)
                                      << QStringLiteral("Normalizing legacy droplet class 0 to Fine");
    QTest::newRow("fine") << 1 << static_cast<uint>(AgriculturalSprayComplexItem::Fine) << QString();
    QTest::newRow("medium") << 2 << static_cast<uint>(AgriculturalSprayComplexItem::Medium) << QString();
    QTest::newRow("coarse") << 3 << static_cast<uint>(AgriculturalSprayComplexItem::Coarse) << QString();
    QTest::newRow("legacy-very-coarse") << 4 << static_cast<uint>(AgriculturalSprayComplexItem::Coarse)
                                        << QStringLiteral("Normalizing legacy droplet class 4 to Coarse");
    QTest::newRow("legacy-extremely-coarse") << 5 << static_cast<uint>(AgriculturalSprayComplexItem::Coarse)
                                             << QStringLiteral("Normalizing legacy droplet class 5 to Coarse");
    QTest::newRow("legacy-ultra-coarse") << 6 << static_cast<uint>(AgriculturalSprayComplexItem::Coarse)
                                         << QStringLiteral("Normalizing legacy droplet class 6 to Coarse");
}

void AgriculturalSprayComplexItemTest::_dropletClassLoadMigration()
{
    QFETCH(int, storedValue);
    QFETCH(uint, expectedValue);
    QFETCH(QString, warning);

    QGCFencePolygon* const polygon = _addPolygon(true);
    QVERIFY(polygon);
    polygon->setPath(square(QGeoCoordinate(47.397742, 8.545594), 100.0));
    QJsonObject json = sprayJson(storedValue);
    json[QStringLiteral("sourcePolygonIndex")] = 0;

    if (!warning.isEmpty()) {
        expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg, QRegularExpression(warning));
    }
    QString errorString;
    QVERIFY(_item->load(json, 7, errorString));
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    if (!warning.isEmpty()) {
        verifyExpectedLogMessage();
    }
    QCOMPARE(_item->dropletClass()->rawValue().toUInt(), expectedValue);

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    QCOMPARE(static_cast<uint>(savedItems.at(0).toObject().value(QStringLiteral("DropletClass")).toInt()),
             expectedValue);
}

void AgriculturalSprayComplexItemTest::_invalidJson_data()
{
    QTest::addColumn<QJsonObject>("object");

    const QJsonObject valid = {
        {QStringLiteral("version"), 1},
        {QStringLiteral("type"), QStringLiteral("ComplexItem")},
        {QStringLiteral("complexItemType"), QStringLiteral("AgriculturalSpray")},
        {QStringLiteral("Altitude"), 60.0},
        {QStringLiteral("LineSpacing"), 12.5},
        {QStringLiteral("GridAngle"), -15.0},
        {QStringLiteral("EntryCorner"), 2},
        {QStringLiteral("DropletClass"), 1},
        {QStringLiteral("ApplicationRate"), 2.5},
    };

    QJsonObject object = valid;
    object.remove(QStringLiteral("ApplicationRate"));
    QTest::newRow("missing-required-field") << object;

    object = valid;
    object[QStringLiteral("path")] = QJsonArray();
    QTest::newRow("unexpected-route-field") << object;

    object = valid;
    object[QStringLiteral("version")] = 8;
    QTest::newRow("unsupported-version") << object;

    object = valid;
    object[QStringLiteral("Altitude")] = QStringLiteral("60");
    QTest::newRow("wrong-fact-type") << object;

    object = valid;
    object[QStringLiteral("EntryCorner")] = 1.5;
    QTest::newRow("fractional-enum") << object;

    object = valid;
    object[QStringLiteral("DropletClass")] = 7;
    QTest::newRow("out-of-range-enum") << object;
}

void AgriculturalSprayComplexItemTest::_invalidJson()
{
    QFETCH(QJsonObject, object);

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "(JSON validation failed|JSON Fact validation failed|version 8 is not supported)")));
    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(!loaded.load(object, 7, errorString));
    QVERIFY(!errorString.isEmpty());
    verifyExpectedLogMessage();
}

UT_REGISTER_TEST(AgriculturalSprayComplexItemTest, TestLabel::Unit, TestLabel::MissionManager)
