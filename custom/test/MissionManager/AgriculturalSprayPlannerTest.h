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
    void _circleInclusionIsConservative();
    void _entryCorner_data();
    void _entryCorner();
    void _failureIsFailClosed_data();
    void _failureIsFailClosed();
    void _inputOrderAndWindingAreDeterministic();
};
