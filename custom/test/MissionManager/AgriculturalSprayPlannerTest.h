#pragma once

#include "UnitTest.h"

class AgriculturalSprayPlannerTest final : public UnitTest
{
    Q_OBJECT

private slots:
    void _areaGeometry_data();
    void _areaGeometry();
    void _exclusionScanlineSplit_data();
    void _exclusionScanlineSplit();
    void _circleObstacleRouting_data();
    void _circleObstacleRouting();
    void _circleConnectorStringPulling_data();
    void _circleConnectorStringPulling();
    void _circleDetourEntryIsForward_data();
    void _circleDetourEntryIsForward();
    void _tangentExclusionBoundaryIsNonFlyable();
    void _circleInclusionIsConservative();
    void _entryCorner_data();
    void _entryCorner();
    void _boundaryMarginZeroPreservesRoute();
    void _selectedEdgeBoundaryMargin();
    void _allEdgesBoundaryMarginIsConcaveAndDeterministic();
    void _excessiveBoundaryMarginIsEmpty();
    void _exclusionMarginsAreConservative();
    void _failureIsFailClosed_data();
    void _failureIsFailClosed();
    void _inputOrderAndWindingAreDeterministic();
    void _circleRoutingIsDeterministic();
    void _symmetricExclusionsAreDeterministic();
    void _nearEqualExclusionOrderingIsDeterministic();
};
