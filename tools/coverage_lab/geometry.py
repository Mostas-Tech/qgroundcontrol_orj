"""Geometry and kinematic helpers for the coverage planning laboratory."""

from __future__ import annotations

import math
from collections.abc import Iterable, Iterator

from shapely import affinity
from shapely.geometry import LineString, MultiLineString, MultiPolygon, Polygon
from shapely.geometry.base import BaseGeometry
from shapely.ops import unary_union

from .models import PlannerConfig, Point2D, RouteMetrics, RouteSegment


def distance(start: Point2D, end: Point2D) -> float:
    return math.hypot(end[0] - start[0], end[1] - start[1])


def heading(start: Point2D, end: Point2D) -> float:
    return math.atan2(end[1] - start[1], end[0] - start[0])


def heading_delta_degrees(first: float, second: float) -> float:
    delta = math.degrees(second - first)
    return abs((delta + 180.0) % 360.0 - 180.0)


def rotate_point(point: Point2D, angle_degrees: float) -> Point2D:
    normalized_angle = angle_degrees % 360.0
    if math.isclose(normalized_angle, 0.0, abs_tol=1e-12):
        return point
    if math.isclose(normalized_angle, 90.0, abs_tol=1e-12):
        return -point[1], point[0]
    if math.isclose(normalized_angle, 180.0, abs_tol=1e-12):
        return -point[0], -point[1]
    if math.isclose(normalized_angle, 270.0, abs_tol=1e-12):
        return point[1], -point[0]
    angle_radians = math.radians(angle_degrees)
    cosine = math.cos(angle_radians)
    sine = math.sin(angle_radians)
    return (
        point[0] * cosine - point[1] * sine,
        point[0] * sine + point[1] * cosine,
    )


def rotate_geometry(geometry: BaseGeometry, angle_degrees: float) -> BaseGeometry:
    return affinity.rotate(geometry, angle_degrees, origin=(0.0, 0.0), use_radians=False)


def iter_polygons(geometry: BaseGeometry) -> Iterator[Polygon]:
    if geometry.is_empty:
        return
    if isinstance(geometry, Polygon):
        yield geometry
        return
    if isinstance(geometry, MultiPolygon):
        yield from geometry.geoms
        return
    if hasattr(geometry, "geoms"):
        for child in geometry.geoms:
            yield from iter_polygons(child)


def iter_lines(geometry: BaseGeometry) -> Iterator[LineString]:
    if geometry.is_empty:
        return
    if isinstance(geometry, LineString):
        yield geometry
        return
    if isinstance(geometry, MultiLineString):
        yield from geometry.geoms
        return
    if hasattr(geometry, "geoms"):
        for child in geometry.geoms:
            yield from iter_lines(child)


def boundary_vertices(geometry: BaseGeometry) -> tuple[Point2D, ...]:
    vertices: set[Point2D] = set()
    for polygon in iter_polygons(geometry):
        rings = (polygon.exterior, *polygon.interiors)
        for ring in rings:
            for x_coordinate, y_coordinate in tuple(ring.coords)[:-1]:
                vertices.add((float(x_coordinate), float(y_coordinate)))
    return tuple(sorted(vertices))


def segment_travel_time(length: float, speed: float, acceleration: float) -> float:
    """Rest-to-rest trapezoidal/triangular travel-time estimate."""

    if length <= 0.0:
        return 0.0
    acceleration_distance = speed * speed / acceleration
    if length <= acceleration_distance:
        return 2.0 * math.sqrt(length / acceleration)
    return 2.0 * speed / acceleration + (length - acceleration_distance) / speed


def turn_time(previous_heading: float, next_heading: float, config: PlannerConfig) -> tuple[float, int]:
    delta_degrees = heading_delta_degrees(previous_heading, next_heading)
    if delta_degrees <= config.yaw_threshold_degrees:
        return 0.0, 0
    return delta_degrees / config.yaw_rate_degrees + config.yaw_settle_seconds, 1


def route_time(
    segments: Iterable[RouteSegment],
    config: PlannerConfig,
    initial_heading: float | None = None,
) -> tuple[float, int]:
    total_time = 0.0
    turn_count = 0
    previous_heading = initial_heading
    for segment in segments:
        length = distance(segment.start, segment.end)
        if length <= config.geometry_tolerance:
            continue
        current_heading = heading(segment.start, segment.end)
        if previous_heading is not None:
            penalty, added_turns = turn_time(previous_heading, current_heading, config)
            total_time += penalty
            turn_count += added_turns
        speed = config.spray_speed if segment.spray else config.transit_speed
        total_time += segment_travel_time(length, speed, config.acceleration)
        previous_heading = current_heading
    return total_time, turn_count


def _count_connector_groups(segments: tuple[RouteSegment, ...]) -> tuple[int, int]:
    connector_count = 0
    inter_cell_count = 0
    previous_kind = ""
    for segment in segments:
        if segment.spray:
            previous_kind = segment.kind
            continue
        if segment.kind != previous_kind:
            connector_count += 1
            if segment.kind == "transit_between":
                inter_cell_count += 1
        previous_kind = segment.kind
    return connector_count, inter_cell_count


def _coverage_ratio(
    segments: tuple[RouteSegment, ...],
    free_space: BaseGeometry,
    swath_spacing: float,
) -> float:
    spray_lines = [LineString([segment.start, segment.end]) for segment in segments if segment.spray]
    if not spray_lines or free_space.area <= 0.0:
        return 0.0
    footprints = [line.buffer(swath_spacing / 2.0, cap_style=2) for line in spray_lines]
    covered_area = unary_union(footprints).intersection(free_space).area
    return min(1.0, max(0.0, covered_area / free_space.area))


def route_metrics(
    segments: tuple[RouteSegment, ...],
    free_space: BaseGeometry,
    config: PlannerConfig,
    *,
    expected_spray_distance: float | None = None,
) -> RouteMetrics:
    total_distance = sum(distance(segment.start, segment.end) for segment in segments)
    spray_distance = sum(distance(segment.start, segment.end) for segment in segments if segment.spray)
    transit_distance = total_distance - spray_distance
    estimated_time, turns = route_time(segments, config)
    connector_count, inter_cell_count = _count_connector_groups(segments)
    coverage_ratio = _coverage_ratio(segments, free_space, config.swath_spacing)

    reason = ""
    valid = bool(segments)
    if expected_spray_distance is not None and not math.isclose(
        spray_distance,
        expected_spray_distance,
        rel_tol=1e-8,
        abs_tol=config.geometry_tolerance,
    ):
        valid = False
        reason = "spray distance changed while reordering swaths"

    if valid:
        tolerance_space = free_space.buffer(config.geometry_tolerance)
        for segment in segments:
            line = LineString([segment.start, segment.end])
            if not tolerance_space.covers(line):
                valid = False
                reason = f"{segment.kind} segment leaves navigable free space"
                break

    return RouteMetrics(
        valid=valid,
        total_distance_m=total_distance,
        spray_distance_m=spray_distance,
        transit_distance_m=transit_distance,
        estimated_time_s=estimated_time,
        turn_count=turns,
        transit_connector_count=connector_count,
        inter_cell_transition_count=inter_cell_count,
        coverage_ratio=coverage_ratio,
        reason=reason,
    )
