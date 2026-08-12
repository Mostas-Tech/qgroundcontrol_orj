#pragma once

#include "QGCOptions.h"

class CustomOptions final : public QGCOptions
{
    Q_OBJECT

public:
    explicit CustomOptions(QObject* parent = nullptr);

    bool showPlanInfo() const final { return false; }
    bool showPlanDefaults() const final { return false; }
    bool showInitialCameraSettings() const final { return false; }
    bool showRallyPoints() const final { return false; }
    bool showTransform() const final { return false; }
    bool showRallyLayer() const final { return false; }
    QColor geoFencePolygonBorderColor() const final { return QColorConstants::Blue; }
    bool newGeoFenceCircleInclusion() const final { return false; }
    double newGeoFenceCircleRadius() const final { return 3.0; }
};
