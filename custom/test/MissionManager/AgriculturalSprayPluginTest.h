#pragma once

#include "BaseClasses/MissionTest.h"

class PlanMasterController;

class AgriculturalSprayPluginTest final : public OfflineMissionTest
{
    Q_OBJECT

private slots:
    void _singleSprayPolicyAndOtherComplexItems();
    void _sprayPolicyIsPerPlan();
    void _interactiveCreationBindsNewPolygonButImportDoesNot();
    void _traceCompletionBuildsRouteAndRestoresMissionLayer();
    void _customAndStockPlanOptionsRemainDistinct();
};
