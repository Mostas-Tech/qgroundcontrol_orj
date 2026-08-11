#pragma once

#include <cmath>

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

}  // namespace AgriculturalSpray
