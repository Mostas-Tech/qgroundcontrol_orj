"""Focused regression tests for the standalone agricultural coverage lab."""

from __future__ import annotations

import itertools
import math

import pytest
from shapely.geometry import LineString, Point, Polygon, box
from shapely.ops import unary_union

from tools.coverage_lab.decomposition import build_decomposition
from tools.coverage_lab.models import PlannerConfig
from tools.coverage_lab.planner import (
    CoveragePlanner,
    _OptimizationModel,
    _build_cell_variants,
    solve_exact_open_route,
)
from tools.coverage_lab.reporting import result_document
from tools.coverage_lab.routing import VisibilityRouter
from tools.coverage_lab.scenarios import Scenario, get_scenario


@pytest.mark.parametrize(
    "scenario_name",
    [
        "empty_rectangle",
        "large_circle",
        "large_polygon",
        "edge_obstacle",
        "l_field",
        "two_obstacles",
        "narrow_corridor",
    ],
)
def test_plans_preserve_swaths_and_stay_in_free_space(scenario_name: str) -> None:
    config = PlannerConfig(swath_spacing=10.0)
    scenario = get_scenario(scenario_name)
    result = CoveragePlanner(config).plan(scenario)

    assert result.baseline.metrics.valid, result.baseline.metrics.reason
    assert result.optimized.metrics.valid, result.optimized.metrics.reason
    assert result.optimized.metrics.spray_distance_m == pytest.approx(
        result.baseline.metrics.spray_distance_m,
        rel=1e-9,
        abs=1e-7,
    )
    for segment in result.optimized.segments:
        assert LineString([segment.start, segment.end]).difference(scenario.free_space).length <= 1e-7


@pytest.mark.parametrize("scenario_name", ["large_circle", "large_polygon", "two_obstacles"])
def test_bcd_cells_form_an_area_partition(scenario_name: str) -> None:
    config = PlannerConfig(swath_spacing=10.0)
    free_space = get_scenario(scenario_name).free_space
    decomposition = build_decomposition(free_space, 0.0, config)
    cell_union = unary_union([cell.geometry for cell in decomposition.cells])

    assert cell_union.symmetric_difference(free_space).area <= free_space.area * 1e-9
    for first_index, first in enumerate(decomposition.cells):
        for second in decomposition.cells[first_index + 1 :]:
            assert first.geometry.intersection(second.geometry).area <= 1e-7


def test_large_circle_avoids_repeating_the_full_obstacle_detour() -> None:
    config = PlannerConfig(swath_spacing=10.0)
    result = CoveragePlanner(config).plan(get_scenario("large_circle"))

    assert len(result.decomposition.cells) >= 4
    assert not result.optimized.used_fallback
    assert (
        result.optimized.metrics.inter_cell_transition_count
        < result.baseline.metrics.transit_connector_count
    )
    assert result.optimized.metrics.transit_distance_m < result.baseline.metrics.transit_distance_m
    assert result.optimized.metrics.estimated_time_s < result.baseline.metrics.estimated_time_s


def test_empty_rectangle_has_one_cell_and_no_regression() -> None:
    result = CoveragePlanner(PlannerConfig(swath_spacing=10.0)).plan(get_scenario("empty_rectangle"))

    active_cells = [cell for cell in result.decomposition.cells if cell.legs]
    assert len(active_cells) == 1
    assert result.optimized.metrics.estimated_time_s <= result.baseline.metrics.estimated_time_s + 1e-7
    assert result.optimized.metrics.transit_distance_m == pytest.approx(
        result.baseline.metrics.transit_distance_m,
        abs=1e-7,
    )


def test_exact_solver_matches_exhaustive_search() -> None:
    config = PlannerConfig(swath_spacing=20.0)
    field = box(0.0, 0.0, 140.0, 100.0)
    obstacle = Polygon([(52.0, 24.0), (91.0, 28.0), (96.0, 75.0), (48.0, 71.0)])
    free_space = field.difference(obstacle)
    decomposition = build_decomposition(free_space, 0.0, config)
    router = VisibilityRouter(free_space, config)
    variants = _build_cell_variants(decomposition, router, config)
    model = _OptimizationModel(variants, router, config, entry=None)

    exact_cost, exact_sequence = solve_exact_open_route(model)
    cells = tuple(sorted(model.variants_by_cell))
    brute_force_cost = math.inf
    for cell_order in itertools.permutations(cells):
        choices = [model.variants_by_cell[cell_id] for cell_id in cell_order]
        for sequence in itertools.product(*choices):
            brute_force_cost = min(brute_force_cost, model.sequence_cost(sequence))

    assert exact_sequence
    assert exact_cost == pytest.approx(brute_force_cost, rel=1e-10, abs=1e-8)


def test_selected_field_vertex_defines_sweep_direction_and_nearest_start() -> None:
    field = Polygon([(0.0, 0.0), (120.0, 0.0), (120.0, 80.0), (0.0, 80.0)])
    scenario = Scenario(
        name="vertex_selected_rectangle",
        field=field,
        obstacles=(),
        description="Rectangle used to exercise field-edge selection.",
    )
    result = CoveragePlanner(PlannerConfig(swath_spacing=10.0)).plan(
        scenario,
        field_vertex_index=1,
    )

    assert result.optimized.metrics.valid
    assert result.selection.vertex_index == 1
    assert result.selection.point == (120.0, 0.0)
    assert result.selection.edge_end == (120.0, 80.0)
    assert result.optimized.angle_degrees == pytest.approx(90.0)

    spray_segments = [segment for segment in result.optimized.segments if segment.spray]
    selected_point = Point(result.selection.point)
    first_line_distance = selected_point.distance(
        LineString([spray_segments[0].start, spray_segments[0].end])
    )
    nearest_line_distance = min(
        selected_point.distance(LineString([leg.left, leg.right]))
        for leg in result.decomposition.legs
    )
    assert first_line_distance == pytest.approx(nearest_line_distance, abs=1e-7)
    assert result.optimized.segments[0].start == result.selection.point


def test_first_field_vertex_is_the_default_selection() -> None:
    scenario = Scenario(
        name="default_vertex_rectangle",
        field=Polygon([(0.0, 0.0), (120.0, 0.0), (120.0, 80.0), (0.0, 80.0)]),
        obstacles=(),
        description="Rectangle used to verify default field-edge selection.",
    )

    result = CoveragePlanner(PlannerConfig(swath_spacing=10.0)).plan(scenario)

    assert result.selection.vertex_index == 0
    assert result.selection.point == (0.0, 0.0)
    assert result.selection.edge_end == (120.0, 0.0)
    assert result.optimized.angle_degrees == pytest.approx(0.0)


def test_slanted_field_edge_produces_its_exact_sweep_heading() -> None:
    scenario = Scenario(
        name="slanted_field",
        field=Polygon([(0.0, 0.0), (120.0, 30.0), (100.0, 110.0), (-20.0, 80.0)]),
        obstacles=(),
        description="Slanted parallelogram used to verify arbitrary edge headings.",
    )

    result = CoveragePlanner(PlannerConfig(swath_spacing=10.0)).plan(scenario)

    assert result.optimized.metrics.valid, result.optimized.metrics.reason
    assert result.selection.angle_degrees == pytest.approx(math.degrees(math.atan2(30.0, 120.0)))
    first_spray = next(segment for segment in result.optimized.segments if segment.spray)
    selected_point = Point(result.selection.point)
    assert selected_point.distance(LineString([first_spray.start, first_spray.end])) == pytest.approx(
        min(
            selected_point.distance(LineString([leg.left, leg.right]))
            for leg in result.decomposition.legs
        ),
        abs=1e-7,
    )


def test_field_vertex_index_must_address_an_exterior_polygon_point() -> None:
    planner = CoveragePlanner(PlannerConfig(swath_spacing=10.0))

    with pytest.raises(ValueError, match="valid range 0..3"):
        planner.plan(get_scenario("empty_rectangle"), field_vertex_index=4)


def test_seeded_stress_scenario_is_deterministic_and_uses_bounded_optimizer() -> None:
    first = get_scenario("seeded_stress")
    second = get_scenario("seeded_stress")
    assert [obstacle.wkt for obstacle in first.obstacles] == [obstacle.wkt for obstacle in second.obstacles]

    config = PlannerConfig(swath_spacing=40.0, exact_cell_limit=4)
    result = CoveragePlanner(config).plan(first)
    assert result.optimized.metrics.valid
    assert "cheapest_insertion" in result.optimized.method or result.optimized.used_fallback


def test_metrics_document_contains_comparable_outputs() -> None:
    result = CoveragePlanner(PlannerConfig(swath_spacing=20.0)).plan(get_scenario("large_circle"))
    document = result_document(result)

    assert document["baseline"]["spray_distance_m"] == pytest.approx(
        document["optimized"]["spray_distance_m"]
    )
    assert "estimated_time_percent" in document["improvement"]
    assert document["decomposition"]["cell_count"] >= 4
    assert document["field_edge_selection"]["vertex_index"] == 0
