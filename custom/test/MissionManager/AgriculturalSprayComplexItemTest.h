#pragma once

#include "AgriculturalSprayComplexItem.h"
#include "BaseClasses/MissionTest.h"

class QGCFenceCircle;
class QGCFencePolygon;

class AgriculturalSprayComplexItemTest final : public OfflineMissionTest
{
    Q_OBJECT

private slots:
    void init() final;
    void cleanup() final;
    void directionVertexDefaultsToZero();
    void rapidDirectionChangesPublishLatestRoute();

private:
    void _factMetadataAndAreaStates();
    void _fenceEditsRebuildWhileDirty();
    void _planningInputsAndMetadataOnlyInputs();
    void _derivedRouteAndMissionExpansion();
    void _terrainUpdatesAreOptedOut();
    void _stockItemTerrainUpdatesRemainEnabled();
    void _jsonRoundTripIsStrictAndSelfContained();
    void _sourcePolygonReferenceRoundTripTracksReorder();
    void _legacySourcePolygonReferenceBindsWithWarning();
    void _invalidSourcePolygonReference_data();
    void _invalidSourcePolygonReference();
    void _deletedSourcePolygonReportsExplicitError();
    void _dropletClassLoadMigration_data();
    void _dropletClassLoadMigration();
    void _invalidJson_data();
    void _invalidJson();
    QGCFencePolygon* _addPolygon(bool inclusion);
    QGCFenceCircle* _addCircle(bool inclusion);
    void _makeReadyWithSquare();
    void _waitForStatus(AgriculturalSprayComplexItem::Status status);

    AgriculturalSprayComplexItem* _item = nullptr;
};
