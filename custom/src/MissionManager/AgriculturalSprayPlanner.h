#pragma once

#include <cstddef>
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
    std::size_t maxRoutePoints = 32768;
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

/// A successful result has a complete route only; every failing result has empty legs and route.
struct PlannerResult
{
    PlannerStatus status = PlannerStatus::InvalidInput;
    std::string error;
    std::vector<SprayLeg> legs;
    std::vector<RoutePoint> route;
    double distance = 0.0;

    [[nodiscard]] bool succeeded() const { return status == PlannerStatus::Success; }
};

/// Builds a bounded, deterministic spray route for union(inclusions) minus union(exclusions).
[[nodiscard]] PlannerResult plan(const PlannerInput& input);

}  // namespace AgriculturalSpray
