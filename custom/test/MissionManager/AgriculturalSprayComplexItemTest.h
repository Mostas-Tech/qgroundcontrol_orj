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

    void _factMetadataAndAreaStates();
    void _fenceEditsRebuildWhileDirty();
    void _planningInputsAndMetadataOnlyInputs();
    void _derivedRouteAndMissionExpansion();
    void _terrainUpdatesAreOptedOut();
    void _stockItemTerrainUpdatesRemainEnabled();
    void _sequenceAndDownstreamRecalculation();
    void _jsonRoundTripIsStrictAndSelfContained();
    void _invalidJson_data();
    void _invalidJson();

private:
    QGCFencePolygon* _addPolygon(bool inclusion);
    QGCFenceCircle* _addCircle(bool inclusion);
    void _makeReadyWithSquare();
    void _waitForStatus(AgriculturalSprayComplexItem::Status status);

    AgriculturalSprayComplexItem* _item = nullptr;
};
