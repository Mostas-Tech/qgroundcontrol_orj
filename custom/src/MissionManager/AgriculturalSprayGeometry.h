#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace AgriculturalSpray {

/// A local-NED horizontal point in metres.
struct Point
{
    double north = 0.0;
    double east = 0.0;
};

[[nodiscard]] inline Point operator+(const Point& left, const Point& right)
{
    return {left.north + right.north, left.east + right.east};
}

[[nodiscard]] inline Point operator-(const Point& left, const Point& right)
{
    return {left.north - right.north, left.east - right.east};
}

[[nodiscard]] inline Point operator*(const Point& point, double scale)
{
    return {point.north * scale, point.east * scale};
}

[[nodiscard]] inline double dot(const Point& left, const Point& right)
{
    return left.north * right.north + left.east * right.east;
}

[[nodiscard]] inline double cross(const Point& left, const Point& right)
{
    return left.north * right.east - left.east * right.north;
}

[[nodiscard]] inline double length(const Point& point)
{
    return std::hypot(point.north, point.east);
}

[[nodiscard]] inline double distance(const Point& left, const Point& right)
{
    return length(left - right);
}

[[nodiscard]] inline double geometryPointToSegmentDistance(const Point& point, const Point& first, const Point& second)
{
    const Point segment = second - first;
    const double squaredLength = dot(segment, segment);
    if (squaredLength <= std::numeric_limits<double>::epsilon()) {
        return distance(point, first);
    }
    const double parameter = std::clamp(dot(point - first, segment) / squaredLength, 0.0, 1.0);
    return distance(point, first + segment * parameter);
}

[[nodiscard]] inline bool geometryFinitePoint(const Point& point)
{
    return std::isfinite(point.north) && std::isfinite(point.east);
}

[[nodiscard]] inline double geometryCoordinateMagnitude(std::span<const Point> points)
{
    double magnitude = 1.0;
    for (const Point& point : points) {
        magnitude = std::max({magnitude, std::abs(point.north), std::abs(point.east)});
    }
    return magnitude;
}

[[nodiscard]] inline double geometryCoordinateTolerance(double magnitude)
{
    constexpr double distanceEpsilon = 1e-9;
    constexpr double roundingSafetyFactor = 16.0;
    return std::max(distanceEpsilon, roundingSafetyFactor * std::numeric_limits<double>::epsilon() * magnitude);
}

[[nodiscard]] inline double geometryCrossTolerance(const Point& left, const Point& right,
                                                   double sourceCoordinateMagnitude)
{
    constexpr double roundingSafetyFactor = 16.0;
    const double productMagnitude =
        std::max(1.0, std::abs(left.north * right.east) + std::abs(left.east * right.north));
    const double subtractionError = geometryCoordinateTolerance(sourceCoordinateMagnitude) *
                                    (std::max(1.0, length(left)) + std::max(1.0, length(right)));
    return roundingSafetyFactor * std::numeric_limits<double>::epsilon() * productMagnitude + subtractionError;
}

[[nodiscard]] inline bool geometryPointOnSegment(const Point& point, const Point& first, const Point& second)
{
    const Point segment = second - first;
    const Point toPoint = point - first;
    const Point points[] = {point, first, second};
    const double sourceMagnitude = geometryCoordinateMagnitude(points);
    if (std::abs(cross(segment, toPoint)) > geometryCrossTolerance(segment, toPoint, sourceMagnitude)) {
        return false;
    }

    const double tolerance = geometryCoordinateTolerance(sourceMagnitude);
    return point.north >= std::min(first.north, second.north) - tolerance &&
           point.north <= std::max(first.north, second.north) + tolerance &&
           point.east >= std::min(first.east, second.east) - tolerance &&
           point.east <= std::max(first.east, second.east) + tolerance;
}

[[nodiscard]] inline int geometryOrientation(const Point& first, const Point& second, const Point& third)
{
    const Point firstVector = second - first;
    const Point secondVector = third - first;
    const double value = cross(firstVector, secondVector);
    const Point points[] = {first, second, third};
    const double tolerance = geometryCrossTolerance(firstVector, secondVector, geometryCoordinateMagnitude(points));
    if (value > tolerance) {
        return 1;
    }
    if (value < -tolerance) {
        return -1;
    }
    return 0;
}

[[nodiscard]] inline bool geometrySegmentsIntersect(const Point& firstStart, const Point& firstEnd,
                                                    const Point& secondStart, const Point& secondEnd)
{
    const int firstA = geometryOrientation(firstStart, firstEnd, secondStart);
    const int firstB = geometryOrientation(firstStart, firstEnd, secondEnd);
    const int secondA = geometryOrientation(secondStart, secondEnd, firstStart);
    const int secondB = geometryOrientation(secondStart, secondEnd, firstEnd);

    if (firstA != firstB && secondA != secondB) {
        return true;
    }

    return (firstA == 0 && geometryPointOnSegment(secondStart, firstStart, firstEnd)) ||
           (firstB == 0 && geometryPointOnSegment(secondEnd, firstStart, firstEnd)) ||
           (secondA == 0 && geometryPointOnSegment(firstStart, secondStart, secondEnd)) ||
           (secondB == 0 && geometryPointOnSegment(firstEnd, secondStart, secondEnd));
}

[[nodiscard]] inline bool pointInPolygonOrBoundary(const Point& point, std::span<const Point> polygon)
{
    if (polygon.size() < 3 || !geometryFinitePoint(point)) {
        return false;
    }

    bool inside = false;
    for (std::size_t index = 0, previous = polygon.size() - 1; index < polygon.size(); previous = index++) {
        const Point& current = polygon[index];
        const Point& prior = polygon[previous];
        if (!geometryFinitePoint(current) || !geometryFinitePoint(prior)) {
            return false;
        }
        if (geometryPointOnSegment(point, prior, current)) {
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

[[nodiscard]] inline bool polygonsOverlapOrContain(std::span<const Point> source, std::span<const Point> candidate)
{
    if (source.size() < 3 || candidate.empty()) {
        return false;
    }

    for (const Point& point : candidate) {
        if (pointInPolygonOrBoundary(point, source)) {
            return true;
        }
    }
    if (candidate.size() >= 3) {
        for (const Point& point : source) {
            if (pointInPolygonOrBoundary(point, candidate)) {
                return true;
            }
        }
    }

    if (candidate.size() < 2) {
        return false;
    }
    for (std::size_t sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
        const Point& sourceStart = source[sourceIndex];
        const Point& sourceEnd = source[(sourceIndex + 1) % source.size()];
        if (!geometryFinitePoint(sourceStart) || !geometryFinitePoint(sourceEnd)) {
            return false;
        }
        for (std::size_t candidateIndex = 0; candidateIndex < candidate.size(); ++candidateIndex) {
            const Point& candidateStart = candidate[candidateIndex];
            const Point& candidateEnd = candidate[(candidateIndex + 1) % candidate.size()];
            if (geometryFinitePoint(candidateStart) && geometryFinitePoint(candidateEnd) &&
                geometrySegmentsIntersect(sourceStart, sourceEnd, candidateStart, candidateEnd)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline bool polygonsOverlapOrWithinMargin(std::span<const Point> source, std::span<const Point> candidate,
                                                        double margin)
{
    if (polygonsOverlapOrContain(source, candidate)) {
        return true;
    }
    if (!std::isfinite(margin) || margin <= 0.0 || source.size() < 3 || candidate.size() < 3) {
        return false;
    }
    for (std::size_t sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
        const Point& sourceStart = source[sourceIndex];
        const Point& sourceEnd = source[(sourceIndex + 1) % source.size()];
        for (std::size_t candidateIndex = 0; candidateIndex < candidate.size(); ++candidateIndex) {
            const Point& candidateStart = candidate[candidateIndex];
            const Point& candidateEnd = candidate[(candidateIndex + 1) % candidate.size()];
            if (geometryPointToSegmentDistance(sourceStart, candidateStart, candidateEnd) <= margin ||
                geometryPointToSegmentDistance(sourceEnd, candidateStart, candidateEnd) <= margin ||
                geometryPointToSegmentDistance(candidateStart, sourceStart, sourceEnd) <= margin ||
                geometryPointToSegmentDistance(candidateEnd, sourceStart, sourceEnd) <= margin) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline bool circleOverlapsOrContains(std::span<const Point> polygon, const Point& center, double radius)
{
    if (polygon.size() < 3 || !geometryFinitePoint(center)) {
        return false;
    }
    if (pointInPolygonOrBoundary(center, polygon)) {
        return true;
    }
    if (!std::isfinite(radius) || radius < 0.0) {
        return false;
    }

    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point& first = polygon[index];
        const Point& second = polygon[(index + 1) % polygon.size()];
        if (!geometryFinitePoint(first) || !geometryFinitePoint(second)) {
            return false;
        }
        const Point segment = second - first;
        const double segmentLengthSquared = dot(segment, segment);
        const double parameter = segmentLengthSquared > 0.0
                                     ? std::clamp(dot(center - first, segment) / segmentLengthSquared, 0.0, 1.0)
                                     : 0.0;
        if (distance(center, first + segment * parameter) <= radius) {
            return true;
        }
    }
    return false;
}

}  // namespace AgriculturalSpray
