#include "AgriculturalSprayComplexItemTest.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
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
    delete _item;
    _item = nullptr;
    OfflineMissionTest::cleanup();
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
    verifyFact(_item->applicationRate(), 1.0, 0.1, 100.0, QStringLiteral("L/da"));

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
    QCOMPARE(dropletClass->enumStrings(), QStringList({QStringLiteral("Fine"), QStringLiteral("Medium"),
                                                        QStringLiteral("Coarse")}));
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
        oversizedPath.append(
            outsideCenter.atDistanceAndAzimuth(20.0, static_cast<double>(index) * 360.0 / 257.0));
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

    _item->gridAngle()->setRawValue(45.0);
    QVERIFY_TRUE_WAIT(
        _item->status() == AgriculturalSprayComplexItem::Ready && _item->routeCoordinates() != spacingRoute,
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
    _item->gridAngle()->setRawValue(-15.0);
    _item->entryCorner()->setRawValue(AgriculturalSprayComplexItem::BottomLeft);
    _item->dropletClass()->setRawValue(AgriculturalSprayComplexItem::Fine);
    _item->applicationRate()->setRawValue(2.5);
    _waitForStatus(AgriculturalSprayComplexItem::Ready);

    QJsonArray savedItems;
    _item->save(savedItems);
    QCOMPARE(savedItems.size(), 1);
    const QJsonObject saved = savedItems.first().toObject();
    QCOMPARE(saved.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(saved.size(), 10);
    for (const QString& key :
         {QStringLiteral("version"), QStringLiteral("type"), QStringLiteral("complexItemType"),
          QStringLiteral("Altitude"), QStringLiteral("LineSpacing"), QStringLiteral("GridAngle"),
          QStringLiteral("EntryCorner"), QStringLiteral("DropletClass"), QStringLiteral("ApplicationRate"),
          QStringLiteral("sourcePolygonIndex")}) {
        QVERIFY2(saved.contains(key), qPrintable(key));
    }

    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(loaded.load(saved, 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(loaded.status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    QCOMPARE(loaded.altitude()->rawValue().toDouble(), 60.0);
    QCOMPARE(loaded.lineSpacing()->rawValue().toDouble(), 12.5);
    QCOMPARE(loaded.gridAngle()->rawValue().toDouble(), -15.0);
    QCOMPARE(loaded.entryCorner()->rawValue().toUInt(), static_cast<uint>(AgriculturalSprayComplexItem::BottomLeft));
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

    expectLogMessage("qgc.custom.agriculturalspraycomplexitem", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Legacy Agricultural Spray plan has no source polygon reference")));
    QString errorString;
    QVERIFY(_item->load(sprayJson(), 7, errorString));
    QVERIFY(errorString.isEmpty());
    QCOMPARE_TRUE_WAIT(_item->status(), AgriculturalSprayComplexItem::Ready, TestTimeout::shortMs());
    verifyExpectedLogMessage();

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
                     QRegularExpression(QStringLiteral("Route input snapshot failed.*selected spray inclusion polygon was deleted")));
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
    object[QStringLiteral("version")] = 2;
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
                         "(JSON validation failed|JSON Fact validation failed|version 2 is not supported)")));
    AgriculturalSprayComplexItem loaded(planController(), false);
    QString errorString;
    QVERIFY(!loaded.load(object, 7, errorString));
    QVERIFY(!errorString.isEmpty());
    verifyExpectedLogMessage();
}

UT_REGISTER_TEST(AgriculturalSprayComplexItemTest, TestLabel::Unit, TestLabel::MissionManager)
