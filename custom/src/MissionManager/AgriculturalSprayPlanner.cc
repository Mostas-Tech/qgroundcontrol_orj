#include "AgriculturalSprayPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace AgriculturalSpray {
namespace {

constexpr double DISTANCE_EPSILON = 1e-9;
constexpr double PARAMETER_EPSILON = 1e-9;
constexpr double MAXIMUM_COORDINATE = 1e7;
constexpr double BOUNDARY_INSET = 1e-7;
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
    if (input.inclusions.empty()) {
        error = "at least one inclusion shape is required";
        return false;
    }
    if (!finiteCoordinate(input.spacing) || input.spacing <= DISTANCE_EPSILON ||
        !finiteCoordinate(input.gridAngleDegrees) || std::abs(input.gridAngleDegrees) > 360.0 ||
        !finiteCoordinate(input.circleChordError) || input.circleChordError <= 0.0 ||
        !validEntryCorner(input.entryCorner)) {
        error = "spacing, grid angle, circle chord error, or entry corner is invalid";
        return false;
    }
    if (input.inclusions.size() + input.exclusions.size() > input.limits.maxShapes || input.limits.maxShapes == 0 ||
        input.limits.maxShapeVertices < 3 || input.limits.maxScanLines == 0 || input.limits.maxSprayLegs == 0 ||
        input.limits.maxVisibilityNodes < 2 || input.limits.maxTopologyRepresentatives == 0 ||
        input.limits.maxRoutePoints == 0) {
        error = "planner limits are invalid or shape count exceeds the limit";
        return false;
    }

    const double radians = input.gridAngleDegrees * std::numbers::pi / 180.0;
    prepared.frame.direction = {std::cos(radians), std::sin(radians)};
    prepared.frame.normal = {-std::sin(radians), std::cos(radians)};
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
                const auto& circle = std::get<Circle>(shape);
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
    const double tolerance = coordinateTolerance(std::max({1.0, std::abs(along), std::abs(across),
                                                           std::abs(bounds.minimumAlong), std::abs(bounds.maximumAlong),
                                                           std::abs(bounds.minimumAcross), std::abs(bounds.maximumAcross)}));
    return along >= bounds.minimumAlong - tolerance && along <= bounds.maximumAlong + tolerance &&
           across >= bounds.minimumAcross - tolerance && across <= bounds.maximumAcross + tolerance;
}

[[nodiscard]] bool boundsCrossScanline(const GridBounds& bounds, double across)
{
    const double tolerance =
        coordinateTolerance(std::max({1.0, std::abs(across), std::abs(bounds.minimumAcross), std::abs(bounds.maximumAcross)}));
    return across >= bounds.minimumAcross - tolerance && across <= bounds.maximumAcross + tolerance;
}

[[nodiscard]] bool boundsOverlap(const GridBounds& first, const GridBounds& second)
{
    const double tolerance = coordinateTolerance(
        std::max({1.0, std::abs(first.minimumAlong), std::abs(first.maximumAlong), std::abs(first.minimumAcross),
                  std::abs(first.maximumAcross), std::abs(second.minimumAlong), std::abs(second.maximumAlong),
                  std::abs(second.minimumAcross), std::abs(second.maximumAcross)}));
    return first.minimumAlong <= second.maximumAlong + tolerance && first.maximumAlong + tolerance >= second.minimumAlong &&
           first.minimumAcross <= second.maximumAcross + tolerance && first.maximumAcross + tolerance >= second.minimumAcross;
}

[[nodiscard]] bool pointInEffectiveRegion(const Point& point, const PreparedInput& prepared)
{
    if (!finitePoint(point)) {
        return false;
    }

    const double along = prepared.frame.along(point);
    const double across = prepared.frame.across(point);
    const bool included =
        std::any_of(prepared.inclusions.begin(), prepared.inclusions.end(),
                    [&point, along, across](const PreparedShape& shape) {
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

[[nodiscard]] std::optional<std::vector<Interval>> polygonIntervals(const PreparedShape& shape,
                                                                    const GridFrame& frame, double across)
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

    double extent = 0.0;
    for (const Point& point : shape.vertices) {
        extent = std::max(extent, distance(center, point));
    }
    const double clearance = std::max(1e-7, extent * 1e-8);
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
        for (const Point& direction : {firstOutward, secondOutward, firstOutward + secondOutward, radial}) {
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

[[nodiscard]] bool advancesFromStart(const Point& start, const Point& point, const std::optional<Point>& initialDirection)
{
    if (!initialDirection) {
        return true;
    }

    const double initialDirectionLength = length(*initialDirection);
    return initialDirectionLength <= DISTANCE_EPSILON ||
           dot(point - start, *initialDirection) >= -DISTANCE_EPSILON * initialDirectionLength;
}

[[nodiscard]] TransitResult findTransit(const Point& start, const Point& end, const PreparedInput& prepared,
                                        const Roadmap& roadmap, const std::optional<Point>& initialDirection = std::nullopt)
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
            if (firstVisible != roadmap.nodes.size() &&
                findComponent(node) == findComponent(firstVisible)) {
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
    const bool connected = std::all_of(representativeComponents.begin(), representativeComponents.end(),
                                       [&findComponent, firstComponent](std::size_t component) {
                                           return findComponent(component) == firstComponent;
                                       });
    return {connected ? CoverageStatus::Connected : CoverageStatus::Disconnected};
}

[[nodiscard]] PlannerResult failure(PlannerStatus status, std::string error)
{
    return {status, std::move(error), {}, {}, 0.0};
}

}  // namespace

PlannerResult plan(const PlannerInput& input)
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
        const std::optional<std::vector<Point>> simplifiedTransitPoints =
            simplifyTransitPoints(transit.points, legs[index - 1].end, legs[index].start, prepared, previousLegDirection);
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

}  // namespace AgriculturalSpray
