#pragma once

#include "BaseClasses/MissionTest.h"

class AgriculturalSprayPlanLoadTest final : public OfflineMissionTest
{
    Q_OBJECT

private slots:
    void _missionLoadedBeforeFenceBuildsFinalRouteAndClearsStaleRoute();
    void _duplicateSprayLoadIsRejectedAtomically();
};
