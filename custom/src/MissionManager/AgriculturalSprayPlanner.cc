#include "AgriculturalSprayPlanner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace AgriculturalSpray {
namespace {

constexpr double DISTANCE_EPSILON = 1e-9;
constexpr double PARAMETER_EPSILON = 1e-9;
constexpr double MAXIMUM_COORDINATE = 1e7;
constexpr double BOUNDARY_INSET = 1e-7;
constexpr double OBSTACLE_CLEARANCE = 0.05;
constexpr double ROUNDING_SAFETY_FACTOR = 16.0;

struct Interval
{
    double start = 0.0;
    double end = 0.0;
};

struct GridFrame
{
    Point direction;
    Point normal;

    [[nodiscard]] double along(const Point& point) const { return dot(point, direction); }

    [[nodiscard]] double across(const Point& point) const { return dot(point, normal); }

    [[nodiscard]] Point point(double alongValue, double acrossValue) const
    {
        return direction * alongValue + normal * acrossValue;
    }
};

struct GridBounds
{
    double minimumAlong = std::numeric_limits<double>::infinity();
    double maximumAlong = -std::numeric_limits<double>::infinity();
    double minimumAcross = std::numeric_limits<double>::infinity();
    double maximumAcross = -std::numeric_limits<double>::infinity();
};

struct PreparedEdge
{
    Point first;
    Point second;
    GridBounds bounds;
};

struct CircleNavigation
{
    Point center;
    double radius = 0.0;
};

struct PreparedShape
{
    std::vector<Point> vertices;
    std::vector<PreparedEdge> edges;
    GridBounds bounds;
    std::optional<CircleNavigation> circleNavigation;
    bool inclusion = false;
};

struct PreparedInput
{
    std::vector<PreparedShape> inclusions;
    std::vector<PreparedShape> exclusions;
    GridFrame frame;
    PlannerLimits limits;
};

struct ScanRow
{
    double across = 0.0;
    std::vector<Interval> intervals;
};

enum class ScanRowsStatus
{
    Found,
    InvalidGeometry,
    ComplexityLimit,
};

struct ScanRowsResult
{
    ScanRowsStatus status = ScanRowsStatus::InvalidGeometry;
    std::vector<ScanRow> rows;
};

enum class TransitStatus
{
    Found,
    NoRoute,
};

struct TransitResult
{
    TransitStatus status = TransitStatus::NoRoute;
    std::vector<Point> points;
};

struct Roadmap
{
    std::vector<Point> nodes;
    std::vector<std::vector<std::size_t>> edges;
    std::vector<std::size_t> components;
};

[[nodiscard]] bool finiteCoordinate(double value)
{
    return std::isfinite(value) && std::abs(value) <= MAXIMUM_COORDINATE;
}

[[nodiscard]] bool finitePoint(const Point& point)
{
    return finiteCoordinate(point.north) && finiteCoordinate(point.east);
}

[[nodiscard]] bool validEntryCorner(EntryCorner entryCorner)
{
    switch (entryCorner) {
        case EntryCorner::TopLeft:
        case EntryCorner::TopRight:
        case EntryCorner::BottomLeft:
        case EntryCorner::BottomRight:
            return true;
    }
    return false;
}

[[nodiscard]] double coordinateMagnitude(std::initializer_list<Point> points)
{
    double magnitude = 1.0;
    for (const Point& point : points) {
        magnitude = std::max({magnitude, std::abs(point.north), std::abs(point.east)});
    }
    return magnitude;
}

[[nodiscard]] double coordinateTolerance(double magnitude)
{
    return std::max(DISTANCE_EPSILON, ROUNDING_SAFETY_FACTOR * std::numeric_limits<double>::epsilon() * magnitude);
}

[[nodiscard]] double crossTolerance(const Point& left, const Point& right, double sourceCoordinateMagnitude)
{
    const double productMagnitude =
        std::max(1.0, std::abs(left.north * right.east) + std::abs(left.east * right.north));
    const double subtractionError =
        coordinateTolerance(sourceCoordinateMagnitude) * (std::max(1.0, length(left)) + std::max(1.0, length(right)));
    return ROUNDING_SAFETY_FACTOR * std::numeric_limits<double>::epsilon() * productMagnitude + subtractionError;
}

[[nodiscard]] bool samePoint(const Point& first, const Point& second)
{
    return distance(first, second) <= coordinateTolerance(coordinateMagnitude({first, second}));
}

[[nodiscard]] double signedArea(const std::vector<Point>& vertices)
{
    const Point origin = vertices.front();
    double twiceArea = 0.0;
    for (std::size_t index = 1; index + 1 < vertices.size(); ++index) {
        twiceArea += cross(vertices[index] - origin, vertices[index + 1] - origin);
    }
    return twiceArea * 0.5;
}

[[nodiscard]] bool pointOnSegment(const Point& point, const Point& first, const Point& second)
{
    const Point segment = second - first;
    const Point toPoint = point - first;
    const double sourceMagnitude = coordinateMagnitude({point, first, second});
    if (std::abs(cross(segment, toPoint)) > crossTolerance(segment, toPoint, sourceMagnitude)) {
        return false;
    }

    const double tolerance = coordinateTolerance(sourceMagnitude);
    return point.north >= std::min(first.north, second.north) - tolerance &&
           point.north <= std::max(first.north, second.north) + tolerance &&
           point.east >= std::min(first.east, second.east) - tolerance &&
           point.east <= std::max(first.east, second.east) + tolerance;
}

[[nodiscard]] int orientation(const Point& first, const Point& second, const Point& third)
{
    const Point firstVector = second - first;
    const Point secondVector = third - first;
    const double value = cross(firstVector, secondVector);
    const double tolerance = crossTolerance(firstVector, secondVector, coordinateMagnitude({first, second, third}));
    if (value > tolerance) {
        return 1;
    }
    if (value < -tolerance) {
        return -1;
    }
    return 0;
}

[[nodiscard]] bool segmentsIntersect(const Point& firstStart, const Point& firstEnd, const Point& secondStart,
                                     const Point& secondEnd)
{
    const int firstA = orientation(firstStart, firstEnd, secondStart);
    const int firstB = orientation(firstStart, firstEnd, secondEnd);
    const int secondA = orientation(secondStart, secondEnd, firstStart);
    const int secondB = orientation(secondStart, secondEnd, firstEnd);

    if (firstA != firstB && secondA != secondB) {
        return true;
    }

    return (firstA == 0 && pointOnSegment(secondStart, firstStart, firstEnd)) ||
           (firstB == 0 && pointOnSegment(secondEnd, firstStart, firstEnd)) ||
           (secondA == 0 && pointOnSegment(firstStart, secondStart, secondEnd)) ||
           (secondB == 0 && pointOnSegment(firstEnd, secondStart, secondEnd));
}

[[nodiscard]] bool validPolygon(const Polygon& polygon, const PlannerLimits& limits, std::string& error)
{
    if (polygon.vertices.size() < 3 || polygon.vertices.size() > limits.maxShapeVertices) {
        error = "polygon vertex count is outside planner limits";
        return false;
    }

    for (std::size_t index = 0; index < polygon.vertices.size(); ++index) {
        const Point& current = polygon.vertices[index];
        const Point& next = polygon.vertices[(index + 1) % polygon.vertices.size()];
        if (!finitePoint(current) || distance(current, next) <= DISTANCE_EPSILON) {
            error = "polygon contains a non-finite or repeated adjacent vertex";
            return false;
        }
    }

    double extent = 0.0;
    double perimeter = 0.0;
    double sourceMagnitude = 1.0;
    const Point& origin = polygon.vertices.front();
    for (std::size_t index = 0; index < polygon.vertices.size(); ++index) {
        const Point& vertex = polygon.vertices[index];
        const Point& next = polygon.vertices[(index + 1) % polygon.vertices.size()];
        extent = std::max(extent, distance(origin, vertex));
        perimeter += distance(vertex, next);
        sourceMagnitude = std::max(sourceMagnitude, coordinateMagnitude({vertex}));
    }
    const double areaTolerance = coordinateTolerance(sourceMagnitude) * std::max({1.0, extent, perimeter});
    if (std::abs(signedArea(polygon.vertices)) <= areaTolerance) {
        error = "polygon has zero area";
        return false;
    }

    for (std::size_t first = 0; first < polygon.vertices.size(); ++first) {
        const std::size_t firstNext = (first + 1) % polygon.vertices.size();
        for (std::size_t second = first + 1; second < polygon.vertices.size(); ++second) {
            const std::size_t secondNext = (second + 1) % polygon.vertices.size();
            if (first == second || firstNext == second || secondNext == first) {
                continue;
            }
            if (segmentsIntersect(polygon.vertices[first], polygon.vertices[firstNext], polygon.vertices[second],
                                  polygon.vertices[secondNext])) {
                error = "polygon self-intersects or touches itself";
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] std::optional<std::size_t> circleSegmentCount(double radius, double chordError, bool inclusion)
{
    if (!finiteCoordinate(radius) || !finiteCoordinate(chordError) || radius <= DISTANCE_EPSILON || chordError <= 0.0) {
        return std::nullopt;
    }

    const double ratio = inclusion ? std::max(-1.0, 1.0 - chordError / radius) : radius / (radius + chordError);
    const double angle = std::acos(std::clamp(ratio, -1.0, 1.0));
    if (angle <= 0.0 || !std::isfinite(angle)) {
        return std::nullopt;
    }

    const double segments = std::ceil(std::numbers::pi / angle);
    if (!std::isfinite(segments) || segments > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }

    return std::max<std::size_t>(3, static_cast<std::size_t>(segments));
}

[[nodiscard]] std::optional<std::vector<Point>> approximateCircle(const Circle& circle, double chordError,
                                                                  bool inclusion, const GridFrame& frame,
                                                                  const PlannerLimits& limits)
{
    if (!finitePoint(circle.center)) {
        return std::nullopt;
    }

    const std::optional<std::size_t> count = circleSegmentCount(circle.radius, chordError, inclusion);
    if (!count || *count > limits.maxShapeVertices) {
        return std::nullopt;
    }

    const double halfStep = std::numbers::pi / static_cast<double>(*count);
    const double radius = inclusion ? circle.radius : circle.radius / std::cos(halfStep);
    const double gridAngle = std::atan2(frame.direction.east, frame.direction.north);
    // Align vertices/faces with the grid so approximations stay deterministic when the sweep rotates.
    const double phase = inclusion ? gridAngle : gridAngle + halfStep;

    std::vector<Point> vertices;
    vertices.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index) {
        const double angle =
            phase + (2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(*count));
        const Point vertex{
            circle.center.north + radius * std::cos(angle),
            circle.center.east + radius * std::sin(angle),
        };
        if (!finitePoint(vertex)) {
            return std::nullopt;
        }
        vertices.push_back(vertex);
    }
    return vertices;
}

[[nodiscard]] GridBounds gridBounds(const std::vector<Point>& vertices, const GridFrame& frame)
{
    GridBounds bounds;
    for (const Point& vertex : vertices) {
        const double along = frame.along(vertex);
        const double across = frame.across(vertex);
        bounds.minimumAlong = std::min(bounds.minimumAlong, along);
        bounds.maximumAlong = std::max(bounds.maximumAlong, along);
        bounds.minimumAcross = std::min(bounds.minimumAcross, across);
        bounds.maximumAcross = std::max(bounds.maximumAcross, across);
    }
    return bounds;
}

[[nodiscard]] bool pointLexicographicallyLess(const Point& left, const Point& right)
{
    return left.north == right.north ? left.east < right.east : left.north < right.north;
}

[[nodiscard]] PreparedShape prepareShape(std::vector<Point> vertices, bool inclusion, const GridFrame& frame,
                                         const std::optional<CircleNavigation>& circleNavigation = std::nullopt)
{
    PreparedShape shape;
    shape.vertices = std::move(vertices);
    shape.bounds = gridBounds(shape.vertices, frame);
    shape.circleNavigation = circleNavigation;
    shape.inclusion = inclusion;
    shape.edges.reserve(shape.vertices.size());
    for (std::size_t index = 0; index < shape.vertices.size(); ++index) {
        const Point& first = shape.vertices[index];
        const Point& second = shape.vertices[(index + 1) % shape.vertices.size()];
        shape.edges.push_back({first, second, gridBounds({first, second}, frame)});
    }
    return shape;
}

[[nodiscard]] bool prepareShapes(const PlannerInput& input, PreparedInput& prepared, std::string& error)
{
    const bool hasCornerEntry = input.entryPoint.has_value() || input.sweepDirection.has_value();
    const bool validCornerEntry = input.entryPoint && input.sweepDirection && finitePoint(*input.entryPoint) &&
                                  finitePoint(*input.sweepDirection) &&
                                  length(*input.sweepDirection) > DISTANCE_EPSILON;
    const PlannerCostModel& cost = input.costModel;
    if (input.inclusions.empty()) {
        error = "at least one inclusion shape is required";
        return false;
    }
    if (!finiteCoordinate(input.spacing) || input.spacing <= DISTANCE_EPSILON ||
        !finiteCoordinate(input.gridAngleDegrees) || std::abs(input.gridAngleDegrees) > 360.0 ||
        !finiteCoordinate(input.circleChordError) || input.circleChordError <= 0.0 ||
        !validEntryCorner(input.entryCorner) || (hasCornerEntry && !validCornerEntry) ||
        !finiteCoordinate(cost.spraySpeed) || cost.spraySpeed <= 0.0 || !finiteCoordinate(cost.transitSpeed) ||
        cost.transitSpeed <= 0.0 || !finiteCoordinate(cost.acceleration) || cost.acceleration <= 0.0 ||
        !finiteCoordinate(cost.yawRateDegrees) || cost.yawRateDegrees <= 0.0 ||
        !finiteCoordinate(cost.yawThresholdDegrees) || cost.yawThresholdDegrees < 0.0 ||
        !finiteCoordinate(cost.yawSettleSeconds) || cost.yawSettleSeconds < 0.0) {
        error = "spacing, direction, circle chord error, entry corner, or route cost model is invalid";
        return false;
    }
    if (input.inclusions.size() + input.exclusions.size() > input.limits.maxShapes || input.limits.maxShapes == 0 ||
        input.limits.maxShapeVertices < 3 || input.limits.maxScanLines == 0 || input.limits.maxSprayLegs == 0 ||
        input.limits.maxVisibilityNodes < 2 || input.limits.maxTopologyRepresentatives == 0 ||
        input.limits.maxRoutePoints == 0 || input.limits.maxCoverageCells == 0 || input.limits.exactCellLimit == 0 ||
        input.limits.exactCellLimit > 12) {
        error = "planner limits are invalid or shape count exceeds the limit";
        return false;
    }

    if (validCornerEntry) {
        const double directionLength = length(*input.sweepDirection);
        prepared.frame.direction = *input.sweepDirection * (1.0 / directionLength);
    } else {
        const double radians = input.gridAngleDegrees * std::numbers::pi / 180.0;
        prepared.frame.direction = {std::cos(radians), std::sin(radians)};
    }
    prepared.frame.normal = {-prepared.frame.direction.east, prepared.frame.direction.north};
    prepared.limits = input.limits;

    auto appendShapes = [&](const std::vector<Shape>& shapes, bool inclusion, std::vector<PreparedShape>& target) {
        for (const Shape& shape : shapes) {
            std::vector<Point> vertices;
            std::optional<CircleNavigation> circleNavigation;
            if (const auto* polygon = std::get_if<Polygon>(&shape)) {
                if (!validPolygon(*polygon, input.limits, error)) {
                    return false;
                }
                vertices = polygon->vertices;
            } else {
                Circle circle = std::get<Circle>(shape);
                if (!inclusion) {
                    circle.radius += OBSTACLE_CLEARANCE;
                }
                const auto approximation =
                    approximateCircle(circle, input.circleChordError, inclusion, prepared.frame, input.limits);
                if (!approximation) {
                    error = "circle is invalid or its conservative approximation exceeds the vertex limit";
                    return false;
                }
                vertices = *approximation;
                if (!inclusion) {
                    const double outerRadius = distance(circle.center, vertices.front());
                    circleNavigation = {{circle.center, outerRadius}};
                }
            }
            target.push_back(prepareShape(std::move(vertices), inclusion, prepared.frame, circleNavigation));
        }
        return true;
    };

    return appendShapes(input.inclusions, true, prepared.inclusions) &&
           appendShapes(input.exclusions, false, prepared.exclusions);
}

[[nodiscard]] bool pointInPolygon(const Point& point, const std::vector<Point>& polygon)
{
    bool inside = false;
    for (std::size_t index = 0, previous = polygon.size() - 1; index < polygon.size(); previous = index++) {
        const Point& current = polygon[index];
        const Point& prior = polygon[previous];
        if (pointOnSegment(point, prior, current)) {
            return true;
        }
        const bool straddles = (current.east > point.east) != (prior.east > point.east);
        if (straddles) {
            const double northAtCrossing =
                (prior.north - current.north) * (point.east - current.east) / (prior.east - current.east) +
                current.north;
            if (point.north < northAtCrossing) {
                inside = !inside;
            }
        }
    }
    return inside;
}

[[nodiscard]] bool boundsContain(const GridBounds& bounds, double along, double across)
{
    const double tolerance = coordinateTolerance(
        std::max({1.0, std::abs(along), std::abs(across), std::abs(bounds.minimumAlong), std::abs(bounds.maximumAlong),
                  std::abs(bounds.minimumAcross), std::abs(bounds.maximumAcross)}));
    return along >= bounds.minimumAlong - tolerance && along <= bounds.maximumAlong + tolerance &&
           across >= bounds.minimumAcross - tolerance && across <= bounds.maximumAcross + tolerance;
}

[[nodiscard]] bool boundsCrossScanline(const GridBounds& bounds, double across)
{
    const double tolerance = coordinateTolerance(
        std::max({1.0, std::abs(across), std::abs(bounds.minimumAcross), std::abs(bounds.maximumAcross)}));
    return across >= bounds.minimumAcross - tolerance && across <= bounds.maximumAcross + tolerance;
}

[[nodiscard]] bool boundsOverlap(const GridBounds& first, const GridBounds& second)
{
    const double tolerance = coordinateTolerance(
        std::max({1.0, std::abs(first.minimumAlong), std::abs(first.maximumAlong), std::abs(first.minimumAcross),
                  std::abs(first.maximumAcross), std::abs(second.minimumAlong), std::abs(second.maximumAlong),
                  std::abs(second.minimumAcross), std::abs(second.maximumAcross)}));
    return first.minimumAlong <= second.maximumAlong + tolerance &&
           first.maximumAlong + tolerance >= second.minimumAlong &&
           first.minimumAcross <= second.maximumAcross + tolerance &&
           first.maximumAcross + tolerance >= second.minimumAcross;
}

[[nodiscard]] bool pointInEffectiveRegion(const Point& point, const PreparedInput& prepared)
{
    if (!finitePoint(point)) {
        return false;
    }

    const double along = prepared.frame.along(point);
    const double across = prepared.frame.across(point);
    const bool included = std::any_of(
        prepared.inclusions.begin(), prepared.inclusions.end(), [&point, along, across](const PreparedShape& shape) {
            return boundsContain(shape.bounds, along, across) && pointInPolygon(point, shape.vertices);
        });
    if (!included) {
        return false;
    }

    return std::none_of(prepared.exclusions.begin(), prepared.exclusions.end(),
                        [&point, along, across](const PreparedShape& shape) {
                            return boundsContain(shape.bounds, along, across) && pointInPolygon(point, shape.vertices);
                        });
}

[[nodiscard]] std::vector<Interval> normalizeIntervals(std::vector<Interval> intervals)
{
    std::sort(intervals.begin(), intervals.end(), [](const Interval& left, const Interval& right) {
        return left.start == right.start ? left.end < right.end : left.start < right.start;
    });

    std::vector<Interval> normalized;
    for (const Interval& interval : intervals) {
        // A zero-width tangency has no interior, but even a very narrow positive
        // exclusion chord must remain available to the subtraction below.
        if (interval.end <= interval.start) {
            continue;
        }
        // Do not use an epsilon to bridge a positive gap. Scanline intervals can
        // describe distinct effective regions whose separation is small but real.
        if (normalized.empty() || interval.start > normalized.back().end) {
            normalized.push_back(interval);
        } else {
            normalized.back().end = std::max(normalized.back().end, interval.end);
        }
    }
    return normalized;
}

[[nodiscard]] std::optional<std::vector<Interval>> polygonIntervals(const PreparedShape& shape, const GridFrame& frame,
                                                                    double across)
{
    if (!boundsCrossScanline(shape.bounds, across)) {
        return std::vector<Interval>{};
    }

    std::vector<double> intersections;
    intersections.reserve(shape.edges.size());
    for (const PreparedEdge& edge : shape.edges) {
        if (!boundsCrossScanline(edge.bounds, across)) {
            continue;
        }
        const Point& first = edge.first;
        const Point& second = edge.second;
        const double firstAcross = frame.across(first);
        const double secondAcross = frame.across(second);
        const bool crosses =
            (firstAcross <= across && secondAcross > across) || (secondAcross <= across && firstAcross > across);
        if (crosses) {
            const double fraction = (across - firstAcross) / (secondAcross - firstAcross);
            intersections.push_back(frame.along(first + (second - first) * fraction));
        }
    }

    std::sort(intersections.begin(), intersections.end());
    if (intersections.size() % 2 != 0) {
        return std::nullopt;
    }

    std::vector<Interval> intervals;
    intervals.reserve(intersections.size() / 2);
    for (std::size_t index = 0; index < intersections.size(); index += 2) {
        intervals.push_back({intersections[index], intersections[index + 1]});
    }
    return normalizeIntervals(std::move(intervals));
}

[[nodiscard]] bool scanlineContains(double value, double across)
{
    const double scale = std::max({1.0, std::abs(value), std::abs(across)});
    const double tolerance =
        std::max(DISTANCE_EPSILON, ROUNDING_SAFETY_FACTOR * std::numeric_limits<double>::epsilon() * scale);
    return std::abs(value - across) <= tolerance;
}

[[nodiscard]] Interval expandedBoundaryInterval(double first, double second)
{
    return {std::nextafter(std::min(first, second), -std::numeric_limits<double>::infinity()),
            std::nextafter(std::max(first, second), std::numeric_limits<double>::infinity())};
}

[[nodiscard]] std::optional<std::vector<Interval>> exclusionIntervals(const PreparedShape& shape,
                                                                      const GridFrame& frame, double across)
{
    const auto interiorIntervals = polygonIntervals(shape, frame, across);
    if (!interiorIntervals) {
        return std::nullopt;
    }

    std::vector<Interval> intervals = *interiorIntervals;
    intervals.reserve(intervals.size() + shape.edges.size() * 2);
    for (const PreparedEdge& edge : shape.edges) {
        if (!boundsCrossScanline(edge.bounds, across)) {
            continue;
        }
        const Point& first = edge.first;
        const Point& second = edge.second;
        const double firstAcross = frame.across(first);
        const double secondAcross = frame.across(second);
        const double firstAlong = frame.along(first);
        const double secondAlong = frame.along(second);

        // Polygon interiors use the half-open conversion above. Exclusions also
        // block their closed boundary, so a tangent vertex cannot join two legs.
        if (scanlineContains(firstAcross, across)) {
            intervals.push_back(expandedBoundaryInterval(firstAlong, firstAlong));
        }
        if (scanlineContains(firstAcross, across) && scanlineContains(secondAcross, across)) {
            intervals.push_back(expandedBoundaryInterval(firstAlong, secondAlong));
        }
    }
    return normalizeIntervals(std::move(intervals));
}

[[nodiscard]] std::optional<std::vector<Interval>> effectiveIntervals(const PreparedInput& prepared, double across)
{
    std::vector<Interval> included;
    std::vector<Interval> excluded;
    for (const PreparedShape& shape : prepared.inclusions) {
        const auto intervals = polygonIntervals(shape, prepared.frame, across);
        if (!intervals) {
            return std::nullopt;
        }
        included.insert(included.end(), intervals->begin(), intervals->end());
    }
    for (const PreparedShape& shape : prepared.exclusions) {
        const auto intervals = exclusionIntervals(shape, prepared.frame, across);
        if (!intervals) {
            return std::nullopt;
        }
        excluded.insert(excluded.end(), intervals->begin(), intervals->end());
    }

    included = normalizeIntervals(std::move(included));
    excluded = normalizeIntervals(std::move(excluded));
    std::vector<Interval> result;
    for (const Interval& inclusion : included) {
        double cursor = inclusion.start;
        for (const Interval& exclusion : excluded) {
            if (exclusion.end <= cursor) {
                continue;
            }
            if (exclusion.start >= inclusion.end) {
                break;
            }
            if (exclusion.start > cursor) {
                result.push_back({cursor, std::min(inclusion.end, exclusion.start)});
            }
            cursor = std::max(cursor, exclusion.end);
            if (cursor >= inclusion.end) {
                break;
            }
        }
        if (cursor < inclusion.end) {
            result.push_back({cursor, inclusion.end});
        }
    }
    return result;
}

[[nodiscard]] ScanRowsResult buildScanRows(const PreparedInput& prepared, double spacing)
{
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const PreparedShape& shape : prepared.inclusions) {
        for (const Point& vertex : shape.vertices) {
            minimum = std::min(minimum, prepared.frame.across(vertex));
            maximum = std::max(maximum, prepared.frame.across(vertex));
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum < minimum) {
        return {};
    }

    const double width = maximum - minimum;
    const double countValue = std::max(1.0, std::ceil(width / spacing));
    if (!std::isfinite(countValue) || countValue > static_cast<double>(prepared.limits.maxScanLines)) {
        return {ScanRowsStatus::ComplexityLimit, {}};
    }
    const std::size_t count = static_cast<std::size_t>(countValue);
    if (count == 0 || count > prepared.limits.maxScanLines) {
        return {ScanRowsStatus::ComplexityLimit, {}};
    }

    const double first =
        count == 1 ? (minimum + maximum) * 0.5 : minimum + (width - static_cast<double>(count - 1) * spacing) * 0.5;
    std::vector<ScanRow> rows;
    rows.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const double across = first + static_cast<double>(index) * spacing;
        const auto intervals = effectiveIntervals(prepared, across);
        if (!intervals) {
            return {};
        }
        rows.push_back({across, *intervals});
    }
    return {ScanRowsStatus::Found, std::move(rows)};
}

[[nodiscard]] std::vector<double> segmentBoundaryParameters(const Point& start, const Point& end, const Point& first,
                                                            const Point& second)
{
    const Point direction = end - start;
    const Point edge = second - first;
    const double denominator = cross(direction, edge);
    const double sourceMagnitude = coordinateMagnitude({start, end, first, second});
    std::vector<double> parameters;
    if (std::abs(denominator) > crossTolerance(direction, edge, sourceMagnitude)) {
        const Point delta = first - start;
        const double segmentParameter = cross(delta, edge) / denominator;
        const double edgeParameter = cross(delta, direction) / denominator;
        if (segmentParameter >= -PARAMETER_EPSILON && segmentParameter <= 1.0 + PARAMETER_EPSILON &&
            edgeParameter >= -PARAMETER_EPSILON && edgeParameter <= 1.0 + PARAMETER_EPSILON) {
            parameters.push_back(std::clamp(segmentParameter, 0.0, 1.0));
        }
    } else if (std::abs(cross(first - start, direction)) <= crossTolerance(first - start, direction, sourceMagnitude)) {
        const double squaredLength = dot(direction, direction);
        if (squaredLength > DISTANCE_EPSILON * DISTANCE_EPSILON) {
            for (const Point& point : {first, second}) {
                const double parameter = dot(point - start, direction) / squaredLength;
                if (parameter >= -PARAMETER_EPSILON && parameter <= 1.0 + PARAMETER_EPSILON) {
                    parameters.push_back(std::clamp(parameter, 0.0, 1.0));
                }
            }
        }
    }
    return parameters;
}

[[nodiscard]] bool segmentInEffectiveRegion(const Point& start, const Point& end, const PreparedInput& prepared)
{
    if (!pointInEffectiveRegion(start, prepared) || !pointInEffectiveRegion(end, prepared)) {
        return false;
    }

    const GridBounds segmentBounds = gridBounds({start, end}, prepared.frame);
    std::vector<double> parameters{0.0, 1.0};
    const auto appendBoundaryParameters = [&](const std::vector<PreparedShape>& shapes) {
        for (const PreparedShape& shape : shapes) {
            if (!boundsOverlap(segmentBounds, shape.bounds)) {
                continue;
            }
            for (const PreparedEdge& edge : shape.edges) {
                if (!boundsOverlap(segmentBounds, edge.bounds)) {
                    continue;
                }
                const auto edgeParameters = segmentBoundaryParameters(start, end, edge.first, edge.second);
                parameters.insert(parameters.end(), edgeParameters.begin(), edgeParameters.end());
            }
        }
    };
    appendBoundaryParameters(prepared.inclusions);
    appendBoundaryParameters(prepared.exclusions);
    std::sort(parameters.begin(), parameters.end());
    parameters.erase(std::unique(parameters.begin(), parameters.end(),
                                 [](double left, double right) { return std::abs(left - right) <= PARAMETER_EPSILON; }),
                     parameters.end());

    const Point direction = end - start;
    for (const double parameter : parameters) {
        // Midpoint tests alone miss a tangential touch. A boundary point is
        // traversable only when it remains in the effective region, which
        // rejects exclusion boundaries and hole vertices.
        if (!pointInEffectiveRegion(start + direction * parameter, prepared)) {
            return false;
        }
    }
    for (std::size_t index = 1; index < parameters.size(); ++index) {
        const double before = parameters[index - 1];
        const double after = parameters[index];
        if (after - before <= PARAMETER_EPSILON) {
            continue;
        }
        const Point midpoint = start + direction * ((before + after) * 0.5);
        if (!pointInEffectiveRegion(midpoint, prepared)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool addUniqueNode(std::vector<Point>& nodes, const Point& point, const PreparedInput& prepared)
{
    if (!pointInEffectiveRegion(point, prepared)) {
        return true;
    }
    const bool alreadyPresent =
        std::any_of(nodes.begin(), nodes.end(), [&point](const Point& existing) { return samePoint(existing, point); });
    if (alreadyPresent) {
        return true;
    }
    if (nodes.size() >= prepared.limits.maxVisibilityNodes) {
        return false;
    }
    nodes.push_back(point);
    return true;
}

[[nodiscard]] bool addExclusionNavigationNodes(std::vector<Point>& nodes, const PreparedShape& shape,
                                               const PreparedInput& prepared)
{
    if (shape.circleNavigation) {
        const double clearance = std::max(1e-7, shape.circleNavigation->radius * 1e-8);
        for (const Point& vertex : shape.vertices) {
            const Point radial = vertex - shape.circleNavigation->center;
            const double radialLength = length(radial);
            if (radialLength <= DISTANCE_EPSILON) {
                return false;
            }
            const Point navigationNode = vertex + radial * (clearance / radialLength);
            if (!addUniqueNode(nodes, navigationNode, prepared)) {
                return false;
            }
        }
        return true;
    }

    Point center;
    for (const Point& point : shape.vertices) {
        center = center + point;
    }
    center = center * (1.0 / static_cast<double>(shape.vertices.size()));

    const double clearance = OBSTACLE_CLEARANCE;
    const double area = signedArea(shape.vertices);

    for (std::size_t index = 0; index < shape.vertices.size(); ++index) {
        const Point& previous = shape.vertices[(index + shape.vertices.size() - 1) % shape.vertices.size()];
        const Point& current = shape.vertices[index];
        const Point& next = shape.vertices[(index + 1) % shape.vertices.size()];
        const Point previousEdge = current - previous;
        const Point nextEdge = next - current;
        const double sign = area >= 0.0 ? 1.0 : -1.0;
        const Point firstOutward = {sign * previousEdge.east, -sign * previousEdge.north};
        const Point secondOutward = {sign * nextEdge.east, -sign * nextEdge.north};
        const Point radial = current - center;
        const Point previousUnit = previousEdge * (1.0 / length(previousEdge));
        const Point nextUnit = nextEdge * (1.0 / length(nextEdge));
        const Point firstOutwardUnit = firstOutward * (1.0 / length(firstOutward));
        const Point secondOutwardUnit = secondOutward * (1.0 / length(secondOutward));
        for (const Point& direction : {firstOutward, secondOutward, firstOutward + secondOutward, radial,
                                       firstOutwardUnit - previousUnit, secondOutwardUnit + nextUnit}) {
            const double directionLength = length(direction);
            if (directionLength > DISTANCE_EPSILON) {
                if (!addUniqueNode(nodes, current + direction * (clearance / directionLength), prepared)) {
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] std::optional<Roadmap> buildRoadmap(const PreparedInput& prepared)
{
    Roadmap roadmap;
    roadmap.nodes.reserve(prepared.limits.maxVisibilityNodes);

    for (const PreparedShape& shape : prepared.inclusions) {
        for (const Point& point : shape.vertices) {
            if (!addUniqueNode(roadmap.nodes, point, prepared)) {
                return std::nullopt;
            }
        }
    }
    for (const PreparedShape& shape : prepared.exclusions) {
        if (!addExclusionNavigationNodes(roadmap.nodes, shape, prepared)) {
            return std::nullopt;
        }
    }

    std::sort(roadmap.nodes.begin(), roadmap.nodes.end(), [](const Point& left, const Point& right) {
        return left.north == right.north ? left.east < right.east : left.north < right.north;
    });

    const std::size_t nodeCount = roadmap.nodes.size();
    roadmap.edges.resize(nodeCount);
    roadmap.components.resize(nodeCount);
    for (std::size_t index = 0; index < nodeCount; ++index) {
        roadmap.components[index] = index;
    }
    const auto findComponent = [&roadmap](std::size_t node) {
        std::size_t root = node;
        while (roadmap.components[root] != root) {
            root = roadmap.components[root];
        }
        while (roadmap.components[node] != node) {
            const std::size_t parent = roadmap.components[node];
            roadmap.components[node] = root;
            node = parent;
        }
        return root;
    };
    const auto joinComponents = [&findComponent, &roadmap](std::size_t first, std::size_t second) {
        const std::size_t firstRoot = findComponent(first);
        const std::size_t secondRoot = findComponent(second);
        if (firstRoot != secondRoot) {
            roadmap.components[secondRoot] = firstRoot;
        }
    };
    for (std::size_t first = 0; first < nodeCount; ++first) {
        for (std::size_t second = first + 1; second < nodeCount; ++second) {
            if (!segmentInEffectiveRegion(roadmap.nodes[first], roadmap.nodes[second], prepared)) {
                continue;
            }
            roadmap.edges[first].push_back(second);
            roadmap.edges[second].push_back(first);
            joinComponents(first, second);
        }
    }
    for (std::size_t index = 0; index < nodeCount; ++index) {
        roadmap.components[index] = findComponent(index);
    }
    return roadmap;
}

[[nodiscard]] bool advancesFromStart(const Point& start, const Point& point,
                                     const std::optional<Point>& initialDirection)
{
    if (!initialDirection) {
        return true;
    }

    const double initialDirectionLength = length(*initialDirection);
    return initialDirectionLength <= DISTANCE_EPSILON ||
           dot(point - start, *initialDirection) >= -OBSTACLE_CLEARANCE * initialDirectionLength;
}

[[nodiscard]] TransitResult findTransit(const Point& start, const Point& end, const PreparedInput& prepared,
                                        const Roadmap& roadmap,
                                        const std::optional<Point>& initialDirection = std::nullopt)
{
    if (!pointInEffectiveRegion(start, prepared) || !pointInEffectiveRegion(end, prepared)) {
        return {};
    }
    if (segmentInEffectiveRegion(start, end, prepared)) {
        return {TransitStatus::Found, {}};
    }

    const std::size_t nodeCount = roadmap.nodes.size();
    if (nodeCount == 0) {
        return {};
    }

    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> costs(nodeCount, infinity);
    std::vector<std::size_t> parent(nodeCount, nodeCount);
    std::vector<bool> visited(nodeCount, false);
    std::vector<bool> reachesEnd(nodeCount, false);
    for (std::size_t node = 0; node < nodeCount; ++node) {
        if (advancesFromStart(start, roadmap.nodes[node], initialDirection) &&
            segmentInEffectiveRegion(start, roadmap.nodes[node], prepared)) {
            costs[node] = distance(start, roadmap.nodes[node]);
        }
        reachesEnd[node] = segmentInEffectiveRegion(roadmap.nodes[node], end, prepared);
    }

    for (std::size_t iteration = 0; iteration < nodeCount; ++iteration) {
        std::size_t current = nodeCount;
        for (std::size_t node = 0; node < nodeCount; ++node) {
            if (!visited[node] && (current == nodeCount || costs[node] < costs[current] ||
                                   (costs[node] == costs[current] && node < current))) {
                current = node;
            }
        }
        if (current == nodeCount || !std::isfinite(costs[current])) {
            break;
        }
        visited[current] = true;
        for (const std::size_t next : roadmap.edges[current]) {
            if (visited[next]) {
                continue;
            }
            const double candidate = costs[current] + distance(roadmap.nodes[current], roadmap.nodes[next]);
            if (candidate + DISTANCE_EPSILON < costs[next]) {
                costs[next] = candidate;
                parent[next] = current;
            }
        }
    }

    std::size_t destination = nodeCount;
    double destinationCost = infinity;
    for (std::size_t node = 0; node < nodeCount; ++node) {
        if (!reachesEnd[node]) {
            continue;
        }
        const double candidate = costs[node] + distance(roadmap.nodes[node], end);
        if (candidate + DISTANCE_EPSILON < destinationCost ||
            (std::abs(candidate - destinationCost) <= DISTANCE_EPSILON && node < destination)) {
            destination = node;
            destinationCost = candidate;
        }
    }
    if (destination == nodeCount || !std::isfinite(destinationCost)) {
        return {};
    }

    std::vector<Point> reversePath;
    for (std::size_t current = destination; current != nodeCount; current = parent[current]) {
        reversePath.push_back(roadmap.nodes[current]);
    }
    std::reverse(reversePath.begin(), reversePath.end());
    if (!reversePath.empty() && distance(reversePath.front(), start) <= DISTANCE_EPSILON) {
        reversePath.erase(reversePath.begin());
    }
    if (!reversePath.empty() && distance(reversePath.back(), end) <= DISTANCE_EPSILON) {
        reversePath.pop_back();
    }
    return {TransitStatus::Found, std::move(reversePath)};
}

[[nodiscard]] std::optional<std::vector<Point>> simplifyTransitPoints(const std::vector<Point>& transitPoints,
                                                                      const Point& start, const Point& end,
                                                                      const PreparedInput& prepared,
                                                                      const std::optional<Point>& initialDirection)
{
    std::vector<Point> simplified;
    Point anchor = start;
    std::size_t next = 0;
    while (next < transitPoints.size()) {
        if (segmentInEffectiveRegion(anchor, end, prepared)) {
            return simplified;
        }

        std::size_t farthest = transitPoints.size();
        for (std::size_t index = transitPoints.size(); index-- > next;) {
            if (segmentInEffectiveRegion(anchor, transitPoints[index], prepared) &&
                (next != 0 || advancesFromStart(start, transitPoints[index], initialDirection))) {
                farthest = index;
                break;
            }
        }
        if (farthest == transitPoints.size()) {
            return std::nullopt;
        }

        simplified.push_back(transitPoints[farthest]);
        anchor = transitPoints[farthest];
        next = farthest + 1;
    }

    if (!segmentInEffectiveRegion(anchor, end, prepared)) {
        return std::nullopt;
    }
    return simplified;
}

enum class CoverageStatus
{
    Connected,
    Disconnected,
    InvalidGeometry,
    ComplexityLimit,
    Empty,
};

struct CoverageResult
{
    CoverageStatus status = CoverageStatus::InvalidGeometry;
};

[[nodiscard]] CoverageResult coverageIsConnected(const PreparedInput& prepared, const Roadmap& roadmap,
                                                 const std::vector<SprayLeg>& legs)
{
    std::vector<Point> representatives;
    representatives.reserve(legs.size());
    const auto appendRepresentative = [&representatives, &prepared](const Point& point) {
        if (!finitePoint(point) || representatives.size() >= prepared.limits.maxTopologyRepresentatives) {
            return false;
        }
        representatives.push_back(point);
        return true;
    };
    for (const SprayLeg& leg : legs) {
        if (!appendRepresentative((leg.start + leg.end) * 0.5)) {
            return {CoverageStatus::ComplexityLimit};
        }
    }

    std::vector<double> crossings;
    const auto appendCrossings = [&crossings, &prepared](const std::vector<PreparedShape>& shapes) {
        for (const PreparedShape& shape : shapes) {
            for (const Point& point : shape.vertices) {
                crossings.push_back(prepared.frame.across(point));
            }
        }
    };
    appendCrossings(prepared.inclusions);
    appendCrossings(prepared.exclusions);
    std::sort(crossings.begin(), crossings.end());
    crossings.erase(std::unique(crossings.begin(), crossings.end(),
                                [](double left, double right) {
                                    return std::abs(left - right) <=
                                           coordinateTolerance(std::max({1.0, std::abs(left), std::abs(right)}));
                                }),
                    crossings.end());
    for (std::size_t index = 1; index < crossings.size(); ++index) {
        const double across = (crossings[index - 1] + crossings[index]) * 0.5;
        const std::optional<std::vector<Interval>> intervals = effectiveIntervals(prepared, across);
        if (!intervals) {
            return {CoverageStatus::InvalidGeometry};
        }
        for (const Interval& interval : *intervals) {
            if (!appendRepresentative(prepared.frame.point((interval.start + interval.end) * 0.5, across))) {
                return {CoverageStatus::ComplexityLimit};
            }
        }
    }
    std::sort(representatives.begin(), representatives.end(), pointLexicographicallyLess);
    representatives.erase(std::unique(representatives.begin(), representatives.end(), samePoint),
                          representatives.end());
    if (representatives.empty()) {
        return {CoverageStatus::Empty};
    }
    if (roadmap.nodes.empty()) {
        return {CoverageStatus::Disconnected};
    }

    std::vector<std::size_t> components = roadmap.components;
    const auto findComponent = [&components](std::size_t node) {
        std::size_t root = node;
        while (components[root] != root) {
            root = components[root];
        }
        while (components[node] != node) {
            const std::size_t parent = components[node];
            components[node] = root;
            node = parent;
        }
        return root;
    };
    const auto joinComponents = [&components, &findComponent](std::size_t first, std::size_t second) {
        const std::size_t firstRoot = findComponent(first);
        const std::size_t secondRoot = findComponent(second);
        if (firstRoot != secondRoot) {
            components[secondRoot] = firstRoot;
        }
    };

    std::vector<std::size_t> representativeComponents;
    representativeComponents.reserve(representatives.size());
    for (const Point& representative : representatives) {
        std::size_t firstVisible = roadmap.nodes.size();
        for (std::size_t node = 0; node < roadmap.nodes.size(); ++node) {
            if (firstVisible != roadmap.nodes.size() && findComponent(node) == findComponent(firstVisible)) {
                continue;
            }
            if (!segmentInEffectiveRegion(representative, roadmap.nodes[node], prepared)) {
                continue;
            }
            if (firstVisible == roadmap.nodes.size()) {
                firstVisible = node;
            } else {
                joinComponents(firstVisible, node);
            }
        }
        if (firstVisible == roadmap.nodes.size()) {
            return {CoverageStatus::Disconnected};
        }
        representativeComponents.push_back(firstVisible);
    }

    const std::size_t firstComponent = findComponent(representativeComponents.front());
    const bool connected = std::all_of(
        representativeComponents.begin(), representativeComponents.end(),
        [&findComponent, firstComponent](std::size_t component) { return findComponent(component) == firstComponent; });
    return {connected ? CoverageStatus::Connected : CoverageStatus::Disconnected};
}

struct IndexedLeg
{
    SprayLeg leg;
    std::size_t rowIndex = 0;
    double minimumAlong = 0.0;
    double maximumAlong = 0.0;
};

struct RouteCandidate
{
    std::vector<SprayLeg> legs;
    std::vector<RoutePoint> route;
    double distance = 0.0;
    double sprayDistance = 0.0;
    double transitDistance = 0.0;
    double estimatedTime = 0.0;
    std::size_t turnCount = 0;
    bool valid = false;
};

[[nodiscard]] double pointToSegmentDistance(const Point& point, const Point& start, const Point& end)
{
    const Point segment = end - start;
    const double squaredLength = dot(segment, segment);
    const double parameter = squaredLength > DISTANCE_EPSILON * DISTANCE_EPSILON
                                 ? std::clamp(dot(point - start, segment) / squaredLength, 0.0, 1.0)
                                 : 0.0;
    return distance(point, start + segment * parameter);
}

[[nodiscard]] double segmentHeading(const Point& start, const Point& end)
{
    const Point direction = end - start;
    return std::atan2(direction.east, direction.north);
}

[[nodiscard]] double headingDeltaDegrees(double first, double second)
{
    double delta = std::remainder((second - first) * 180.0 / std::numbers::pi, 360.0);
    if (delta < -180.0) {
        delta += 360.0;
    } else if (delta > 180.0) {
        delta -= 360.0;
    }
    return std::abs(delta);
}

[[nodiscard]] double segmentTravelTime(double segmentLength, double speed, double acceleration)
{
    if (segmentLength <= DISTANCE_EPSILON) {
        return 0.0;
    }
    const double accelerationDistance = speed * speed / acceleration;
    if (segmentLength <= accelerationDistance) {
        return 2.0 * std::sqrt(segmentLength / acceleration);
    }
    return 2.0 * speed / acceleration + (segmentLength - accelerationDistance) / speed;
}

[[nodiscard]] double turnTime(double firstHeading, double secondHeading, const PlannerCostModel& model,
                              std::size_t& turnCount)
{
    const double delta = headingDeltaDegrees(firstHeading, secondHeading);
    if (delta <= model.yawThresholdDegrees) {
        return 0.0;
    }
    ++turnCount;
    return delta / model.yawRateDegrees + model.yawSettleSeconds;
}

[[nodiscard]] bool appendRoutePoint(std::vector<RoutePoint>& route, const Point& point, RoutePointType type)
{
    if (!route.empty() && samePoint(route.back().position, point)) {
        if (route.back().type == RoutePointType::Transit && type == RoutePointType::SprayStart) {
            route.back().type = type;
            return true;
        }
        return route.back().type == type;
    }
    route.push_back({point, type});
    return true;
}

void calculateRouteMetrics(RouteCandidate& candidate, const PreparedInput& prepared, const PlannerCostModel& model)
{
    candidate.distance = 0.0;
    candidate.sprayDistance = 0.0;
    candidate.transitDistance = 0.0;
    candidate.estimatedTime = 0.0;
    candidate.turnCount = 0;
    candidate.valid = candidate.route.size() >= 2 && !candidate.legs.empty();
    std::optional<double> previousHeading;
    for (std::size_t index = 1; index < candidate.route.size(); ++index) {
        const Point& start = candidate.route[index - 1].position;
        const Point& end = candidate.route[index].position;
        const double segmentLength = distance(start, end);
        if (segmentLength <= DISTANCE_EPSILON || !segmentInEffectiveRegion(start, end, prepared)) {
            candidate.valid = false;
            return;
        }
        const bool spray = candidate.route[index - 1].type == RoutePointType::SprayStart &&
                           candidate.route[index].type == RoutePointType::SprayEnd;
        const double heading = segmentHeading(start, end);
        if (previousHeading) {
            candidate.estimatedTime += turnTime(*previousHeading, heading, model, candidate.turnCount);
        }
        candidate.estimatedTime +=
            segmentTravelTime(segmentLength, spray ? model.spraySpeed : model.transitSpeed, model.acceleration);
        candidate.distance += segmentLength;
        if (spray) {
            candidate.sprayDistance += segmentLength;
        } else {
            candidate.transitDistance += segmentLength;
        }
        previousHeading = heading;
    }
    double expectedSprayDistance = 0.0;
    for (const SprayLeg& leg : candidate.legs) {
        expectedSprayDistance += distance(leg.start, leg.end);
    }
    const double tolerance = coordinateTolerance(std::max(1.0, expectedSprayDistance));
    candidate.valid = std::abs(candidate.sprayDistance - expectedSprayDistance) <= tolerance;
}

[[nodiscard]] std::optional<std::vector<Point>> connectorPath(const Point& start, const Point& end,
                                                              const PreparedInput& prepared, const Roadmap& roadmap,
                                                              const std::optional<Point>& initialDirection)
{
    if (samePoint(start, end)) {
        return std::vector<Point>{start};
    }
    const TransitResult transit = findTransit(start, end, prepared, roadmap, initialDirection);
    if (transit.status != TransitStatus::Found) {
        return std::nullopt;
    }
    const auto simplified = simplifyTransitPoints(transit.points, start, end, prepared, initialDirection);
    if (!simplified) {
        return std::nullopt;
    }
    std::vector<Point> path;
    path.reserve(simplified->size() + 2);
    path.push_back(start);
    path.insert(path.end(), simplified->begin(), simplified->end());
    path.push_back(end);
    return path;
}

[[nodiscard]] RouteCandidate buildRouteCandidate(std::vector<SprayLeg> legs, const std::optional<Point>& entryPoint,
                                                 const PreparedInput& prepared, const Roadmap& roadmap,
                                                 const PlannerCostModel& model)
{
    RouteCandidate candidate;
    candidate.legs = std::move(legs);
    if (candidate.legs.empty()) {
        return candidate;
    }

    if (entryPoint) {
        const auto entryPath =
            connectorPath(*entryPoint, candidate.legs.front().start, prepared, roadmap, std::nullopt);
        if (!entryPath || !appendRoutePoint(candidate.route, *entryPoint, RoutePointType::Transit)) {
            return candidate;
        }
        for (std::size_t index = 1; index + 1 < entryPath->size(); ++index) {
            if (!appendRoutePoint(candidate.route, (*entryPath)[index], RoutePointType::Transit)) {
                return candidate;
            }
        }
    }

    if (!appendRoutePoint(candidate.route, candidate.legs.front().start, RoutePointType::SprayStart) ||
        !appendRoutePoint(candidate.route, candidate.legs.front().end, RoutePointType::SprayEnd)) {
        return candidate;
    }
    for (std::size_t index = 1; index < candidate.legs.size(); ++index) {
        const Point previousDirection = candidate.legs[index - 1].end - candidate.legs[index - 1].start;
        const auto path = connectorPath(candidate.legs[index - 1].end, candidate.legs[index].start, prepared, roadmap,
                                        previousDirection);
        if (!path) {
            return candidate;
        }
        for (std::size_t pathIndex = 1; pathIndex + 1 < path->size(); ++pathIndex) {
            if (!appendRoutePoint(candidate.route, (*path)[pathIndex], RoutePointType::Transit)) {
                return candidate;
            }
        }
        if (!appendRoutePoint(candidate.route, candidate.legs[index].start, RoutePointType::SprayStart) ||
            !appendRoutePoint(candidate.route, candidate.legs[index].end, RoutePointType::SprayEnd)) {
            return candidate;
        }
        if (candidate.route.size() > prepared.limits.maxRoutePoints) {
            return candidate;
        }
    }
    calculateRouteMetrics(candidate, prepared, model);
    return candidate;
}

[[nodiscard]] std::vector<SprayLeg> orderedLegs(const std::vector<ScanRow>& rows, const PreparedInput& prepared,
                                                bool reverseRows, bool startFromMaximumAlong)
{
    std::vector<SprayLeg> legs;
    for (std::size_t orderedRow = 0; orderedRow < rows.size(); ++orderedRow) {
        const std::size_t rowIndex = reverseRows ? rows.size() - 1 - orderedRow : orderedRow;
        const ScanRow& row = rows[rowIndex];
        const bool reverseLegs = startFromMaximumAlong != (orderedRow % 2 != 0);
        for (std::size_t orderedInterval = 0; orderedInterval < row.intervals.size(); ++orderedInterval) {
            const std::size_t intervalIndex =
                reverseLegs ? row.intervals.size() - 1 - orderedInterval : orderedInterval;
            const Interval& interval = row.intervals[intervalIndex];
            const double endpointInset = std::min(BOUNDARY_INSET, (interval.end - interval.start) * 0.25);
            SprayLeg leg{prepared.frame.point(interval.start + endpointInset, row.across),
                         prepared.frame.point(interval.end - endpointInset, row.across)};
            if (reverseLegs) {
                std::swap(leg.start, leg.end);
            }
            if (distance(leg.start, leg.end) > DISTANCE_EPSILON) {
                legs.push_back(leg);
            }
        }
    }
    return legs;
}

[[nodiscard]] std::vector<IndexedLeg> indexedLegs(const std::vector<ScanRow>& rows, const PreparedInput& prepared)
{
    std::vector<IndexedLeg> result;
    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const ScanRow& row = rows[rowIndex];
        for (const Interval& interval : row.intervals) {
            const double endpointInset = std::min(BOUNDARY_INSET, (interval.end - interval.start) * 0.25);
            if (interval.end - interval.start <= endpointInset * 2.0) {
                continue;
            }
            result.push_back({{prepared.frame.point(interval.start + endpointInset, row.across),
                               prepared.frame.point(interval.end - endpointInset, row.across)},
                              rowIndex,
                              interval.start + endpointInset,
                              interval.end - endpointInset});
        }
    }
    return result;
}

class DisjointSet
{
public:
    explicit DisjointSet(std::size_t size) : _parent(size) { std::iota(_parent.begin(), _parent.end(), 0); }

    [[nodiscard]] std::size_t find(std::size_t item)
    {
        if (_parent[item] != item) {
            _parent[item] = find(_parent[item]);
        }
        return _parent[item];
    }

    void join(std::size_t first, std::size_t second)
    {
        first = find(first);
        second = find(second);
        if (first != second) {
            _parent[second] = first;
        }
    }

private:
    std::vector<std::size_t> _parent;
};

[[nodiscard]] std::optional<std::vector<std::vector<IndexedLeg>>> buildCoverageCells(const std::vector<ScanRow>& rows,
                                                                                     const PreparedInput& prepared)
{
    std::vector<IndexedLeg> legs = indexedLegs(rows, prepared);
    if (legs.empty()) {
        return std::vector<std::vector<IndexedLeg>>{};
    }
    std::vector<std::vector<std::size_t>> nodesByRow(rows.size());
    for (std::size_t index = 0; index < legs.size(); ++index) {
        nodesByRow[legs[index].rowIndex].push_back(index);
    }
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    std::vector<std::size_t> outgoing(legs.size(), 0);
    std::vector<std::size_t> incoming(legs.size(), 0);
    for (std::size_t rowIndex = 1; rowIndex < rows.size(); ++rowIndex) {
        for (const std::size_t previous : nodesByRow[rowIndex - 1]) {
            for (const std::size_t current : nodesByRow[rowIndex]) {
                const double overlap = std::min(legs[previous].maximumAlong, legs[current].maximumAlong) -
                                       std::max(legs[previous].minimumAlong, legs[current].minimumAlong);
                if (overlap > coordinateTolerance(std::max({1.0, std::abs(legs[previous].maximumAlong),
                                                            std::abs(legs[current].maximumAlong)}))) {
                    edges.emplace_back(previous, current);
                    ++outgoing[previous];
                    ++incoming[current];
                }
            }
        }
    }
    DisjointSet sets(legs.size());
    for (const auto& [previous, current] : edges) {
        if (outgoing[previous] == 1 && incoming[current] == 1) {
            sets.join(previous, current);
        }
    }
    std::map<std::size_t, std::vector<IndexedLeg>> grouped;
    for (std::size_t index = 0; index < legs.size(); ++index) {
        grouped[sets.find(index)].push_back(legs[index]);
    }
    if (grouped.size() > prepared.limits.maxCoverageCells) {
        return std::nullopt;
    }
    std::vector<std::vector<IndexedLeg>> cells;
    cells.reserve(grouped.size());
    for (auto& [root, cellLegs] : grouped) {
        static_cast<void>(root);
        std::sort(cellLegs.begin(), cellLegs.end(), [](const IndexedLeg& left, const IndexedLeg& right) {
            return std::tie(left.rowIndex, left.minimumAlong) < std::tie(right.rowIndex, right.minimumAlong);
        });
        cells.push_back(std::move(cellLegs));
    }
    std::sort(cells.begin(), cells.end(), [](const auto& left, const auto& right) {
        return std::tie(left.front().rowIndex, left.front().minimumAlong) <
               std::tie(right.front().rowIndex, right.front().minimumAlong);
    });
    return cells;
}

[[nodiscard]] std::vector<SprayLeg> orderedCellLegs(const std::vector<IndexedLeg>& cell, bool reverseRows,
                                                    bool startFromMaximumAlong)
{
    std::map<std::size_t, std::vector<IndexedLeg>> rows;
    for (const IndexedLeg& leg : cell) {
        rows[leg.rowIndex].push_back(leg);
    }
    std::vector<std::size_t> rowIndices;
    rowIndices.reserve(rows.size());
    for (const auto& [rowIndex, rowLegs] : rows) {
        static_cast<void>(rowLegs);
        rowIndices.push_back(rowIndex);
    }
    if (reverseRows) {
        std::reverse(rowIndices.begin(), rowIndices.end());
    }
    std::vector<SprayLeg> result;
    for (std::size_t rowPosition = 0; rowPosition < rowIndices.size(); ++rowPosition) {
        auto rowLegs = rows[rowIndices[rowPosition]];
        const bool reverse = startFromMaximumAlong != (rowPosition % 2 != 0);
        std::sort(rowLegs.begin(), rowLegs.end(), [reverse](const IndexedLeg& left, const IndexedLeg& right) {
            return reverse ? left.minimumAlong > right.minimumAlong : left.minimumAlong < right.minimumAlong;
        });
        for (const IndexedLeg& indexed : rowLegs) {
            SprayLeg leg = indexed.leg;
            if (reverse) {
                std::swap(leg.start, leg.end);
            }
            result.push_back(leg);
        }
    }
    return result;
}

struct CellVariant
{
    std::size_t id = 0;
    std::size_t cell = 0;
    RouteCandidate candidate;
};

[[nodiscard]] std::vector<CellVariant> buildCellVariants(const std::vector<std::vector<IndexedLeg>>& cells,
                                                         const PreparedInput& prepared, const Roadmap& roadmap,
                                                         const PlannerCostModel& model)
{
    std::vector<CellVariant> variants;
    for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
        for (const bool reverseRows : {false, true}) {
            for (const bool startFromMaximumAlong : {false, true}) {
                RouteCandidate candidate =
                    buildRouteCandidate(orderedCellLegs(cells[cellIndex], reverseRows, startFromMaximumAlong),
                                        std::nullopt, prepared, roadmap, model);
                if (candidate.valid) {
                    variants.push_back({variants.size(), cellIndex, std::move(candidate)});
                }
            }
        }
    }
    return variants;
}

struct Transition
{
    bool valid = false;
    std::vector<Point> path;
    double cost = std::numeric_limits<double>::infinity();
};

class OptimizationModel
{
public:
    OptimizationModel(const std::vector<CellVariant>& variants, const PreparedInput& prepared, const Roadmap& roadmap,
                      const PlannerInput& input)
        : _variants(variants), _prepared(prepared), _roadmap(roadmap), _input(input)
    {
        for (std::size_t index = 0; index < variants.size(); ++index) {
            _variantsByCell[variants[index].cell].push_back(index);
        }
        double nearestDistance = std::numeric_limits<double>::infinity();
        for (const CellVariant& variant : variants) {
            const SprayLeg& first = variant.candidate.legs.front();
            if (dot(first.end - first.start, *_input.sweepDirection) <= DISTANCE_EPSILON) {
                continue;
            }
            nearestDistance =
                std::min(nearestDistance, pointToSegmentDistance(*input.entryPoint, first.start, first.end));
        }
        for (std::size_t index = 0; index < variants.size(); ++index) {
            const SprayLeg& first = variants[index].candidate.legs.front();
            if (dot(first.end - first.start, *_input.sweepDirection) > DISTANCE_EPSILON &&
                pointToSegmentDistance(*input.entryPoint, first.start, first.end) <=
                    nearestDistance + coordinateTolerance(std::max(1.0, nearestDistance))) {
                _allowedStarts.push_back(index);
            }
        }
    }

    [[nodiscard]] const std::map<std::size_t, std::vector<std::size_t>>& variantsByCell() const
    {
        return _variantsByCell;
    }

    [[nodiscard]] bool allowedStart(std::size_t variant) const
    {
        return std::find(_allowedStarts.begin(), _allowedStarts.end(), variant) != _allowedStarts.end();
    }

    [[nodiscard]] double intrinsicTime(std::size_t variant) const { return _variants[variant].candidate.estimatedTime; }

    [[nodiscard]] const Transition& entry(std::size_t variant)
    {
        const auto existing = _entryCache.find(variant);
        if (existing != _entryCache.end()) {
            return existing->second;
        }
        Transition result;
        if (allowedStart(variant)) {
            const CellVariant& next = _variants[variant];
            const auto path =
                connectorPath(*_input.entryPoint, next.candidate.legs.front().start, _prepared, _roadmap, std::nullopt);
            if (path) {
                result.valid = true;
                result.path = *path;
                result.cost =
                    connectorCost(result.path, std::nullopt,
                                  segmentHeading(next.candidate.legs.front().start, next.candidate.legs.front().end));
            }
        }
        return _entryCache.emplace(variant, std::move(result)).first->second;
    }

    [[nodiscard]] const Transition& transition(std::size_t first, std::size_t second)
    {
        const auto key = std::make_pair(first, second);
        const auto existing = _transitionCache.find(key);
        if (existing != _transitionCache.end()) {
            return existing->second;
        }
        const CellVariant& previous = _variants[first];
        const CellVariant& next = _variants[second];
        const SprayLeg& previousLeg = previous.candidate.legs.back();
        const SprayLeg& nextLeg = next.candidate.legs.front();
        Transition result;
        const auto path =
            connectorPath(previousLeg.end, nextLeg.start, _prepared, _roadmap, previousLeg.end - previousLeg.start);
        if (path) {
            result.valid = true;
            result.path = *path;
            result.cost = connectorCost(result.path, segmentHeading(previousLeg.start, previousLeg.end),
                                        segmentHeading(nextLeg.start, nextLeg.end));
        }
        return _transitionCache.emplace(key, std::move(result)).first->second;
    }

    [[nodiscard]] double sequenceCost(const std::vector<std::size_t>& sequence)
    {
        if (sequence.empty()) {
            return std::numeric_limits<double>::infinity();
        }
        const Transition& start = entry(sequence.front());
        if (!start.valid) {
            return std::numeric_limits<double>::infinity();
        }
        double cost = start.cost + _variants[sequence.front()].candidate.estimatedTime;
        for (std::size_t index = 1; index < sequence.size(); ++index) {
            const Transition& connector = transition(sequence[index - 1], sequence[index]);
            if (!connector.valid) {
                return std::numeric_limits<double>::infinity();
            }
            cost += connector.cost + _variants[sequence[index]].candidate.estimatedTime;
        }
        return cost;
    }

    [[nodiscard]] RouteCandidate materialize(const std::vector<std::size_t>& sequence)
    {
        RouteCandidate result;
        if (sequence.empty()) {
            return result;
        }
        const Transition& start = entry(sequence.front());
        if (!start.valid || !appendRoutePoint(result.route, *_input.entryPoint, RoutePointType::Transit)) {
            return result;
        }
        for (std::size_t index = 1; index + 1 < start.path.size(); ++index) {
            if (!appendRoutePoint(result.route, start.path[index], RoutePointType::Transit)) {
                return {};
            }
        }
        const auto appendVariant = [&result](const CellVariant& variant) {
            result.legs.insert(result.legs.end(), variant.candidate.legs.begin(), variant.candidate.legs.end());
            for (const RoutePoint& point : variant.candidate.route) {
                if (!appendRoutePoint(result.route, point.position, point.type)) {
                    return false;
                }
            }
            return true;
        };
        if (!appendVariant(_variants[sequence.front()])) {
            return {};
        }
        for (std::size_t index = 1; index < sequence.size(); ++index) {
            const Transition& connector = transition(sequence[index - 1], sequence[index]);
            if (!connector.valid) {
                return {};
            }
            for (std::size_t pathIndex = 1; pathIndex + 1 < connector.path.size(); ++pathIndex) {
                if (!appendRoutePoint(result.route, connector.path[pathIndex], RoutePointType::Transit)) {
                    return {};
                }
            }
            if (!appendVariant(_variants[sequence[index]])) {
                return {};
            }
            if (result.route.size() > _prepared.limits.maxRoutePoints) {
                return {};
            }
        }
        calculateRouteMetrics(result, _prepared, _input.costModel);
        return result;
    }

private:
    [[nodiscard]] double connectorCost(const std::vector<Point>& path, const std::optional<double>& previousHeading,
                                       double nextHeading) const
    {
        double cost = 0.0;
        std::size_t ignoredTurns = 0;
        std::optional<double> heading = previousHeading;
        for (std::size_t index = 1; index < path.size(); ++index) {
            const double segmentLength = distance(path[index - 1], path[index]);
            if (segmentLength <= DISTANCE_EPSILON) {
                continue;
            }
            const double currentHeading = segmentHeading(path[index - 1], path[index]);
            if (heading) {
                cost += turnTime(*heading, currentHeading, _input.costModel, ignoredTurns);
            }
            cost += segmentTravelTime(segmentLength, _input.costModel.transitSpeed, _input.costModel.acceleration);
            heading = currentHeading;
        }
        if (heading) {
            cost += turnTime(*heading, nextHeading, _input.costModel, ignoredTurns);
        }
        return cost;
    }

    const std::vector<CellVariant>& _variants;
    const PreparedInput& _prepared;
    const Roadmap& _roadmap;
    const PlannerInput& _input;
    std::map<std::size_t, std::vector<std::size_t>> _variantsByCell;
    std::vector<std::size_t> _allowedStarts;
    std::map<std::size_t, Transition> _entryCache;
    std::map<std::pair<std::size_t, std::size_t>, Transition> _transitionCache;
};

[[nodiscard]] std::pair<double, std::vector<std::size_t>> bestVariantsForCellOrder(
    OptimizationModel& model, const std::vector<std::size_t>& cellOrder)
{
    if (cellOrder.empty()) {
        return {std::numeric_limits<double>::infinity(), {}};
    }
    std::map<std::size_t, std::pair<double, std::vector<std::size_t>>> states;
    for (const std::size_t variant : model.variantsByCell().at(cellOrder.front())) {
        const Transition& entry = model.entry(variant);
        if (entry.valid) {
            states[variant] = {model.sequenceCost({variant}), {variant}};
        }
    }
    for (std::size_t position = 1; position < cellOrder.size() && !states.empty(); ++position) {
        std::map<std::size_t, std::pair<double, std::vector<std::size_t>>> nextStates;
        for (const std::size_t nextVariant : model.variantsByCell().at(cellOrder[position])) {
            for (const auto& [previousVariant, previousState] : states) {
                const Transition& connector = model.transition(previousVariant, nextVariant);
                if (!connector.valid) {
                    continue;
                }
                std::vector<std::size_t> path = previousState.second;
                path.push_back(nextVariant);
                const double cost = model.sequenceCost(path);
                const auto existing = nextStates.find(nextVariant);
                if (existing == nextStates.end() || cost + DISTANCE_EPSILON < existing->second.first ||
                    (std::abs(cost - existing->second.first) <= DISTANCE_EPSILON && path < existing->second.second)) {
                    nextStates[nextVariant] = {cost, std::move(path)};
                }
            }
        }
        states = std::move(nextStates);
    }
    if (states.empty()) {
        return {std::numeric_limits<double>::infinity(), {}};
    }
    return std::min_element(states.begin(), states.end(),
                            [](const auto& left, const auto& right) {
                                return std::tie(left.second.first, left.second.second) <
                                       std::tie(right.second.first, right.second.second);
                            })
        ->second;
}

[[nodiscard]] std::vector<std::size_t> solveExact(OptimizationModel& model, std::size_t cellCount)
{
    const std::size_t variantCount = [&model]() {
        std::size_t count = 0;
        for (const auto& [cell, variants] : model.variantsByCell()) {
            static_cast<void>(cell);
            count += variants.size();
        }
        return count;
    }();
    const std::size_t fullMask = (std::size_t{1} << cellCount) - 1;
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> costs((fullMask + 1) * variantCount, infinity);
    std::vector<int> parents(costs.size(), -1);
    const auto stateIndex = [variantCount](std::size_t mask, std::size_t variant) {
        return mask * variantCount + variant;
    };
    for (const auto& [cell, variants] : model.variantsByCell()) {
        const std::size_t mask = std::size_t{1} << cell;
        for (const std::size_t variant : variants) {
            if (model.allowedStart(variant)) {
                costs[stateIndex(mask, variant)] = model.sequenceCost({variant});
            }
        }
    }
    for (std::size_t mask = 1; mask <= fullMask; ++mask) {
        for (const auto& [lastCell, lastVariants] : model.variantsByCell()) {
            if ((mask & (std::size_t{1} << lastCell)) == 0) {
                continue;
            }
            for (const std::size_t last : lastVariants) {
                const double currentCost = costs[stateIndex(mask, last)];
                if (!std::isfinite(currentCost)) {
                    continue;
                }
                for (const auto& [nextCell, nextVariants] : model.variantsByCell()) {
                    const std::size_t nextBit = std::size_t{1} << nextCell;
                    if (mask & nextBit) {
                        continue;
                    }
                    for (const std::size_t next : nextVariants) {
                        const Transition& connector = model.transition(last, next);
                        if (!connector.valid) {
                            continue;
                        }
                        const std::size_t nextMask = mask | nextBit;
                        const std::size_t nextState = stateIndex(nextMask, next);
                        const double candidate = currentCost + connector.cost + model.intrinsicTime(next);
                        if (candidate + DISTANCE_EPSILON < costs[nextState]) {
                            costs[nextState] = candidate;
                            parents[nextState] = static_cast<int>(last);
                        }
                    }
                }
            }
        }
    }
    std::size_t last = variantCount;
    double best = infinity;
    for (const auto& [cell, variants] : model.variantsByCell()) {
        static_cast<void>(cell);
        for (const std::size_t variant : variants) {
            const double cost = costs[stateIndex(fullMask, variant)];
            if (cost + DISTANCE_EPSILON < best || (std::abs(cost - best) <= DISTANCE_EPSILON && variant < last)) {
                best = cost;
                last = variant;
            }
        }
    }
    if (last == variantCount) {
        return {};
    }
    std::vector<std::size_t> reversed;
    std::size_t mask = fullMask;
    while (last < variantCount) {
        reversed.push_back(last);
        std::size_t cell = 0;
        for (const auto& [candidateCell, variants] : model.variantsByCell()) {
            if (std::find(variants.begin(), variants.end(), last) != variants.end()) {
                cell = candidateCell;
                break;
            }
        }
        const int previous = parents[stateIndex(mask, last)];
        mask &= ~(std::size_t{1} << cell);
        if (previous < 0) {
            break;
        }
        last = static_cast<std::size_t>(previous);
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

[[nodiscard]] std::vector<std::size_t> solveHeuristic(OptimizationModel& model)
{
    std::size_t firstVariant = std::numeric_limits<std::size_t>::max();
    double firstCost = std::numeric_limits<double>::infinity();
    for (const auto& [cell, variants] : model.variantsByCell()) {
        static_cast<void>(cell);
        for (const std::size_t variant : variants) {
            const double cost = model.sequenceCost({variant});
            if (cost + DISTANCE_EPSILON < firstCost ||
                (std::abs(cost - firstCost) <= DISTANCE_EPSILON && variant < firstVariant)) {
                firstCost = cost;
                firstVariant = variant;
            }
        }
    }
    if (firstVariant == std::numeric_limits<std::size_t>::max()) {
        return {};
    }
    std::size_t firstCell = 0;
    for (const auto& [cell, variants] : model.variantsByCell()) {
        if (std::find(variants.begin(), variants.end(), firstVariant) != variants.end()) {
            firstCell = cell;
            break;
        }
    }
    std::vector<std::size_t> cellOrder{firstCell};
    std::vector<std::size_t> remaining;
    for (const auto& [cell, variants] : model.variantsByCell()) {
        static_cast<void>(variants);
        if (cell != firstCell) {
            remaining.push_back(cell);
        }
    }
    while (!remaining.empty()) {
        double bestCost = std::numeric_limits<double>::infinity();
        std::vector<std::size_t> bestOrder;
        std::size_t insertedCell = remaining.front();
        const std::size_t candidateCount =
            remaining.size() > 20 ? std::min<std::size_t>(12, remaining.size()) : remaining.size();
        for (std::size_t remainingIndex = 0; remainingIndex < candidateCount; ++remainingIndex) {
            const std::size_t cell = remaining[remainingIndex];
            for (std::size_t position = 0; position <= cellOrder.size(); ++position) {
                std::vector<std::size_t> candidate = cellOrder;
                candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(position), cell);
                const auto [cost, sequence] = bestVariantsForCellOrder(model, candidate);
                static_cast<void>(sequence);
                if (cost + DISTANCE_EPSILON < bestCost ||
                    (std::abs(cost - bestCost) <= DISTANCE_EPSILON && candidate < bestOrder)) {
                    bestCost = cost;
                    bestOrder = std::move(candidate);
                    insertedCell = cell;
                }
            }
        }
        if (!std::isfinite(bestCost)) {
            return {};
        }
        cellOrder = std::move(bestOrder);
        remaining.erase(std::find(remaining.begin(), remaining.end(), insertedCell));
    }
    for (std::size_t pass = 0; pass < 6; ++pass) {
        const auto [incumbentCost, incumbentSequence] = bestVariantsForCellOrder(model, cellOrder);
        static_cast<void>(incumbentSequence);
        double bestCost = incumbentCost;
        std::vector<std::size_t> bestOrder = cellOrder;
        for (std::size_t first = 0; first + 1 < cellOrder.size(); ++first) {
            for (std::size_t last = first + 1; last < std::min(cellOrder.size(), first + 9); ++last) {
                std::vector<std::size_t> candidate = cellOrder;
                std::reverse(candidate.begin() + static_cast<std::ptrdiff_t>(first),
                             candidate.begin() + static_cast<std::ptrdiff_t>(last + 1));
                const auto [cost, sequence] = bestVariantsForCellOrder(model, candidate);
                static_cast<void>(sequence);
                if (cost + DISTANCE_EPSILON < bestCost) {
                    bestCost = cost;
                    bestOrder = std::move(candidate);
                }
            }
        }
        for (std::size_t from = 0; from < cellOrder.size(); ++from) {
            for (std::size_t to = 0; to < cellOrder.size(); ++to) {
                if (from == to) {
                    continue;
                }
                std::vector<std::size_t> candidate = cellOrder;
                const std::size_t cell = candidate[from];
                candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(from));
                candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(to), cell);
                const auto [cost, sequence] = bestVariantsForCellOrder(model, candidate);
                static_cast<void>(sequence);
                if (cost + DISTANCE_EPSILON < bestCost) {
                    bestCost = cost;
                    bestOrder = std::move(candidate);
                }
            }
        }
        if (bestOrder == cellOrder) {
            break;
        }
        cellOrder = std::move(bestOrder);
    }
    return bestVariantsForCellOrder(model, cellOrder).second;
}

[[nodiscard]] PlannerResult successfulResult(RouteCandidate candidate, PlannerMethod method, std::size_t activeCells,
                                             bool fallback)
{
    return {PlannerStatus::Success,
            {},
            std::move(candidate.legs),
            std::move(candidate.route),
            candidate.distance,
            candidate.sprayDistance,
            candidate.transitDistance,
            candidate.estimatedTime,
            candidate.turnCount,
            activeCells,
            method,
            fallback};
}

[[nodiscard]] PlannerResult failure(PlannerStatus status, std::string error)
{
    return {status, std::move(error), {}, {}, 0.0};
}

}  // namespace

PlannerResult legacyPlan(const PlannerInput& input)
{
    PreparedInput prepared;
    std::string error;
    if (!prepareShapes(input, prepared, error)) {
        return failure(PlannerStatus::InvalidInput, std::move(error));
    }

    ScanRowsResult rows = buildScanRows(prepared, input.spacing);
    if (rows.status == ScanRowsStatus::ComplexityLimit) {
        return failure(PlannerStatus::ComplexityLimit, "scanline count exceeds its limit");
    }
    if (rows.status != ScanRowsStatus::Found) {
        return failure(PlannerStatus::InvalidInput, "scanline generation encountered numerically invalid geometry");
    }

    std::vector<SprayLeg> legs;
    const bool reverseRows =
        input.entryCorner == EntryCorner::TopRight || input.entryCorner == EntryCorner::BottomRight;
    const bool reverseFirstRow =
        input.entryCorner == EntryCorner::TopLeft || input.entryCorner == EntryCorner::TopRight;
    for (std::size_t orderedIndex = 0; orderedIndex < rows.rows.size(); ++orderedIndex) {
        const std::size_t rowIndex = reverseRows ? rows.rows.size() - 1 - orderedIndex : orderedIndex;
        const ScanRow& row = rows.rows[rowIndex];
        const bool reverseLegs = reverseFirstRow != (orderedIndex % 2 != 0);
        for (std::size_t orderedInterval = 0; orderedInterval < row.intervals.size(); ++orderedInterval) {
            const std::size_t intervalIndex =
                reverseLegs ? row.intervals.size() - 1 - orderedInterval : orderedInterval;
            const Interval& interval = row.intervals[intervalIndex];
            // Stay off all boundaries: exclusion boundaries are not flyable, and this also avoids
            // classifying a numerically ambiguous inclusion edge as a transit endpoint.
            const double endpointInset = std::min(BOUNDARY_INSET, (interval.end - interval.start) * 0.25);
            SprayLeg leg{prepared.frame.point(interval.start + endpointInset, row.across),
                         prepared.frame.point(interval.end - endpointInset, row.across)};
            if (reverseLegs) {
                std::swap(leg.start, leg.end);
            }
            if (distance(leg.start, leg.end) > DISTANCE_EPSILON) {
                legs.push_back(leg);
            }
            if (legs.size() > prepared.limits.maxSprayLegs) {
                return failure(PlannerStatus::ComplexityLimit, "spray leg count exceeds its limit");
            }
        }
    }
    if (legs.empty()) {
        return failure(PlannerStatus::EmptyRegion, "the effective region contains no sprayable scanline");
    }

    const std::optional<Roadmap> roadmap = buildRoadmap(prepared);
    if (!roadmap) {
        return failure(PlannerStatus::ComplexityLimit, "visibility graph exceeds its node limit");
    }
    const CoverageResult coverage = coverageIsConnected(prepared, *roadmap, legs);
    if (coverage.status == CoverageStatus::ComplexityLimit) {
        return failure(PlannerStatus::ComplexityLimit, "topology representatives exceed their limit");
    }
    if (coverage.status == CoverageStatus::InvalidGeometry) {
        return failure(PlannerStatus::InvalidInput, "connectivity analysis encountered numerically invalid geometry");
    }
    if (coverage.status == CoverageStatus::Empty) {
        return failure(PlannerStatus::EmptyRegion, "the effective region has no connected interior");
    }
    if (coverage.status != CoverageStatus::Connected) {
        return failure(PlannerStatus::DisconnectedRegion, "the effective region is disconnected or cannot be routed");
    }

    std::vector<RoutePoint> route;
    route.reserve(legs.size() * 3);
    const auto appendRoutePoint = [&route](const Point& point, RoutePointType type) {
        if (!route.empty() && distance(route.back().position, point) <= DISTANCE_EPSILON) {
            return false;
        }
        route.push_back({point, type});
        return true;
    };
    if (!appendRoutePoint(legs.front().start, RoutePointType::SprayStart) ||
        !appendRoutePoint(legs.front().end, RoutePointType::SprayEnd)) {
        return failure(PlannerStatus::NoRoute, "the spray route contains a zero-length segment");
    }
    if (route.size() > prepared.limits.maxRoutePoints) {
        return failure(PlannerStatus::ComplexityLimit, "route point count exceeds its limit");
    }
    for (std::size_t index = 1; index < legs.size(); ++index) {
        const Point previousLegDirection = legs[index - 1].end - legs[index - 1].start;
        TransitResult transit =
            findTransit(legs[index - 1].end, legs[index].start, prepared, *roadmap, previousLegDirection);
        if (transit.status != TransitStatus::Found) {
            return failure(PlannerStatus::NoRoute, "a spray-leg connector cannot remain inside the effective region");
        }
        const std::optional<std::vector<Point>> simplifiedTransitPoints = simplifyTransitPoints(
            transit.points, legs[index - 1].end, legs[index].start, prepared, previousLegDirection);
        if (!simplifiedTransitPoints) {
            return failure(PlannerStatus::NoRoute, "a spray-leg connector cannot be simplified safely");
        }
        for (const Point& point : *simplifiedTransitPoints) {
            if (!appendRoutePoint(point, RoutePointType::Transit)) {
                return failure(PlannerStatus::NoRoute, "a spray-leg connector contains a zero-length segment");
            }
        }
        if (!appendRoutePoint(legs[index].start, RoutePointType::SprayStart) ||
            !appendRoutePoint(legs[index].end, RoutePointType::SprayEnd)) {
            return failure(PlannerStatus::NoRoute, "the spray route contains a zero-length segment");
        }
        if (route.size() > prepared.limits.maxRoutePoints) {
            return failure(PlannerStatus::ComplexityLimit, "route point count exceeds its limit");
        }
    }

    double totalDistance = 0.0;
    for (std::size_t index = 1; index < route.size(); ++index) {
        totalDistance += distance(route[index - 1].position, route[index].position);
    }
    return {PlannerStatus::Success, {}, std::move(legs), std::move(route), totalDistance};
}

PlannerResult plan(const PlannerInput& input)
{
    if (!input.entryPoint || !input.sweepDirection) {
        return legacyPlan(input);
    }

    PreparedInput prepared;
    std::string error;
    if (!prepareShapes(input, prepared, error)) {
        return failure(PlannerStatus::InvalidInput, std::move(error));
    }
    ScanRowsResult rows = buildScanRows(prepared, input.spacing);
    if (rows.status == ScanRowsStatus::ComplexityLimit) {
        return failure(PlannerStatus::ComplexityLimit, "scanline count exceeds its limit");
    }
    if (rows.status != ScanRowsStatus::Found) {
        return failure(PlannerStatus::InvalidInput, "scanline generation encountered numerically invalid geometry");
    }

    const std::vector<IndexedLeg> canonical = indexedLegs(rows.rows, prepared);
    if (canonical.empty()) {
        return failure(PlannerStatus::EmptyRegion, "the effective region contains no sprayable scanline");
    }
    if (canonical.size() > prepared.limits.maxSprayLegs) {
        return failure(PlannerStatus::ComplexityLimit, "spray leg count exceeds its limit");
    }
    std::vector<SprayLeg> canonicalLegs;
    canonicalLegs.reserve(canonical.size());
    for (const IndexedLeg& leg : canonical) {
        canonicalLegs.push_back(leg.leg);
    }

    const std::optional<Roadmap> roadmap = buildRoadmap(prepared);
    if (!roadmap) {
        return failure(PlannerStatus::ComplexityLimit, "visibility graph exceeds its node limit");
    }
    const CoverageResult coverage = coverageIsConnected(prepared, *roadmap, canonicalLegs);
    if (coverage.status == CoverageStatus::ComplexityLimit) {
        return failure(PlannerStatus::ComplexityLimit, "topology representatives exceed their limit");
    }
    if (coverage.status == CoverageStatus::InvalidGeometry) {
        return failure(PlannerStatus::InvalidInput, "connectivity analysis encountered numerically invalid geometry");
    }
    if (coverage.status == CoverageStatus::Empty) {
        return failure(PlannerStatus::EmptyRegion, "the effective region has no connected interior");
    }
    if (coverage.status != CoverageStatus::Connected) {
        return failure(PlannerStatus::DisconnectedRegion, "the effective region is disconnected or cannot be routed");
    }

    RouteCandidate legacy;
    double bestLineDistance = std::numeric_limits<double>::infinity();
    double bestStartDistance = std::numeric_limits<double>::infinity();
    std::size_t orderingIndex = 0;
    std::size_t bestOrderingIndex = std::numeric_limits<std::size_t>::max();
    for (const bool reverseRows : {false, true}) {
        for (const bool startFromMaximumAlong : {false, true}) {
            std::vector<SprayLeg> candidateLegs = orderedLegs(rows.rows, prepared, reverseRows, startFromMaximumAlong);
            if (candidateLegs.empty()) {
                ++orderingIndex;
                continue;
            }
            if (dot(candidateLegs.front().end - candidateLegs.front().start, *input.sweepDirection) <=
                DISTANCE_EPSILON) {
                ++orderingIndex;
                continue;
            }
            const double lineDistance =
                pointToSegmentDistance(*input.entryPoint, candidateLegs.front().start, candidateLegs.front().end);
            const double startDistance = distance(*input.entryPoint, candidateLegs.front().start);
            RouteCandidate candidate =
                buildRouteCandidate(std::move(candidateLegs), input.entryPoint, prepared, *roadmap, input.costModel);
            if (candidate.valid && (lineDistance + DISTANCE_EPSILON < bestLineDistance ||
                                    (std::abs(lineDistance - bestLineDistance) <= DISTANCE_EPSILON &&
                                     (startDistance + DISTANCE_EPSILON < bestStartDistance ||
                                      (std::abs(startDistance - bestStartDistance) <= DISTANCE_EPSILON &&
                                       orderingIndex < bestOrderingIndex))))) {
                bestLineDistance = lineDistance;
                bestStartDistance = startDistance;
                bestOrderingIndex = orderingIndex;
                legacy = std::move(candidate);
            }
            ++orderingIndex;
        }
    }
    if (!legacy.valid) {
        return failure(PlannerStatus::NoRoute, "the legacy spray route cannot remain inside the effective region");
    }

    const auto cells = buildCoverageCells(rows.rows, prepared);
    if (!cells || cells->empty()) {
        return successfulResult(std::move(legacy), PlannerMethod::LegacyFallback, 0, true);
    }
    std::vector<CellVariant> variants = buildCellVariants(*cells, prepared, *roadmap, input.costModel);
    std::size_t variantCellCount = 0;
    std::optional<std::size_t> previousCell;
    for (const CellVariant& variant : variants) {
        if (!previousCell || *previousCell != variant.cell) {
            ++variantCellCount;
            previousCell = variant.cell;
        }
    }
    if (variantCellCount != cells->size()) {
        return successfulResult(std::move(legacy), PlannerMethod::LegacyFallback, cells->size(), true);
    }

    OptimizationModel model(variants, prepared, *roadmap, input);
    std::vector<std::size_t> sequence;
    PlannerMethod method = PlannerMethod::HeuristicCellOptimization;
    if (cells->size() <= input.limits.exactCellLimit) {
        sequence = solveExact(model, cells->size());
        method = PlannerMethod::ExactCellOptimization;
    } else {
        sequence = solveHeuristic(model);
    }
    RouteCandidate optimized = model.materialize(sequence);
    const double sprayTolerance = coordinateTolerance(std::max(1.0, legacy.sprayDistance));
    if (!optimized.valid || std::abs(optimized.sprayDistance - legacy.sprayDistance) > sprayTolerance ||
        optimized.estimatedTime > legacy.estimatedTime + DISTANCE_EPSILON) {
        return successfulResult(std::move(legacy), PlannerMethod::LegacyFallback, cells->size(), true);
    }
    return successfulResult(std::move(optimized), method, cells->size(), false);
}

}  // namespace AgriculturalSpray
