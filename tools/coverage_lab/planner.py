"""Legacy and BCD-based agricultural coverage planners."""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass, replace
from typing import TYPE_CHECKING

from shapely.geometry import LineString, Point
from shapely.ops import unary_union

from .decomposition import Decomposition, build_decomposition
from .geometry import distance, heading, route_metrics, route_time, turn_time
from .models import (
    CellVariant,
    PlannedRoute,
    PlannerConfig,
    Point2D,
    RouteSegment,
    SprayLeg,
)
from .routing import VisibilityRouter

if TYPE_CHECKING:
    from shapely.geometry.base import BaseGeometry

    from .scenarios import Scenario


@dataclass(frozen=True)
class FieldEdgeSelection:
    """A tapped field vertex and its outgoing boundary edge."""

    vertex_index: int
    point: Point2D
    edge_end: Point2D
    angle_degrees: float


@dataclass(frozen=True)
class ScenarioResult:
    """The baseline and selected optimized route for one scenario."""

    scenario: Scenario
    selection: FieldEdgeSelection
    decomposition: Decomposition
    baseline: PlannedRoute
    optimized: PlannedRoute
    visibility_node_count: int


def select_field_edge(
    scenario: Scenario,
    vertex_index: int,
    tolerance: float,
) -> FieldEdgeSelection:
    """Resolve one exterior vertex to the outgoing edge that defines the sweep direction."""

    coordinates = tuple(
        (float(east), float(north)) for east, north in tuple(scenario.field.exterior.coords)[:-1]
    )
    if not coordinates:
        raise ValueError("Field polygon has no selectable exterior vertices")
    if vertex_index < 0 or vertex_index >= len(coordinates):
        raise ValueError(
            f"Field vertex index {vertex_index} is outside the valid range 0..{len(coordinates) - 1}"
        )
    point = coordinates[vertex_index]
    edge_end = coordinates[(vertex_index + 1) % len(coordinates)]
    if distance(point, edge_end) <= tolerance:
        raise ValueError(f"Field vertex {vertex_index} starts a zero-length boundary edge")
    angle_degrees = math.degrees(heading(point, edge_end)) % 180.0
    return FieldEdgeSelection(
        vertex_index=vertex_index,
        point=point,
        edge_end=edge_end,
        angle_degrees=angle_degrees,
    )


def _segment_direction(segment: RouteSegment) -> Point2D:
    length = distance(segment.start, segment.end)
    if length == 0.0:
        return (0.0, 0.0)
    return (
        (segment.end[0] - segment.start[0]) / length,
        (segment.end[1] - segment.start[1]) / length,
    )


def _last_spray_segment(segments: tuple[RouteSegment, ...]) -> RouteSegment:
    return next(segment for segment in reversed(segments) if segment.spray)


def _spray_line_distance(point: Point2D, segment: RouteSegment) -> float:
    return Point(point).distance(LineString([segment.start, segment.end]))


def _spray_segment(leg: SprayLeg, reverse: bool, cell_id: int | None) -> RouteSegment:
    start, end = (leg.right, leg.left) if reverse else (leg.left, leg.right)
    return RouteSegment(start=start, end=end, spray=True, kind="spray", cell_id=cell_id)


def _ordered_rows(legs: list[SprayLeg], reverse_rows: bool) -> list[list[SprayLeg]]:
    grouped: dict[int, list[SprayLeg]] = {}
    for leg in legs:
        grouped.setdefault(leg.row_index, []).append(leg)
    row_indices = sorted(grouped, reverse=reverse_rows)
    return [grouped[row_index] for row_index in row_indices]


def _ordered_spray_segments(
    legs: list[SprayLeg],
    *,
    reverse_rows: bool,
    start_from_right: bool,
    cell_id: int | None,
) -> list[RouteSegment]:
    ordered: list[RouteSegment] = []
    for row_position, row in enumerate(_ordered_rows(legs, reverse_rows)):
        reverse = start_from_right if row_position % 2 == 0 else not start_from_right
        row_legs = sorted(row, key=lambda leg: leg.local_x_min, reverse=reverse)
        ordered.extend(_spray_segment(leg, reverse=reverse, cell_id=cell_id) for leg in row_legs)
    return ordered


def _connect_spray_segments(
    spray_segments: list[RouteSegment],
    router: VisibilityRouter,
    *,
    connector_kind: str,
) -> tuple[RouteSegment, ...] | None:
    if not spray_segments:
        return ()
    route: list[RouteSegment] = [spray_segments[0]]
    previous_spray = spray_segments[0]
    for spray_segment in spray_segments[1:]:
        connector = router.route_segments(
            previous_spray.end,
            spray_segment.start,
            kind=connector_kind,
            initial_direction=_segment_direction(previous_spray),
        )
        if connector is None:
            return None
        route.extend(connector)
        route.append(spray_segment)
        previous_spray = spray_segment
    return tuple(route)


def _prepend_entry(
    segments: tuple[RouteSegment, ...],
    entry: Point2D | None,
    router: VisibilityRouter,
) -> tuple[RouteSegment, ...] | None:
    if entry is None or not segments:
        return segments
    connector = router.route_segments(entry, segments[0].start, kind="transit_entry")
    if connector is None:
        return None
    return (*connector, *segments)


def _build_legacy_route(
    decomposition: Decomposition,
    router: VisibilityRouter,
    config: PlannerConfig,
    entry: Point2D | None,
) -> PlannedRoute:
    orderings = (
        _ordered_spray_segments(
            list(decomposition.legs),
            reverse_rows=reverse_rows,
            start_from_right=start_from_right,
            cell_id=None,
        )
        for reverse_rows, start_from_right in itertools.product((False, True), repeat=2)
    )
    spray_segments = min(
        (segments for segments in orderings if segments),
        key=lambda segments: (
            _spray_line_distance(entry, segments[0]) if entry is not None else 0.0,
            distance(entry, segments[0].start) if entry is not None else 0.0,
        ),
        default=[],
    )
    connected = _connect_spray_segments(spray_segments, router, connector_kind="transit_legacy")
    if connected is None:
        connected = ()
    with_entry = _prepend_entry(connected, entry, router)
    segments = with_entry or ()
    expected_spray_distance = sum(leg.length for leg in decomposition.legs)
    metrics = route_metrics(
        segments,
        decomposition.free_space,
        config,
        expected_spray_distance=expected_spray_distance,
    )
    return PlannedRoute(
        method="legacy_global_scanline",
        angle_degrees=decomposition.angle_degrees,
        segments=segments,
        metrics=metrics,
    )


def _build_cell_variants(
    decomposition: Decomposition,
    router: VisibilityRouter,
    config: PlannerConfig,
) -> tuple[CellVariant, ...]:
    variants: list[CellVariant] = []
    next_variant_id = 0
    for cell in decomposition.cells:
        if not cell.legs:
            continue
        for reverse_rows, start_from_right in itertools.product((False, True), repeat=2):
            spray_segments = _ordered_spray_segments(
                cell.legs,
                reverse_rows=reverse_rows,
                start_from_right=start_from_right,
                cell_id=cell.cell_id,
            )
            connected = _connect_spray_segments(
                spray_segments,
                router,
                connector_kind="transit_internal",
            )
            if not connected:
                continue
            intrinsic_time, _ = route_time(connected, config)
            variants.append(
                CellVariant(
                    variant_id=next_variant_id,
                    cell_id=cell.cell_id,
                    reverse_rows=reverse_rows,
                    start_from_right=start_from_right,
                    segments=connected,
                    intrinsic_time_seconds=intrinsic_time,
                )
            )
            next_variant_id += 1
    return tuple(variants)


class _OptimizationModel:
    def __init__(
        self,
        variants: tuple[CellVariant, ...],
        router: VisibilityRouter,
        config: PlannerConfig,
        entry: Point2D | None,
    ) -> None:
        self.variants = variants
        self.router = router
        self.config = config
        self.entry = entry
        self.variants_by_cell: dict[int, tuple[int, ...]] = {}
        for index, variant in enumerate(variants):
            self.variants_by_cell.setdefault(variant.cell_id, ())
            self.variants_by_cell[variant.cell_id] = (
                *self.variants_by_cell[variant.cell_id],
                index,
            )
        self._transition_segment_cache: dict[tuple[int, int], tuple[RouteSegment, ...] | None] = {}
        self._transition_cost_cache: dict[tuple[int, int], float] = {}
        self._entry_segment_cache: dict[int, tuple[RouteSegment, ...] | None] = {}
        self._entry_cost_cache: dict[int, float] = {}
        self.allowed_start_variants = self._nearest_start_variants()

    def _nearest_start_variants(self) -> frozenset[int]:
        if self.entry is None:
            return frozenset(range(len(self.variants)))
        distances = [
            _spray_line_distance(self.entry, variant.segments[0]) for variant in self.variants
        ]
        if not distances:
            return frozenset()
        nearest_distance = min(distances)
        return frozenset(
            index
            for index, candidate_distance in enumerate(distances)
            if candidate_distance <= nearest_distance + self.config.geometry_tolerance
        )

    def transition_segments(
        self, first_index: int, second_index: int
    ) -> tuple[RouteSegment, ...] | None:
        key = (first_index, second_index)
        if key in self._transition_segment_cache:
            return self._transition_segment_cache[key]
        first = self.variants[first_index]
        second = self.variants[second_index]
        previous_spray = _last_spray_segment(first.segments)
        connector = self.router.route_segments(
            first.end,
            second.start,
            kind="transit_between",
            initial_direction=_segment_direction(previous_spray),
        )
        self._transition_segment_cache[key] = connector
        return connector

    def transition_cost(self, first_index: int, second_index: int) -> float:
        key = (first_index, second_index)
        if key in self._transition_cost_cache:
            return self._transition_cost_cache[key]
        first = self.variants[first_index]
        second = self.variants[second_index]
        connector = self.transition_segments(first_index, second_index)
        if connector is None:
            self._transition_cost_cache[key] = math.inf
            return math.inf
        previous_heading = heading(first.segments[-1].start, first.segments[-1].end)
        connector_time, _ = route_time(connector, self.config, initial_heading=previous_heading)
        last_heading = previous_heading
        if connector:
            last_heading = heading(connector[-1].start, connector[-1].end)
        next_heading = heading(second.segments[0].start, second.segments[0].end)
        final_turn_time, _ = turn_time(last_heading, next_heading, self.config)
        cost = connector_time + final_turn_time
        self._transition_cost_cache[key] = cost
        return cost

    def entry_segments(self, variant_index: int) -> tuple[RouteSegment, ...] | None:
        if variant_index in self._entry_segment_cache:
            return self._entry_segment_cache[variant_index]
        if self.entry is None:
            segments: tuple[RouteSegment, ...] | None = ()
        else:
            segments = self.router.route_segments(
                self.entry,
                self.variants[variant_index].start,
                kind="transit_entry",
            )
        self._entry_segment_cache[variant_index] = segments
        return segments

    def entry_cost(self, variant_index: int) -> float:
        if variant_index in self._entry_cost_cache:
            return self._entry_cost_cache[variant_index]
        if variant_index not in self.allowed_start_variants:
            self._entry_cost_cache[variant_index] = math.inf
            return math.inf
        connector = self.entry_segments(variant_index)
        if connector is None:
            self._entry_cost_cache[variant_index] = math.inf
            return math.inf
        connector_time, _ = route_time(connector, self.config)
        if connector:
            connector_heading = heading(connector[-1].start, connector[-1].end)
            first_segment = self.variants[variant_index].segments[0]
            first_heading = heading(first_segment.start, first_segment.end)
            final_turn_time, _ = turn_time(connector_heading, first_heading, self.config)
            connector_time += final_turn_time
        self._entry_cost_cache[variant_index] = connector_time
        return connector_time

    def sequence_cost(self, sequence: tuple[int, ...]) -> float:
        if not sequence:
            return math.inf
        cost = self.entry_cost(sequence[0]) + self.variants[sequence[0]].intrinsic_time_seconds
        for first_index, second_index in itertools.pairwise(sequence):
            cost += self.transition_cost(first_index, second_index)
            cost += self.variants[second_index].intrinsic_time_seconds
        return cost

    def materialize(self, sequence: tuple[int, ...]) -> tuple[RouteSegment, ...] | None:
        if not sequence:
            return None
        entry_segments = self.entry_segments(sequence[0])
        if entry_segments is None:
            return None
        materialized: list[RouteSegment] = [*entry_segments, *self.variants[sequence[0]].segments]
        for first_index, second_index in itertools.pairwise(sequence):
            connector = self.transition_segments(first_index, second_index)
            if connector is None:
                return None
            materialized.extend(connector)
            materialized.extend(self.variants[second_index].segments)
        return tuple(materialized)


def solve_exact_open_route(model: _OptimizationModel) -> tuple[float, tuple[int, ...]]:
    """Solve the open generalized TSP using bitmask dynamic programming."""

    cells = tuple(sorted(model.variants_by_cell))
    cell_bits = {cell_id: 1 << index for index, cell_id in enumerate(cells)}
    full_mask = (1 << len(cells)) - 1
    states_by_mask: dict[int, dict[int, float]] = {}
    parents: dict[tuple[int, int], tuple[int, int] | None] = {}

    for variant_index, variant in enumerate(model.variants):
        mask = cell_bits[variant.cell_id]
        cost = model.entry_cost(variant_index) + variant.intrinsic_time_seconds
        if math.isfinite(cost):
            states_by_mask.setdefault(mask, {})[variant_index] = cost
            parents[(mask, variant_index)] = None

    for mask in range(1, full_mask + 1):
        for last_index, current_cost in tuple(states_by_mask.get(mask, {}).items()):
            for next_index, next_variant in enumerate(model.variants):
                next_bit = cell_bits[next_variant.cell_id]
                if mask & next_bit:
                    continue
                transition_cost = model.transition_cost(last_index, next_index)
                candidate_cost = (
                    current_cost + transition_cost + next_variant.intrinsic_time_seconds
                )
                next_state = (mask | next_bit, next_index)
                next_states = states_by_mask.setdefault(next_state[0], {})
                incumbent = next_states.get(next_index, math.inf)
                if candidate_cost + model.config.geometry_tolerance < incumbent:
                    next_states[next_index] = candidate_cost
                    parents[next_state] = (mask, last_index)

    final_states = [
        (cost, variant_index) for variant_index, cost in states_by_mask.get(full_mask, {}).items()
    ]
    if not final_states:
        return math.inf, ()
    best_cost, final_index = min(final_states)
    state: tuple[int, int] | None = (full_mask, final_index)
    reversed_sequence: list[int] = []
    while state is not None:
        reversed_sequence.append(state[1])
        state = parents[state]
    return best_cost, tuple(reversed(reversed_sequence))


def _best_variants_for_cell_order(
    model: _OptimizationModel,
    cell_order: tuple[int, ...],
) -> tuple[float, tuple[int, ...]]:
    if not cell_order:
        return math.inf, ()
    costs: dict[int, float] = {}
    paths: dict[int, tuple[int, ...]] = {}
    for variant_index in model.variants_by_cell[cell_order[0]]:
        costs[variant_index] = (
            model.entry_cost(variant_index) + model.variants[variant_index].intrinsic_time_seconds
        )
        paths[variant_index] = (variant_index,)
    for cell_id in cell_order[1:]:
        next_costs: dict[int, float] = {}
        next_paths: dict[int, tuple[int, ...]] = {}
        for next_index in model.variants_by_cell[cell_id]:
            candidates = [
                (
                    current_cost
                    + model.transition_cost(current_index, next_index)
                    + model.variants[next_index].intrinsic_time_seconds,
                    current_path,
                )
                for current_index, current_cost in costs.items()
                for current_path in (paths[current_index],)
            ]
            best_cost, best_path = min(candidates)
            next_costs[next_index] = best_cost
            next_paths[next_index] = (*best_path, next_index)
        costs = next_costs
        paths = next_paths
    final_index = min(costs, key=costs.__getitem__)
    return costs[final_index], paths[final_index]


def solve_heuristic_open_route(model: _OptimizationModel) -> tuple[float, tuple[int, ...]]:
    """Deterministic cheapest insertion followed by bounded 2-opt and Or-opt."""

    cells = set(model.variants_by_cell)
    if not cells:
        return math.inf, ()
    first_index = min(
        range(len(model.variants)),
        key=lambda index: (
            model.entry_cost(index) + model.variants[index].intrinsic_time_seconds,
            index,
        ),
    )
    sequence = (first_index,)
    cells.remove(model.variants[first_index].cell_id)

    while cells:
        candidate_cells = cells
        if len(cells) > 20:
            route_endpoints = [model.variants[index].start for index in sequence]
            route_endpoints.extend(model.variants[index].end for index in sequence)

            def proximity(cell_id: int, route_endpoints: list[Point2D] = route_endpoints) -> float:
                return min(
                    distance(endpoint, model.variants[index].start)
                    for endpoint in route_endpoints
                    for index in model.variants_by_cell[cell_id]
                )

            candidate_cells = set(
                sorted(cells, key=lambda cell_id: (proximity(cell_id), cell_id))[:12]
            )

        best: tuple[float, tuple[int, ...], int] | None = None
        for cell_id in sorted(candidate_cells):
            for variant_index in model.variants_by_cell[cell_id]:
                for position in range(len(sequence) + 1):
                    candidate = (*sequence[:position], variant_index, *sequence[position:])
                    cost = model.sequence_cost(candidate)
                    ranking = (cost, candidate, cell_id)
                    if best is None or ranking < best:
                        best = ranking
        if best is None or not math.isfinite(best[0]):
            return math.inf, ()
        sequence = best[1]
        cells.remove(best[2])

    best_cost = model.sequence_cost(sequence)
    cell_order = tuple(model.variants[index].cell_id for index in sequence)
    improvement_passes = 0
    improved = True
    while improved and improvement_passes < 6:
        improved = False
        improvement_passes += 1
        for first in range(len(cell_order) - 1):
            for last in range(first + 1, min(len(cell_order), first + 9)):
                candidate_order = (
                    *cell_order[:first],
                    *reversed(cell_order[first : last + 1]),
                    *cell_order[last + 1 :],
                )
                candidate_cost, candidate_sequence = _best_variants_for_cell_order(
                    model, candidate_order
                )
                if candidate_cost + model.config.geometry_tolerance < best_cost:
                    best_cost = candidate_cost
                    sequence = candidate_sequence
                    cell_order = candidate_order
                    improved = True
                    break
            if improved:
                break
        if improved:
            continue
        for source in range(len(cell_order)):
            reduced = (*cell_order[:source], *cell_order[source + 1 :])
            for destination in range(len(reduced) + 1):
                candidate_order = (
                    *reduced[:destination],
                    cell_order[source],
                    *reduced[destination:],
                )
                if candidate_order == cell_order:
                    continue
                candidate_cost, candidate_sequence = _best_variants_for_cell_order(
                    model, candidate_order
                )
                if candidate_cost + model.config.geometry_tolerance < best_cost:
                    best_cost = candidate_cost
                    sequence = candidate_sequence
                    cell_order = candidate_order
                    improved = True
                    break
            if improved:
                break
    return best_cost, sequence


class CoveragePlanner:
    """Runs the baseline, BCD, optimizer, and safety fallback pipeline."""

    def __init__(self, config: PlannerConfig | None = None) -> None:
        self.config = config or PlannerConfig()

    def _navigation_space(self, scenario: Scenario) -> BaseGeometry:
        if not scenario.obstacles:
            return scenario.field
        expanded_obstacles = unary_union(
            [
                obstacle.buffer(self.config.obstacle_clearance, join_style="mitre")
                for obstacle in scenario.obstacles
            ]
        )
        return scenario.field.difference(expanded_obstacles)

    def _plan_angle(
        self,
        scenario: Scenario,
        router: VisibilityRouter,
        angle_degrees: float,
        entry: Point2D | None,
        selection: FieldEdgeSelection,
    ) -> ScenarioResult:
        decomposition = build_decomposition(
            self._navigation_space(scenario), angle_degrees, self.config
        )
        baseline = _build_legacy_route(decomposition, router, self.config, entry)
        variants = _build_cell_variants(decomposition, router, self.config)
        active_cell_count = len({variant.cell_id for variant in variants})
        model = _OptimizationModel(variants, router, self.config, entry)

        if active_cell_count == 0:
            sequence: tuple[int, ...] = ()
            method = "bcd_no_cells"
        elif active_cell_count <= self.config.exact_cell_limit:
            _, sequence = solve_exact_open_route(model)
            method = "bcd_exact_dp"
        else:
            _, sequence = solve_heuristic_open_route(model)
            method = "bcd_cheapest_insertion"

        optimized_segments = model.materialize(sequence) or ()
        expected_spray_distance = sum(leg.length for leg in decomposition.legs)
        optimized_metrics = route_metrics(
            optimized_segments,
            decomposition.free_space,
            self.config,
            expected_spray_distance=expected_spray_distance,
        )
        cell_order = tuple(model.variants[index].cell_id for index in sequence)
        variant_order = tuple(model.variants[index].variant_id for index in sequence)
        optimized = PlannedRoute(
            method=method,
            angle_degrees=angle_degrees,
            segments=optimized_segments,
            metrics=optimized_metrics,
            cell_order=cell_order,
            variant_order=variant_order,
        )

        if baseline.metrics.valid and (
            not optimized.metrics.valid
            or optimized.metrics.estimated_time_s
            > baseline.metrics.estimated_time_s + self.config.geometry_tolerance
        ):
            optimized = replace(
                baseline,
                method=f"{method}_fallback_legacy",
                used_fallback=True,
            )
        return ScenarioResult(
            scenario=scenario,
            selection=selection,
            decomposition=decomposition,
            baseline=baseline,
            optimized=optimized,
            visibility_node_count=router.node_count,
        )

    def plan(
        self,
        scenario: Scenario,
        *,
        field_vertex_index: int = 0,
    ) -> ScenarioResult:
        selection = select_field_edge(
            scenario,
            field_vertex_index,
            self.config.geometry_tolerance,
        )
        free_space = self._navigation_space(scenario)
        router = VisibilityRouter(free_space, self.config)
        return self._plan_angle(
            scenario,
            router,
            selection.angle_degrees,
            selection.point,
            selection,
        )
