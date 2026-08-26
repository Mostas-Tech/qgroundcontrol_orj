#include "AgriculturalSprayPlannerTest.h"

#include <QtCore/QString>
#include <QtTest/QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "AgriculturalSprayPlanner.h"

namespace {

using namespace AgriculturalSpray;

constexpr double ORACLE_TOLERANCE = 1e-8;

enum class AreaScenario
{
    Convex,
    Narrow,
    Rotated,
    Concave,
    OverlappingUnion,
};

enum class ExclusionScenario
{
    Polygon,
    Circle,
};

enum class FailureScenario
{
    InvalidPolygon,
    NoInclusion,
    Empty,
    DisconnectedInclusions,
    DisconnectedUnscannedInclusion,
    NonRoutableBarrier,
    NonRoutableCircleBarrier,
    ScanlineLimit,
    PartialRouteLimit,
    TopologyRepresentativeLimit,
};

struct OracleRectangle
{
    double minimumAlong = 0.0;
    double maximumAlong = 0.0;
    double minimumAcross = 0.0;
    double maximumAcross = 0.0;
    double angleDegrees = 0.0;
};

struct OracleCircle
{
    Point center;
    double radius = 0.0;
};

struct OracleRegion
{
    std::vector<OracleRectangle> inclusionRectangles;
    std::vector<OracleCircle> inclusionCircles;
    std::vector<OracleRectangle> exclusionRectangles;
    std::vector<OracleCircle> exclusionCircles;
};

struct ParameterInterval
{
    double start = 0.0;
    double end = 0.0;
};

[[nodiscard]] Point framePoint(double along, double across, double angleDegrees)
{
    const double radians = angleDegrees * std::numbers::pi / 180.0;
    const Point direction{std::cos(radians), std::sin(radians)};
    const Point normal{-std::sin(radians), std::cos(radians)};
    return direction * along + normal * across;
}

[[nodiscard]] Polygon rectangle(double minimumAlong, double maximumAlong, double minimumAcross, double maximumAcross,
                                double angleDegrees = 0.0)
{
    return {{
        framePoint(minimumAlong, minimumAcross, angleDegrees),
        framePoint(maximumAlong, minimumAcross, angleDegrees),
        framePoint(maximumAlong, maximumAcross, angleDegrees),
        framePoint(minimumAlong, maximumAcross, angleDegrees),
    }};
}

[[nodiscard]] OracleRectangle oracleRectangle(double minimumAlong, double maximumAlong, double minimumAcross,
                                              double maximumAcross, double angleDegrees = 0.0)
{
    return {minimumAlong, maximumAlong, minimumAcross, maximumAcross, angleDegrees};
}

[[nodiscard]] Polygon reversed(Polygon polygon)
{
    std::reverse(polygon.vertices.begin(), polygon.vertices.end());
    return polygon;
}

[[nodiscard]] std::pair<double, double> frameCoordinates(const Point& point, double angleDegrees)
{
    const double radians = angleDegrees * std::numbers::pi / 180.0;
    const Point direction{std::cos(radians), std::sin(radians)};
    const Point normal{-std::sin(radians), std::cos(radians)};
    return {dot(point, direction), dot(point, normal)};
}

[[nodiscard]] bool clipSlab(double origin, double delta, double minimum, double maximum, double& enter, double& exit)
{
    if (std::abs(delta) <= std::numeric_limits<double>::epsilon()) {
        return origin >= minimum && origin <= maximum;
    }

    double first = (minimum - origin) / delta;
    double second = (maximum - origin) / delta;
    if (first > second) {
        std::swap(first, second);
    }
    enter = std::max(enter, first);
    exit = std::min(exit, second);
    return enter <= exit;
}

[[nodiscard]] std::optional<ParameterInterval> rectangleInterval(const Point& start, const Point& end,
                                                                 const OracleRectangle& rectangle,
                                                                 double boundaryExpansion)
{
    const auto [startAlong, startAcross] = frameCoordinates(start, rectangle.angleDegrees);
    const auto [endAlong, endAcross] = frameCoordinates(end, rectangle.angleDegrees);
    double enter = 0.0;
    double exit = 1.0;
    if (!clipSlab(startAlong, endAlong - startAlong, rectangle.minimumAlong - boundaryExpansion,
                  rectangle.maximumAlong + boundaryExpansion, enter, exit) ||
        !clipSlab(startAcross, endAcross - startAcross, rectangle.minimumAcross - boundaryExpansion,
                  rectangle.maximumAcross + boundaryExpansion, enter, exit)) {
        return std::nullopt;
    }
    return ParameterInterval{enter, exit};
}

[[nodiscard]] std::optional<ParameterInterval> circleInterval(const Point& start, const Point& end,
                                                              const OracleCircle& circle, double radiusExpansion)
{
    const Point direction = end - start;
    const Point offset = start - circle.center;
    const double radius = circle.radius + radiusExpansion;
    const double quadratic = dot(direction, direction);
    if (quadratic <= std::numeric_limits<double>::epsilon()) {
        if (dot(offset, offset) <= radius * radius) {
            return ParameterInterval{0.0, 1.0};
        }
        return std::nullopt;
    }

    const double linear = 2.0 * dot(offset, direction);
    const double constant = dot(offset, offset) - radius * radius;
    const double discriminant = linear * linear - 4.0 * quadratic * constant;
    if (discriminant < 0.0) {
        return std::nullopt;
    }

    const double root = std::sqrt(std::max(0.0, discriminant));
    const double first = (-linear - root) / (2.0 * quadratic);
    const double second = (-linear + root) / (2.0 * quadratic);
    const double enter = std::max(0.0, std::min(first, second));
    const double exit = std::min(1.0, std::max(first, second));
    if (enter > exit) {
        return std::nullopt;
    }
    return ParameterInterval{enter, exit};
}

[[nodiscard]] double pointToSegmentDistance(const Point& point, const Point& start, const Point& end)
{
    const Point segment = end - start;
    const double squaredLength = dot(segment, segment);
    if (squaredLength <= std::numeric_limits<double>::epsilon()) {
        return distance(point, start);
    }

    const double parameter = std::clamp(dot(point - start, segment) / squaredLength, 0.0, 1.0);
    return distance(point, start + segment * parameter);
}

[[nodiscard]] bool inclusionIntervalsCoverSegment(const Point& start, const Point& end, const OracleRegion& region)
{
    std::vector<ParameterInterval> intervals;
    for (const OracleRectangle& rectangle : region.inclusionRectangles) {
        const auto interval = rectangleInterval(start, end, rectangle, ORACLE_TOLERANCE);
        if (interval) {
            intervals.push_back(*interval);
        }
    }
    for (const OracleCircle& circle : region.inclusionCircles) {
        const auto interval = circleInterval(start, end, circle, ORACLE_TOLERANCE);
        if (interval) {
            intervals.push_back(*interval);
        }
    }
    std::sort(intervals.begin(), intervals.end(), [](const ParameterInterval& left, const ParameterInterval& right) {
        return left.start == right.start ? left.end < right.end : left.start < right.start;
    });

    double coveredThrough = 0.0;
    bool coversStart = false;
    for (const ParameterInterval& interval : intervals) {
        if (!coversStart) {
            if (interval.start > ORACLE_TOLERANCE) {
                return false;
            }
            coversStart = true;
        } else if (interval.start > coveredThrough + ORACLE_TOLERANCE) {
            return false;
        }
        coveredThrough = std::max(coveredThrough, interval.end);
        if (coveredThrough >= 1.0 - ORACLE_TOLERANCE) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool segmentIsAllowed(const Point& start, const Point& end, const OracleRegion& region)
{
    if (!inclusionIntervalsCoverSegment(start, end, region)) {
        return false;
    }

    for (const OracleRectangle& rectangle : region.exclusionRectangles) {
        if (rectangleInterval(start, end, rectangle, 0.0)) {
            return false;
        }
    }
    for (const OracleCircle& circle : region.exclusionCircles) {
        if (pointToSegmentDistance(circle.center, start, end) <= circle.radius + ORACLE_TOLERANCE) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool pointsNear(const Point& first, const Point& second, double tolerance = ORACLE_TOLERANCE)
{
    return distance(first, second) <= tolerance;
}

[[nodiscard]] bool validateSuccessfulResult(const PlannerResult& result, const OracleRegion& region, QString& failure)
{
    if (!result.succeeded() || !result.error.empty() || result.legs.empty() || result.route.empty()) {
        failure = QStringLiteral("Expected a non-empty successful result");
        return false;
    }

    for (std::size_t index = 0; index < result.legs.size(); ++index) {
        const SprayLeg& leg = result.legs[index];
        if (distance(leg.start, leg.end) <= ORACLE_TOLERANCE) {
            failure = QStringLiteral("Spray leg %1 has zero length").arg(index);
            return false;
        }
        if (!segmentIsAllowed(leg.start, leg.end, region)) {
            failure = QStringLiteral("Spray leg %1 leaves the analytical allowed region").arg(index);
            return false;
        }
    }

    std::size_t legIndex = 0;
    bool sprayStarted = false;
    for (std::size_t index = 0; index < result.route.size(); ++index) {
        const RoutePoint& routePoint = result.route[index];
        if (!std::isfinite(routePoint.position.north) || !std::isfinite(routePoint.position.east)) {
            failure = QStringLiteral("Route point %1 is non-finite").arg(index);
            return false;
        }

        if (routePoint.type == RoutePointType::SprayStart) {
            if (sprayStarted || legIndex >= result.legs.size() ||
                !pointsNear(routePoint.position, result.legs[legIndex].start)) {
                failure = QStringLiteral("Route point %1 does not match its spray start").arg(index);
                return false;
            }
            sprayStarted = true;
        } else if (routePoint.type == RoutePointType::SprayEnd) {
            if (!sprayStarted || legIndex >= result.legs.size() ||
                !pointsNear(routePoint.position, result.legs[legIndex].end)) {
                failure = QStringLiteral("Route point %1 does not match its spray end").arg(index);
                return false;
            }
            sprayStarted = false;
            ++legIndex;
        } else if (sprayStarted) {
            failure = QStringLiteral("Transit point %1 interrupts a spray leg").arg(index);
            return false;
        }

        if (index > 0) {
            const Point& previous = result.route[index - 1].position;
            if (distance(previous, routePoint.position) <= ORACLE_TOLERANCE) {
                failure = QStringLiteral("Route segment ending at %1 has zero length").arg(index);
                return false;
            }
            if (!segmentIsAllowed(previous, routePoint.position, region)) {
                failure = QStringLiteral("Route segment ending at %1 leaves the analytical allowed region").arg(index);
                return false;
            }
        }
    }
    if (sprayStarted || legIndex != result.legs.size()) {
        failure = QStringLiteral("Route does not contain every spray leg");
        return false;
    }

    double calculatedDistance = 0.0;
    for (std::size_t index = 1; index < result.route.size(); ++index) {
        calculatedDistance += distance(result.route[index - 1].position, result.route[index].position);
    }
    const double distanceTolerance = ORACLE_TOLERANCE * std::max(1.0, calculatedDistance);
    if (std::abs(calculatedDistance - result.distance) > distanceTolerance) {
        failure = QStringLiteral("Reported route distance does not match its segments");
        return false;
    }
    return true;
}

[[nodiscard]] bool validateConnectorStringPulling(const PlannerResult& result, const OracleRegion& region,
                                                  QString& failure)
{
    for (std::size_t start = 0; start < result.route.size(); ++start) {
        if (result.route[start].type != RoutePointType::SprayEnd) {
            continue;
        }

        std::size_t end = start + 1;
        while (end < result.route.size() && result.route[end].type == RoutePointType::Transit) {
            ++end;
        }
        // The final spray end has no connector to simplify.
        if (end == result.route.size()) {
            continue;
        }
        if (result.route[end].type != RoutePointType::SprayStart) {
            failure = QStringLiteral("Connector after route point %1 does not end at a spray start").arg(start);
            return false;
        }

        for (std::size_t anchor = start; anchor + 2 <= end; ++anchor) {
            for (std::size_t candidate = anchor + 2; candidate <= end; ++candidate) {
                if (segmentIsAllowed(result.route[anchor].position, result.route[candidate].position, region)) {
                    failure = QStringLiteral("Connector point %1 can bypass retained waypoint %2")
                                  .arg(anchor)
                                  .arg(candidate - 1);
                    return false;
                }
            }
        }
    }
    return true;
}

[[nodiscard]] bool routeHasTransitPoint(const PlannerResult& result)
{
    return std::any_of(result.route.begin(), result.route.end(),
                       [](const RoutePoint& point) { return point.type == RoutePointType::Transit; });
}

[[nodiscard]] bool validateTransitEntriesDoNotBacktrack(const PlannerResult& result, QString& failure)
{
    bool foundTransitEntry = false;
    for (std::size_t index = 1; index + 1 < result.route.size(); ++index) {
        if (result.route[index].type != RoutePointType::SprayEnd ||
            result.route[index + 1].type != RoutePointType::Transit) {
            continue;
        }

        if (result.route[index - 1].type != RoutePointType::SprayStart) {
            failure = QStringLiteral("Spray end at route point %1 is not preceded by a spray start").arg(index);
            return false;
        }

        foundTransitEntry = true;
        const Point sprayDirection = result.route[index].position - result.route[index - 1].position;
        const Point transitDirection = result.route[index + 1].position - result.route[index].position;
        if (dot(sprayDirection, transitDirection) < -ORACLE_TOLERANCE) {
            failure = QStringLiteral("Transit point %1 starts behind its preceding spray-leg heading").arg(index + 1);
            return false;
        }
    }

    if (!foundTransitEntry) {
        failure = QStringLiteral("Expected at least one spray-end transit entry");
        return false;
    }
    return true;
}

[[nodiscard]] PlannerInput circleRoutingInput(double radius)
{
    PlannerInput input;
    input.inclusions = {rectangle(0.0, 300.0, 0.0, 200.0)};
    input.exclusions = {Circle{{150.0, 100.0}, radius}};
    input.spacing = 2.5;
    return input;
}

[[nodiscard]] OracleRegion circleRoutingRegion(double radius)
{
    OracleRegion region;
    region.inclusionRectangles = {oracleRectangle(0.0, 300.0, 0.0, 200.0)};
    region.exclusionCircles = {{{150.0, 100.0}, radius}};
    return region;
}

[[nodiscard]] std::vector<SprayLeg> canonicalLegs(const PlannerResult& result)
{
    std::vector<SprayLeg> legs = result.legs;
    for (SprayLeg& leg : legs) {
        if (leg.end.north < leg.start.north || (leg.end.north == leg.start.north && leg.end.east < leg.start.east)) {
            std::swap(leg.start, leg.end);
        }
    }
    std::sort(legs.begin(), legs.end(), [](const SprayLeg& left, const SprayLeg& right) {
        if (left.start.north != right.start.north) {
            return left.start.north < right.start.north;
        }
        if (left.start.east != right.start.east) {
            return left.start.east < right.start.east;
        }
        if (left.end.north != right.end.north) {
            return left.end.north < right.end.north;
        }
        return left.end.east < right.end.east;
    });
    return legs;
}

[[nodiscard]] bool sameLegSet(const PlannerResult& first, const PlannerResult& second)
{
    const std::vector<SprayLeg> firstLegs = canonicalLegs(first);
    const std::vector<SprayLeg> secondLegs = canonicalLegs(second);
    if (firstLegs.size() != secondLegs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < firstLegs.size(); ++index) {
        if (!pointsNear(firstLegs[index].start, secondLegs[index].start) ||
            !pointsNear(firstLegs[index].end, secondLegs[index].end)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool sameResult(const PlannerResult& first, const PlannerResult& second)
{
    if (first.status != second.status || first.legs.size() != second.legs.size() ||
        first.route.size() != second.route.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.legs.size(); ++index) {
        if (!pointsNear(first.legs[index].start, second.legs[index].start) ||
            !pointsNear(first.legs[index].end, second.legs[index].end)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < first.route.size(); ++index) {
        if (first.route[index].type != second.route[index].type ||
            !pointsNear(first.route[index].position, second.route[index].position)) {
            return false;
        }
    }
    return std::abs(first.distance - second.distance) <=
           ORACLE_TOLERANCE * std::max({1.0, first.distance, second.distance});
}

[[nodiscard]] bool sameResultExactly(const PlannerResult& first, const PlannerResult& second)
{
    if (first.status != second.status || first.error != second.error || first.distance != second.distance ||
        first.legs.size() != second.legs.size() || first.route.size() != second.route.size()) {
        return false;
    }
    for (std::size_t index = 0; index < first.legs.size(); ++index) {
        if (first.legs[index].start.north != second.legs[index].start.north ||
            first.legs[index].start.east != second.legs[index].start.east ||
            first.legs[index].end.north != second.legs[index].end.north ||
            first.legs[index].end.east != second.legs[index].end.east) {
            return false;
        }
    }
    for (std::size_t index = 0; index < first.route.size(); ++index) {
        if (first.route[index].type != second.route[index].type ||
            first.route[index].position.north != second.route[index].position.north ||
            first.route[index].position.east != second.route[index].position.east) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] double segmentDistance(const Point& firstStart, const Point& firstEnd, const Point& secondStart,
                                     const Point& secondEnd)
{
    const auto orientation = [](const Point& first, const Point& second, const Point& third) {
        return cross(second - first, third - first);
    };
    const auto pointOnSegment = [](const Point& point, const Point& start, const Point& end) {
        return point.north >= std::min(start.north, end.north) - ORACLE_TOLERANCE &&
               point.north <= std::max(start.north, end.north) + ORACLE_TOLERANCE &&
               point.east >= std::min(start.east, end.east) - ORACLE_TOLERANCE &&
               point.east <= std::max(start.east, end.east) + ORACLE_TOLERANCE;
    };
    const double firstA = orientation(firstStart, firstEnd, secondStart);
    const double firstB = orientation(firstStart, firstEnd, secondEnd);
    const double secondA = orientation(secondStart, secondEnd, firstStart);
    const double secondB = orientation(secondStart, secondEnd, firstEnd);
    const bool properIntersection = ((firstA < -ORACLE_TOLERANCE && firstB > ORACLE_TOLERANCE) ||
                                     (firstA > ORACLE_TOLERANCE && firstB < -ORACLE_TOLERANCE)) &&
                                    ((secondA < -ORACLE_TOLERANCE && secondB > ORACLE_TOLERANCE) ||
                                     (secondA > ORACLE_TOLERANCE && secondB < -ORACLE_TOLERANCE));
    const bool boundaryIntersection =
        (std::abs(firstA) <= ORACLE_TOLERANCE && pointOnSegment(secondStart, firstStart, firstEnd)) ||
        (std::abs(firstB) <= ORACLE_TOLERANCE && pointOnSegment(secondEnd, firstStart, firstEnd)) ||
        (std::abs(secondA) <= ORACLE_TOLERANCE && pointOnSegment(firstStart, secondStart, secondEnd)) ||
        (std::abs(secondB) <= ORACLE_TOLERANCE && pointOnSegment(firstEnd, secondStart, secondEnd));
    if (properIntersection || boundaryIntersection) {
        return 0.0;
    }
    return std::min({pointToSegmentDistance(firstStart, secondStart, secondEnd),
                     pointToSegmentDistance(firstEnd, secondStart, secondEnd),
                     pointToSegmentDistance(secondStart, firstStart, firstEnd),
                     pointToSegmentDistance(secondEnd, firstStart, firstEnd)});
}

[[nodiscard]] bool routeRespectsPolygonMargin(const PlannerResult& result, const Polygon& polygon, double margin,
                                              QString& failure)
{
    for (std::size_t routeIndex = 1; routeIndex < result.route.size(); ++routeIndex) {
        const Point& routeStart = result.route[routeIndex - 1].position;
        const Point& routeEnd = result.route[routeIndex].position;
        for (std::size_t edgeIndex = 0; edgeIndex < polygon.vertices.size(); ++edgeIndex) {
            const Point& edgeStart = polygon.vertices[edgeIndex];
            const Point& edgeEnd = polygon.vertices[(edgeIndex + 1) % polygon.vertices.size()];
            const double clearance = segmentDistance(routeStart, routeEnd, edgeStart, edgeEnd);
            if (clearance + ORACLE_TOLERANCE < margin) {
                failure = QStringLiteral("Route segment %1 has only %2 m clearance from source edge %3")
                              .arg(routeIndex - 1)
                              .arg(clearance, 0, 'g', 12)
                              .arg(edgeIndex);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

void AgriculturalSprayPlannerTest::_areaGeometry_data()
{
    QTest::addColumn<int>("scenario");
    QTest::addColumn<int>("expectedLegCount");

    QTest::newRow("convex") << static_cast<int>(AreaScenario::Convex) << 4;
    QTest::newRow("narrow") << static_cast<int>(AreaScenario::Narrow) << 1;
    QTest::newRow("rotated") << static_cast<int>(AreaScenario::Rotated) << 4;
    QTest::newRow("concave") << static_cast<int>(AreaScenario::Concave) << 4;
    QTest::newRow("overlapping-inclusion-union") << static_cast<int>(AreaScenario::OverlappingUnion) << 3;
}

void AgriculturalSprayPlannerTest::_areaGeometry()
{
    QFETCH(int, scenario);
    QFETCH(int, expectedLegCount);

    PlannerInput input;
    input.spacing = 5.0;
    OracleRegion region;
    switch (static_cast<AreaScenario>(scenario)) {
        case AreaScenario::Convex:
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
            region.inclusionRectangles = {oracleRectangle(0.0, 30.0, 0.0, 20.0)};
            break;
        case AreaScenario::Narrow:
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 2.0)};
            region.inclusionRectangles = {oracleRectangle(0.0, 30.0, 0.0, 2.0)};
            break;
        case AreaScenario::Rotated:
            input.gridAngleDegrees = 35.0;
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0, input.gridAngleDegrees)};
            region.inclusionRectangles = {oracleRectangle(0.0, 30.0, 0.0, 20.0, input.gridAngleDegrees)};
            break;
        case AreaScenario::Concave:
            input.inclusions = {
                Polygon{{{0.0, 0.0}, {30.0, 0.0}, {30.0, 6.0}, {10.0, 6.0}, {10.0, 20.0}, {0.0, 20.0}}}};
            region.inclusionRectangles = {
                oracleRectangle(0.0, 30.0, 0.0, 6.0),
                oracleRectangle(0.0, 10.0, 6.0, 20.0),
            };
            break;
        case AreaScenario::OverlappingUnion:
            input.inclusions = {
                rectangle(0.0, 20.0, 0.0, 10.0),
                rectangle(10.0, 30.0, 5.0, 15.0),
            };
            region.inclusionRectangles = {
                oracleRectangle(0.0, 20.0, 0.0, 10.0),
                oracleRectangle(10.0, 30.0, 5.0, 15.0),
            };
            break;
    }

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(result.legs.size()), expectedLegCount);
    QVERIFY(!routeHasTransitPoint(result));

    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_exclusionScanlineSplit_data()
{
    QTest::addColumn<int>("scenario");

    QTest::newRow("polygon-exclusion") << static_cast<int>(ExclusionScenario::Polygon);
    QTest::newRow("circle-exclusion") << static_cast<int>(ExclusionScenario::Circle);
}

void AgriculturalSprayPlannerTest::_exclusionScanlineSplit()
{
    QFETCH(int, scenario);

    PlannerInput input;
    input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
    input.spacing = 5.0;

    OracleRegion region;
    region.inclusionRectangles = {oracleRectangle(0.0, 30.0, 0.0, 20.0)};
    if (static_cast<ExclusionScenario>(scenario) == ExclusionScenario::Polygon) {
        input.exclusions = {rectangle(12.0, 18.0, 6.0, 14.0)};
        region.exclusionRectangles = {oracleRectangle(12.0, 18.0, 6.0, 14.0)};
    } else {
        input.exclusions = {Circle{{15.0, 10.0}, 3.0}};
        region.exclusionCircles = {{{15.0, 10.0}, 3.0}};
    }

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(result.legs.size(), std::size_t(6));
    QVERIFY(routeHasTransitPoint(result));

    std::vector<double> rows;
    for (const SprayLeg& leg : result.legs) {
        rows.push_back((leg.start.east + leg.end.east) * 0.5);
    }
    std::sort(rows.begin(), rows.end());
    const std::vector<double> expectedRows{2.5, 7.5, 7.5, 12.5, 12.5, 17.5};
    QCOMPARE(rows.size(), expectedRows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        QVERIFY(std::abs(rows[index] - expectedRows[index]) <= ORACLE_TOLERANCE);
    }

    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_circleObstacleRouting_data()
{
    QTest::addColumn<double>("radius");
    QTest::addColumn<int>("expectedLegCount");
    QTest::addColumn<bool>("requireTransit");

    QTest::newRow("typical-5m") << 5.0 << 84 << true;
    QTest::newRow("representative-stress-50m") << 50.0 << 120 << true;
    QTest::newRow("larger-valid-75m") << 75.0 << 140 << true;
}

void AgriculturalSprayPlannerTest::_circleObstacleRouting()
{
    QFETCH(double, radius);
    QFETCH(int, expectedLegCount);
    QFETCH(bool, requireTransit);

    const PlannerResult result = plan(circleRoutingInput(radius));
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(result.legs.size(), static_cast<std::size_t>(expectedLegCount));
    QCOMPARE(routeHasTransitPoint(result), requireTransit);

    const OracleRegion region = circleRoutingRegion(radius);
    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_tangentExclusionBoundaryIsNonFlyable()
{
    PlannerInput input;
    input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
    input.exclusions = {rectangle(12.0, 18.0, 7.5, 12.5)};
    input.spacing = 5.0;

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(routeHasTransitPoint(result));

    OracleRegion region;
    region.inclusionRectangles = {oracleRectangle(0.0, 30.0, 0.0, 20.0)};
    region.exclusionRectangles = {oracleRectangle(12.0, 18.0, 7.5, 12.5)};
    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_circleConnectorStringPulling_data()
{
    QTest::addColumn<double>("gridAngleDegrees");
    QTest::addColumn<int>("entryCorner");
    QTest::addColumn<double>("radius");

    const std::array<EntryCorner, 4> entryCorners{
        EntryCorner::TopLeft,
        EntryCorner::TopRight,
        EntryCorner::BottomLeft,
        EntryCorner::BottomRight,
    };
    for (int angle = 0; angle < 360; angle += 15) {
        for (std::size_t corner = 0; corner < entryCorners.size(); ++corner) {
            QTest::newRow(qPrintable(QStringLiteral("5m-angle-%1-corner-%2").arg(angle).arg(static_cast<int>(corner))))
                << static_cast<double>(angle) << static_cast<int>(entryCorners[corner]) << 5.0;
        }
    }

    const std::array<double, 3> nonAlignedAngles{7.0, 22.0, 37.0};
    for (const double angle : nonAlignedAngles) {
        for (std::size_t corner = 0; corner < entryCorners.size(); ++corner) {
            QTest::newRow(
                qPrintable(QStringLiteral("5m-angle-%1-corner-%2").arg(angle, 0, 'f', 0).arg(static_cast<int>(corner))))
                << angle << static_cast<int>(entryCorners[corner]) << 5.0;
        }
    }

    const std::array<std::pair<double, EntryCorner>, 4> stressRows{{
        {0.0, EntryCorner::TopLeft},
        {22.0, EntryCorner::TopRight},
        {90.0, EntryCorner::BottomLeft},
        {187.0, EntryCorner::BottomRight},
    }};
    for (const auto& [angle, corner] : stressRows) {
        QTest::newRow(
            qPrintable(QStringLiteral("50m-angle-%1-corner-%2").arg(angle, 0, 'f', 0).arg(static_cast<int>(corner))))
            << angle << static_cast<int>(corner) << 50.0;
    }
}

void AgriculturalSprayPlannerTest::_circleConnectorStringPulling()
{
    QFETCH(double, gridAngleDegrees);
    QFETCH(int, entryCorner);
    QFETCH(double, radius);

    PlannerInput input = circleRoutingInput(radius);
    input.gridAngleDegrees = gridAngleDegrees;
    input.entryCorner = static_cast<EntryCorner>(entryCorner);
    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(routeHasTransitPoint(result));

    const OracleRegion region = circleRoutingRegion(radius);
    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
    failure.clear();
    // String pulling must retain conservative circle clearance too. The analytical envelope
    // is independent of the planner's tessellation and covers its configured chord error.
    OracleRegion connectorRegion = region;
    for (OracleCircle& circle : connectorRegion.exclusionCircles) {
        circle.radius += input.circleChordError;
    }
    QVERIFY2(validateConnectorStringPulling(result, connectorRegion, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_circleDetourEntryIsForward_data()
{
    QTest::addColumn<double>("gridAngleDegrees");
    QTest::addColumn<int>("entryCorner");

    QTest::newRow("non-aligned-top-right") << 7.0 << static_cast<int>(EntryCorner::TopRight);
    QTest::newRow("non-aligned-bottom-left") << 22.0 << static_cast<int>(EntryCorner::BottomLeft);
}

void AgriculturalSprayPlannerTest::_circleDetourEntryIsForward()
{
    QFETCH(double, gridAngleDegrees);
    QFETCH(int, entryCorner);

    PlannerInput input = circleRoutingInput(5.0);
    input.gridAngleDegrees = gridAngleDegrees;
    input.entryCorner = static_cast<EntryCorner>(entryCorner);

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));

    QString failure;
    QVERIFY2(validateTransitEntriesDoNotBacktrack(result, failure), qPrintable(failure));

    const OracleRegion region = circleRoutingRegion(5.0);
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));

    const PlannerResult repeated = plan(input);
    QVERIFY(sameResult(result, repeated));
}

void AgriculturalSprayPlannerTest::_circleInclusionIsConservative()
{
    const Point center{4.0, -3.0};
    constexpr double radius = 10.0;

    PlannerInput input;
    input.inclusions = {Circle{center, radius}};
    input.spacing = 3.0;
    input.circleChordError = 0.1;

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(result.legs.size(), std::size_t(7));

    double maximumRadius = 0.0;
    for (const RoutePoint& point : result.route) {
        maximumRadius = std::max(maximumRadius, distance(center, point.position));
        QVERIFY(distance(center, point.position) <= radius + ORACLE_TOLERANCE);
    }
    QVERIFY(maximumRadius >= radius - 1e-6);

    OracleRegion region;
    region.inclusionCircles = {{center, radius}};
    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_entryCorner_data()
{
    QTest::addColumn<int>("entryCorner");
    QTest::addColumn<bool>("maximumAlong");
    QTest::addColumn<bool>("maximumAcross");

    QTest::newRow("top-left") << static_cast<int>(EntryCorner::TopLeft) << true << false;
    QTest::newRow("top-right") << static_cast<int>(EntryCorner::TopRight) << true << true;
    QTest::newRow("bottom-left") << static_cast<int>(EntryCorner::BottomLeft) << false << false;
    QTest::newRow("bottom-right") << static_cast<int>(EntryCorner::BottomRight) << false << true;
}

void AgriculturalSprayPlannerTest::_entryCorner()
{
    QFETCH(int, entryCorner);
    QFETCH(bool, maximumAlong);
    QFETCH(bool, maximumAcross);

    PlannerInput baselineInput;
    baselineInput.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
    baselineInput.spacing = 5.0;
    const PlannerResult baseline = plan(baselineInput);
    QCOMPARE(static_cast<int>(baseline.status), static_cast<int>(PlannerStatus::Success));

    PlannerInput input = baselineInput;
    input.entryCorner = static_cast<EntryCorner>(entryCorner);
    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(sameLegSet(baseline, result));
    QVERIFY(!routeHasTransitPoint(result));
    QCOMPARE(result.route.size(), result.legs.size() * 2);

    const Point entry = result.legs.front().start;
    QVERIFY(maximumAlong ? entry.north > 30.0 - 1e-6 : entry.north < 1e-6);
    QVERIFY(maximumAcross ? entry.east > 17.5 - ORACLE_TOLERANCE : entry.east < 2.5 + ORACLE_TOLERANCE);

    std::vector<double> rows;
    for (const SprayLeg& leg : result.legs) {
        rows.push_back((leg.start.east + leg.end.east) * 0.5);
    }
    std::sort(rows.begin(), rows.end());
    QCOMPARE(rows.size(), std::size_t(4));
    QVERIFY(std::abs(rows.front() - 2.5) <= ORACLE_TOLERANCE);
    QVERIFY(std::abs(rows.back() - 17.5) <= ORACLE_TOLERANCE);
    for (std::size_t index = 1; index < rows.size(); ++index) {
        QVERIFY(std::abs((rows[index] - rows[index - 1]) - input.spacing) <= ORACLE_TOLERANCE);
    }
    QVERIFY(std::abs(rows.front() - (20.0 - rows.back())) <= ORACLE_TOLERANCE);

    OracleRegion region;
    region.inclusionRectangles = {oracleRectangle(0.0, 30.0, 0.0, 20.0)};
    QString failure;
    QVERIFY2(validateSuccessfulResult(result, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_boundaryMarginZeroPreservesRoute()
{
    PlannerInput baselineInput;
    baselineInput.inclusions = {
        Polygon{{{0.0, 0.0}, {30.0, 0.0}, {30.0, 8.0}, {12.0, 8.0}, {12.0, 20.0}, {0.0, 20.0}}}};
    baselineInput.spacing = 4.0;
    baselineInput.entryPoint = Point{0.0, 0.0};
    baselineInput.sweepDirection = Point{1.0, 0.0};

    const PlannerResult baseline = plan(baselineInput);
    QCOMPARE(static_cast<int>(baseline.status), static_cast<int>(PlannerStatus::Success));

    PlannerInput marginInput = baselineInput;
    marginInput.boundaryMargin = 0.0;
    marginInput.boundaryMarginScope = BoundaryMarginScope::AllEdges;
    marginInput.marginEdgeIndex = 4;
    const PlannerResult marginResult = plan(marginInput);
    QVERIFY(sameResultExactly(baseline, marginResult));
}

void AgriculturalSprayPlannerTest::_selectedEdgeBoundaryMargin()
{
    PlannerInput input;
    const Polygon source = rectangle(0.0, 30.0, 0.0, 20.0);
    input.inclusions = {source};
    input.spacing = 4.0;
    input.entryPoint = Point{0.0, 0.0};
    input.sweepDirection = Point{1.0, 0.0};
    input.boundaryMargin = 3.0;
    input.boundaryMarginScope = BoundaryMarginScope::SelectedEdge;
    input.marginEdgeIndex = 0;

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(!result.route.empty());
    for (const RoutePoint& point : result.route) {
        QVERIFY(point.position.east >= input.boundaryMargin - ORACLE_TOLERANCE);
        QVERIFY(point.position.north >= -ORACLE_TOLERANCE);
        QVERIFY(point.position.north <= 30.0 + ORACLE_TOLERANCE);
    }

    const double firstAcross = result.legs.front().start.east;
    QVERIFY(firstAcross >= input.boundaryMargin - ORACLE_TOLERANCE);
    QVERIFY(firstAcross <= input.boundaryMargin + input.spacing + ORACLE_TOLERANCE);

    PlannerInput multipleEdgesInput = input;
    multipleEdgesInput.marginEdgeIndices = {0, 1};
    multipleEdgesInput.marginEdgeMargins = {3.0, 1.0};
    const PlannerResult multipleEdgesResult = plan(multipleEdgesInput);
    QCOMPARE(static_cast<int>(multipleEdgesResult.status), static_cast<int>(PlannerStatus::Success));
    for (const RoutePoint& point : multipleEdgesResult.route) {
        QVERIFY(point.position.east >= 3.0 - ORACLE_TOLERANCE);
        QVERIFY(point.position.north <= 29.0 + ORACLE_TOLERANCE);
    }

    input.marginEdgeIndex = 2;
    const PlannerResult oppositeEdgeResult = plan(input);
    QCOMPARE(static_cast<int>(oppositeEdgeResult.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(!oppositeEdgeResult.route.empty());
    QVERIFY(distance(oppositeEdgeResult.route.front().position, *input.entryPoint) <= ORACLE_TOLERANCE);

    input.inclusions = {Polygon{{{0.0, 0.0}, {30.0, 0.0}, {30.0, 8.0}, {12.0, 8.0}, {12.0, 20.0}, {0.0, 20.0}}}};
    input.marginEdgeIndex = 0;
    input.boundaryMargin = 1.0;
    const PlannerResult concaveResult = plan(input);
    QCOMPARE(static_cast<int>(concaveResult.status), static_cast<int>(PlannerStatus::Success));
    for (const RoutePoint& point : concaveResult.route) {
        QVERIFY(point.position.east >= input.boundaryMargin - ORACLE_TOLERANCE);
    }
    OracleRegion concaveMarginRegion;
    concaveMarginRegion.inclusionRectangles = {
        oracleRectangle(0.0, 30.0, 1.0, 8.0),
        oracleRectangle(0.0, 12.0, 8.0, 20.0),
    };
    QString concaveFailure;
    QVERIFY2(validateSuccessfulResult(concaveResult, concaveMarginRegion, concaveFailure), qPrintable(concaveFailure));

    input.inclusions = {source};
    input.boundaryMargin = 21.0;
    const PlannerResult excessiveResult = plan(input);
    QCOMPARE(static_cast<int>(excessiveResult.status), static_cast<int>(PlannerStatus::EmptyEffectiveArea));
    QVERIFY(excessiveResult.legs.empty());
    QVERIFY(excessiveResult.route.empty());

    PlannerInput collinearInput;
    const Polygon collinearSource{{{0.0, 0.0}, {15.0, 0.0}, {30.0, 0.0}, {30.0, 20.0}, {0.0, 20.0}}};
    collinearInput.inclusions = {collinearSource};
    collinearInput.spacing = 4.0;
    collinearInput.entryPoint = collinearSource.vertices[1];
    collinearInput.sweepDirection = collinearSource.vertices[2] - *collinearInput.entryPoint;
    collinearInput.boundaryMargin = 2.0;
    collinearInput.boundaryMarginScope = BoundaryMarginScope::SelectedEdge;
    collinearInput.marginEdgeIndex = 1;
    const PlannerResult collinearResult = plan(collinearInput);
    QCOMPARE(static_cast<int>(collinearResult.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(!collinearResult.route.empty());

    const Polygon reversedCollinearSource = reversed(collinearSource);
    collinearInput.inclusions = {reversedCollinearSource};
    collinearInput.entryPoint = reversedCollinearSource.vertices[2];
    collinearInput.sweepDirection = reversedCollinearSource.vertices[3] - *collinearInput.entryPoint;
    collinearInput.marginEdgeIndex = 2;
    const PlannerResult reversedCollinearResult = plan(collinearInput);
    QCOMPARE(static_cast<int>(reversedCollinearResult.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(!reversedCollinearResult.route.empty());

    PlannerInput splitInput;
    const Polygon splitSource{
        {{0.0, 0.0}, {30.0, 0.0}, {30.0, 20.0}, {22.0, 20.0}, {22.0, 6.0}, {8.0, 6.0}, {8.0, 20.0}, {0.0, 20.0}}};
    splitInput.inclusions = {splitSource};
    splitInput.spacing = 2.0;
    splitInput.entryPoint = splitSource.vertices[0];
    splitInput.sweepDirection = splitSource.vertices[1] - *splitInput.entryPoint;
    splitInput.boundaryMargin = 8.0;
    splitInput.boundaryMarginScope = BoundaryMarginScope::SelectedEdge;
    splitInput.marginEdgeIndex = 0;
    const PlannerResult splitResult = plan(splitInput);
    QCOMPARE(static_cast<int>(splitResult.status), static_cast<int>(PlannerStatus::DisconnectedRegion));
    QVERIFY(splitResult.route.empty());
}

void AgriculturalSprayPlannerTest::_allEdgesBoundaryMarginIsConcaveAndDeterministic()
{
    PlannerInput input;
    const Polygon source{{{0.0, 0.0}, {30.0, 0.0}, {30.0, 8.0}, {12.0, 8.0}, {12.0, 20.0}, {0.0, 20.0}}};
    input.inclusions = {source};
    input.spacing = 2.0;
    input.entryPoint = Point{0.0, 0.0};
    input.sweepDirection = Point{1.0, 0.0};
    input.boundaryMargin = 1.0;
    input.boundaryMarginScope = BoundaryMarginScope::AllEdges;
    input.marginEdgeIndex = 3;

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::Success));
    const PlannerResult repeated = plan(input);
    QVERIFY(sameResultExactly(result, repeated));

    QString failure;
    QVERIFY2(routeRespectsPolygonMargin(result, source, input.boundaryMargin, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_excessiveBoundaryMarginIsEmpty()
{
    PlannerInput input;
    input.inclusions = {rectangle(0.0, 10.0, 0.0, 10.0)};
    input.spacing = 2.0;
    input.entryPoint = Point{0.0, 0.0};
    input.sweepDirection = Point{1.0, 0.0};
    input.boundaryMargin = 6.0;
    input.boundaryMarginScope = BoundaryMarginScope::AllEdges;

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(PlannerStatus::EmptyEffectiveArea));
    QVERIFY(result.legs.empty());
    QVERIFY(result.route.empty());
}

void AgriculturalSprayPlannerTest::_exclusionMarginsAreConservative()
{
    PlannerInput polygonInput;
    const Polygon polygonExclusion = rectangle(12.0, 18.0, 7.0, 13.0);
    polygonInput.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
    polygonInput.exclusions = {polygonExclusion};
    polygonInput.exclusionMargins = {2.0};
    polygonInput.spacing = 2.0;
    polygonInput.entryPoint = Point{0.0, 0.0};
    polygonInput.sweepDirection = Point{1.0, 0.0};

    const PlannerResult polygonResult = plan(polygonInput);
    QCOMPARE(static_cast<int>(polygonResult.status), static_cast<int>(PlannerStatus::Success));
    QString polygonFailure;
    QVERIFY2(routeRespectsPolygonMargin(polygonResult, polygonExclusion, 2.0, polygonFailure),
             qPrintable(polygonFailure));

    PlannerInput circleInput = polygonInput;
    circleInput.exclusions = {Circle{{15.0, 10.0}, 3.0}};
    circleInput.exclusionMargins = {2.5};
    const PlannerResult circleResult = plan(circleInput);
    QCOMPARE(static_cast<int>(circleResult.status), static_cast<int>(PlannerStatus::Success));
    for (std::size_t index = 1; index < circleResult.route.size(); ++index) {
        const double clearance = pointToSegmentDistance(Point{15.0, 10.0}, circleResult.route[index - 1].position,
                                                        circleResult.route[index].position);
        QVERIFY(clearance + ORACLE_TOLERANCE >= 5.5);
    }

    PlannerInput invalidInput = polygonInput;
    invalidInput.exclusionMargins.clear();
    invalidInput.exclusionMargins.push_back(-1.0);
    const PlannerResult invalidResult = plan(invalidInput);
    QCOMPARE(static_cast<int>(invalidResult.status), static_cast<int>(PlannerStatus::InvalidInput));
    QVERIFY(invalidResult.route.empty());
}

void AgriculturalSprayPlannerTest::_failureIsFailClosed_data()
{
    QTest::addColumn<int>("scenario");
    QTest::addColumn<int>("expectedStatus");

    QTest::newRow("invalid-self-intersecting-polygon")
        << static_cast<int>(FailureScenario::InvalidPolygon) << static_cast<int>(PlannerStatus::InvalidInput);
    QTest::newRow("no-inclusion") << static_cast<int>(FailureScenario::NoInclusion)
                                  << static_cast<int>(PlannerStatus::InvalidInput);
    QTest::newRow("fully-excluded") << static_cast<int>(FailureScenario::Empty)
                                    << static_cast<int>(PlannerStatus::EmptyRegion);
    QTest::newRow("disconnected-inclusions") << static_cast<int>(FailureScenario::DisconnectedInclusions)
                                             << static_cast<int>(PlannerStatus::DisconnectedRegion);
    QTest::newRow("disconnected-unscanned-inclusion")
        << static_cast<int>(FailureScenario::DisconnectedUnscannedInclusion)
        << static_cast<int>(PlannerStatus::DisconnectedRegion);
    QTest::newRow("non-routable-exclusion-barrier")
        << static_cast<int>(FailureScenario::NonRoutableBarrier) << static_cast<int>(PlannerStatus::DisconnectedRegion);
    QTest::newRow("non-routable-circle-barrier") << static_cast<int>(FailureScenario::NonRoutableCircleBarrier)
                                                 << static_cast<int>(PlannerStatus::DisconnectedRegion);
    QTest::newRow("scanline-limit") << static_cast<int>(FailureScenario::ScanlineLimit)
                                    << static_cast<int>(PlannerStatus::ComplexityLimit);
    QTest::newRow("partial-route-limit") << static_cast<int>(FailureScenario::PartialRouteLimit)
                                         << static_cast<int>(PlannerStatus::ComplexityLimit);
    QTest::newRow("topology-representative-limit") << static_cast<int>(FailureScenario::TopologyRepresentativeLimit)
                                                   << static_cast<int>(PlannerStatus::ComplexityLimit);
}

void AgriculturalSprayPlannerTest::_failureIsFailClosed()
{
    QFETCH(int, scenario);
    QFETCH(int, expectedStatus);

    PlannerInput input;
    input.spacing = 5.0;
    switch (static_cast<FailureScenario>(scenario)) {
        case FailureScenario::InvalidPolygon:
            input.inclusions = {Polygon{{{0.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}, {8.0, 0.0}}}};
            break;
        case FailureScenario::NoInclusion:
            break;
        case FailureScenario::Empty:
            input.inclusions = {rectangle(0.0, 10.0, 0.0, 10.0)};
            input.exclusions = {rectangle(-1.0, 11.0, -1.0, 11.0)};
            break;
        case FailureScenario::DisconnectedInclusions:
            input.inclusions = {
                rectangle(0.0, 10.0, 0.0, 10.0),
                rectangle(20.0, 30.0, 0.0, 10.0),
            };
            break;
        case FailureScenario::DisconnectedUnscannedInclusion:
            input.inclusions = {
                rectangle(0.0, 30.0, 0.0, 20.0),
                rectangle(40.0, 50.0, 0.1, 0.2),
            };
            break;
        case FailureScenario::NonRoutableBarrier:
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
            input.exclusions = {rectangle(14.0, 16.0, -1.0, 21.0)};
            break;
        case FailureScenario::NonRoutableCircleBarrier:
            input.inclusions = {rectangle(0.0, 100.0, 0.0, 40.0)};
            input.exclusions = {Circle{{50.0, 20.0}, 21.0}};
            break;
        case FailureScenario::ScanlineLimit:
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
            input.limits.maxScanLines = 2;
            break;
        case FailureScenario::PartialRouteLimit:
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
            input.limits.maxRoutePoints = 3;
            break;
        case FailureScenario::TopologyRepresentativeLimit:
            input.inclusions = {rectangle(0.0, 30.0, 0.0, 20.0)};
            input.limits.maxTopologyRepresentatives = 1;
            break;
    }

    const PlannerResult result = plan(input);
    QCOMPARE(static_cast<int>(result.status), expectedStatus);
    QVERIFY(!result.succeeded());
    QVERIFY(!result.error.empty());
    QVERIFY(result.legs.empty());
    QVERIFY(result.route.empty());
    QCOMPARE(result.distance, 0.0);
}

void AgriculturalSprayPlannerTest::_inputOrderAndWindingAreDeterministic()
{
    const Polygon firstInclusion = rectangle(0.0, 24.0, 0.0, 16.0);
    const Polygon secondInclusion = rectangle(16.0, 40.0, 0.0, 16.0);
    const Polygon firstExclusion = rectangle(12.0, 17.0, 6.0, 10.0);
    const Polygon secondExclusion = rectangle(15.0, 20.0, 6.0, 10.0);

    PlannerInput baselineInput;
    baselineInput.inclusions = {firstInclusion, secondInclusion};
    baselineInput.exclusions = {firstExclusion, secondExclusion};
    baselineInput.spacing = 4.0;

    PlannerInput reorderedInput = baselineInput;
    reorderedInput.inclusions = {reversed(secondInclusion), reversed(firstInclusion)};
    reorderedInput.exclusions = {reversed(secondExclusion), reversed(firstExclusion)};

    const PlannerResult baseline = plan(baselineInput);
    const PlannerResult reordered = plan(reorderedInput);
    QCOMPARE(static_cast<int>(baseline.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(reordered.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(sameResult(baseline, reordered));

    OracleRegion region;
    region.inclusionRectangles = {
        oracleRectangle(0.0, 24.0, 0.0, 16.0),
        oracleRectangle(16.0, 40.0, 0.0, 16.0),
    };
    region.exclusionRectangles = {
        oracleRectangle(12.0, 17.0, 6.0, 10.0),
        oracleRectangle(15.0, 20.0, 6.0, 10.0),
    };
    QString failure;
    QVERIFY2(validateSuccessfulResult(baseline, region, failure), qPrintable(failure));
    failure.clear();
    QVERIFY2(validateSuccessfulResult(reordered, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_circleRoutingIsDeterministic()
{
    PlannerInput baselineInput = circleRoutingInput(5.0);
    baselineInput.inclusions.push_back(rectangle(280.0, 300.0, 0.0, 200.0));
    baselineInput.exclusions.push_back(Circle{{20.0, 20.0}, 2.0});

    PlannerInput reorderedInput = baselineInput;
    std::reverse(reorderedInput.inclusions.begin(), reorderedInput.inclusions.end());
    std::reverse(reorderedInput.exclusions.begin(), reorderedInput.exclusions.end());

    const PlannerResult baseline = plan(baselineInput);
    const PlannerResult repeated = plan(baselineInput);
    const PlannerResult reordered = plan(reorderedInput);
    QCOMPARE(static_cast<int>(baseline.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(repeated.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(reordered.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(sameResult(baseline, repeated));
    QVERIFY(sameResult(baseline, reordered));

    OracleRegion region = circleRoutingRegion(5.0);
    region.inclusionRectangles.push_back(oracleRectangle(280.0, 300.0, 0.0, 200.0));
    region.exclusionCircles.push_back({{20.0, 20.0}, 2.0});
    QString failure;
    QVERIFY2(validateSuccessfulResult(baseline, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_symmetricExclusionsAreDeterministic()
{
    const Polygon firstExclusion = rectangle(12.0, 16.0, 6.0, 10.0);
    const Polygon secondExclusion = rectangle(16.0, 20.0, 10.0, 14.0);

    PlannerInput baselineInput;
    baselineInput.inclusions = {rectangle(0.0, 40.0, 0.0, 20.0)};
    baselineInput.exclusions = {firstExclusion, secondExclusion};
    baselineInput.spacing = 4.0;

    PlannerInput reorderedInput = baselineInput;
    reorderedInput.exclusions = {reversed(secondExclusion), reversed(firstExclusion)};

    const PlannerResult baseline = plan(baselineInput);
    const PlannerResult reordered = plan(reorderedInput);
    QCOMPARE(static_cast<int>(baseline.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(reordered.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(sameResult(baseline, reordered));

    OracleRegion region;
    region.inclusionRectangles = {oracleRectangle(0.0, 40.0, 0.0, 20.0)};
    region.exclusionRectangles = {
        oracleRectangle(12.0, 16.0, 6.0, 10.0),
        oracleRectangle(16.0, 20.0, 10.0, 14.0),
    };
    QString failure;
    QVERIFY2(validateSuccessfulResult(baseline, region, failure), qPrintable(failure));
}

void AgriculturalSprayPlannerTest::_nearEqualExclusionOrderingIsDeterministic()
{
    // At shared upper-right vertex (22, 14), center distances grow first→second→third
    // by roughly 0.8e-9, 0.8e-9, and 1.7e-9. The former tolerance comparator therefore
    // considered adjacent pairs tied but the endpoints distinct. Canonical geometry order is
    // deliberately the reverse (third→second→first) because their lower-left x coordinates
    // decrease in that order.
    const Polygon firstExclusion = rectangle(14.0, 22.0, 6.0, 14.0);
    const Polygon secondExclusion = rectangle(14.0 - 2.4e-9, 22.0, 6.0, 14.0);
    const Polygon thirdExclusion = rectangle(14.0 - 4.8e-9, 22.0, 6.0, 14.0);

    PlannerInput baselineInput;
    baselineInput.inclusions = {rectangle(0.0, 40.0, 0.0, 20.0)};
    baselineInput.exclusions = {firstExclusion, secondExclusion, thirdExclusion};
    baselineInput.spacing = 4.0;

    PlannerInput firstPermutation = baselineInput;
    firstPermutation.exclusions = {reversed(thirdExclusion), reversed(firstExclusion), reversed(secondExclusion)};
    PlannerInput secondPermutation = baselineInput;
    secondPermutation.exclusions = {reversed(secondExclusion), reversed(thirdExclusion), reversed(firstExclusion)};

    const PlannerResult baseline = plan(baselineInput);
    const PlannerResult first = plan(firstPermutation);
    const PlannerResult second = plan(secondPermutation);
    QCOMPARE(static_cast<int>(baseline.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(first.status), static_cast<int>(PlannerStatus::Success));
    QCOMPARE(static_cast<int>(second.status), static_cast<int>(PlannerStatus::Success));
    QVERIFY(routeHasTransitPoint(baseline));
    QVERIFY(sameResultExactly(baseline, first));
    QVERIFY(sameResultExactly(baseline, second));

    OracleRegion region;
    region.inclusionRectangles = {oracleRectangle(0.0, 40.0, 0.0, 20.0)};
    region.exclusionRectangles = {oracleRectangle(14.0 - 4.8e-9, 22.0, 6.0, 14.0)};
    QString failure;
    QVERIFY2(validateSuccessfulResult(baseline, region, failure), qPrintable(failure));
}

UT_REGISTER_TEST(AgriculturalSprayPlannerTest, TestLabel::Unit, TestLabel::MissionManager)
