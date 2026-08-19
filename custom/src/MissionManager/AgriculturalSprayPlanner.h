#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "AgriculturalSprayGeometry.h"

namespace AgriculturalSpray {

/// A simple, non-self-intersecting local-NED polygon. The closing vertex is omitted.
struct Polygon
{
    std::vector<Point> vertices;
};

/// A local-NED circle.
struct Circle
{
    Point center;
    double radius = 0.0;
};

using Shape = std::variant<Polygon, Circle>;

/// Survey's four entry-location choices in the rotated grid frame.
/// Top/bottom start at the maximum/minimum along-grid extent; left/right start at the
/// minimum/maximum across-grid extent.
enum class EntryCorner
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

/// Limits prevent unbounded work from untrusted planning input.
struct PlannerLimits
{
    std::size_t maxShapes = 64;
    std::size_t maxShapeVertices = 256;
    std::size_t maxScanLines = 4096;
    std::size_t maxSprayLegs = 8192;
    std::size_t maxVisibilityNodes = 512;
    std::size_t maxTopologyRepresentatives = 32768;
    std::size_t maxRoutePoints = 32768;
    std::size_t maxCoverageCells = 256;
    std::size_t exactCellLimit = 12;
};

/// Fixed kinematic assumptions used to compare route orderings. These values do not command the vehicle.
struct PlannerCostModel
{
    double spraySpeed = 7.0;
    double transitSpeed = 10.0;
    double acceleration = 2.5;
    double yawRateDegrees = 90.0;
    double yawThresholdDegrees = 5.0;
    double yawSettleSeconds = 0.3;
};

/// Immutable metric inputs. Positive grid angles follow Survey: 0 is north/south, 90 is east/west.
/// Grid angles must be in Survey's inclusive [-360, 360] degree range.
struct PlannerInput
{
    std::vector<Shape> inclusions;
    std::vector<Shape> exclusions;
    double spacing = 0.0;
    double gridAngleDegrees = 0.0;
    double circleChordError = 0.05;
    /// New corner-driven input. When both values are present, they replace gridAngleDegrees/entryCorner.
    std::optional<Point> entryPoint;
    std::optional<Point> sweepDirection;
    PlannerCostModel costModel;

    /// Legacy compatibility for version-1 plans and existing low-level callers.
    EntryCorner entryCorner = EntryCorner::TopLeft;
    PlannerLimits limits;
};

struct SprayLeg
{
    Point start;
    Point end;
};

enum class RoutePointType
{
    SprayStart,
    SprayEnd,
    Transit,
};

struct RoutePoint
{
    Point position;
    RoutePointType type = RoutePointType::Transit;
};

enum class PlannerStatus
{
    Success,
    InvalidInput,
    ComplexityLimit,
    EmptyRegion,
    DisconnectedRegion,
    NoRoute,
};

enum class PlannerMethod
{
    Legacy,
    ExactCellOptimization,
    HeuristicCellOptimization,
    LegacyFallback,
};

/// A successful result has a complete route only; every failing result has empty legs and route.
struct PlannerResult
{
    PlannerStatus status = PlannerStatus::InvalidInput;
    std::string error;
    std::vector<SprayLeg> legs;
    std::vector<RoutePoint> route;
    double distance = 0.0;
    double sprayDistance = 0.0;
    double transitDistance = 0.0;
    double estimatedTime = 0.0;
    std::size_t turnCount = 0;
    std::size_t activeCellCount = 0;
    PlannerMethod method = PlannerMethod::Legacy;
    bool usedLegacyFallback = false;

    [[nodiscard]] bool succeeded() const { return status == PlannerStatus::Success; }
};

/// Builds a bounded, deterministic spray route for union(inclusions) minus union(exclusions).
[[nodiscard]] PlannerResult plan(const PlannerInput& input);

}  // namespace AgriculturalSpray
