"""Shared data types for the coverage planning laboratory."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, TypeAlias

if TYPE_CHECKING:
    from shapely.geometry.base import BaseGeometry

Point2D: TypeAlias = tuple[float, float]


@dataclass(frozen=True)
class PlannerConfig:
    """Vehicle and planner assumptions used by every scenario."""

    swath_spacing: float = 2.5
    spray_speed: float = 7.0
    transit_speed: float = 10.0
    acceleration: float = 2.5
    yaw_rate_degrees: float = 90.0
    yaw_threshold_degrees: float = 5.0
    yaw_settle_seconds: float = 0.3
    exact_cell_limit: int = 12
    geometry_tolerance: float = 1e-7
    obstacle_clearance: float = 0.05


@dataclass(frozen=True)
class SprayLeg:
    """One clipped swath interval, stored in world coordinates."""

    row_index: int
    local_y: float
    local_x_min: float
    local_x_max: float
    left: Point2D
    right: Point2D

    @property
    def length(self) -> float:
        return self.local_x_max - self.local_x_min


@dataclass
class CoverageCell:
    """A Boustrophedon cell and the spray legs assigned to it."""

    cell_id: int
    geometry: BaseGeometry
    legs: list[SprayLeg] = field(default_factory=list)


@dataclass(frozen=True)
class RouteSegment:
    """A straight segment in a planned route."""

    start: Point2D
    end: Point2D
    spray: bool
    kind: str
    cell_id: int | None = None


@dataclass(frozen=True)
class CellVariant:
    """One of the four traversal choices for a coverage cell."""

    variant_id: int
    cell_id: int
    reverse_rows: bool
    start_from_right: bool
    segments: tuple[RouteSegment, ...]
    intrinsic_time_seconds: float

    @property
    def start(self) -> Point2D:
        return self.segments[0].start

    @property
    def end(self) -> Point2D:
        return self.segments[-1].end


@dataclass(frozen=True)
class RouteMetrics:
    """Comparable metrics for a complete open route."""

    valid: bool
    total_distance_m: float
    spray_distance_m: float
    transit_distance_m: float
    estimated_time_s: float
    turn_count: int
    transit_connector_count: int
    inter_cell_transition_count: int
    coverage_ratio: float
    reason: str = ""


@dataclass(frozen=True)
class PlannedRoute:
    """A materialized route and its metrics."""

    method: str
    angle_degrees: float
    segments: tuple[RouteSegment, ...]
    metrics: RouteMetrics
    cell_order: tuple[int, ...] = ()
    variant_order: tuple[int, ...] = ()
    used_fallback: bool = False
