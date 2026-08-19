"""Deterministic synthetic fields used by the coverage planner laboratory."""

from __future__ import annotations

import math
import random
from dataclasses import dataclass

from shapely.geometry import Point, Polygon, box
from shapely.geometry.base import BaseGeometry
from shapely.ops import unary_union

from .models import Point2D


@dataclass(frozen=True)
class Scenario:
    """A field boundary and zero or more forbidden obstacle polygons."""

    name: str
    field: Polygon
    obstacles: tuple[Polygon, ...]
    description: str

    @property
    def free_space(self) -> BaseGeometry:
        if not self.obstacles:
            return self.field
        return self.field.difference(unary_union(self.obstacles))


def _circle(center: Point2D, radius: float, chord_error: float = 0.05) -> Polygon:
    ratio = max(-1.0, min(1.0, 1.0 - chord_error / radius))
    quadrant_segments = max(4, math.ceil(math.pi / (4.0 * math.acos(ratio))))
    return Point(center).buffer(radius, quad_segs=quadrant_segments)


def _seeded_obstacles() -> tuple[Polygon, ...]:
    rng = random.Random(240814)
    obstacles: list[Polygon] = []
    attempts = 0
    while len(obstacles) < 15 and attempts < 500:
        attempts += 1
        radius = rng.uniform(14.0, 30.0)
        center = (rng.uniform(55.0, 945.0), rng.uniform(55.0, 745.0))
        candidate = _circle(center, radius, chord_error=0.75)
        if all(candidate.distance(existing) >= 18.0 for existing in obstacles):
            obstacles.append(candidate)
    if len(obstacles) != 15:
        raise RuntimeError("Could not construct the deterministic stress scenario")
    return tuple(obstacles)


def _build_scenarios() -> dict[str, Scenario]:
    return {
        "empty_rectangle": Scenario(
            name="empty_rectangle",
            field=box(0.0, 0.0, 300.0, 200.0),
            obstacles=(),
            description="300 x 200 m rectangular field without obstacles.",
        ),
        "large_circle": Scenario(
            name="large_circle",
            field=box(0.0, 0.0, 300.0, 200.0),
            obstacles=(_circle((150.0, 100.0), 75.0),),
            description="300 x 200 m field with a 75 m radius central obstacle.",
        ),
        "large_polygon": Scenario(
            name="large_polygon",
            field=box(0.0, 0.0, 400.0, 250.0),
            obstacles=(
                Polygon(
                    [
                        (115.0, 48.0),
                        (285.0, 62.0),
                        (315.0, 165.0),
                        (245.0, 218.0),
                        (105.0, 190.0),
                        (82.0, 105.0),
                    ]
                ),
            ),
            description="400 x 250 m field with a large irregular polygon obstacle.",
        ),
        "edge_obstacle": Scenario(
            name="edge_obstacle",
            field=box(0.0, 0.0, 300.0, 200.0),
            obstacles=(Polygon([(0.0, 55.0), (92.0, 70.0), (82.0, 150.0), (0.0, 165.0)]),),
            description="Obstacle touches the western field boundary.",
        ),
        "l_field": Scenario(
            name="l_field",
            field=Polygon([(0.0, 0.0), (320.0, 0.0), (320.0, 95.0), (135.0, 95.0), (135.0, 240.0), (0.0, 240.0)]),
            obstacles=(_circle((70.0, 155.0), 28.0, chord_error=0.1),),
            description="Concave L-shaped field with one obstacle.",
        ),
        "two_obstacles": Scenario(
            name="two_obstacles",
            field=box(0.0, 0.0, 420.0, 260.0),
            obstacles=(
                _circle((135.0, 112.0), 48.0, chord_error=0.1),
                Polygon([(245.0, 62.0), (345.0, 78.0), (330.0, 195.0), (235.0, 178.0)]),
            ),
            description="Two separated obstacles with competing traversal choices.",
        ),
        "narrow_corridor": Scenario(
            name="narrow_corridor",
            field=box(0.0, 0.0, 360.0, 220.0),
            obstacles=(
                box(115.0, 25.0, 235.0, 101.0),
                box(115.0, 119.0, 235.0, 195.0),
            ),
            description="Two obstacles leave an 18 m navigable central corridor.",
        ),
        "seeded_stress": Scenario(
            name="seeded_stress",
            field=box(0.0, 0.0, 1000.0, 800.0),
            obstacles=_seeded_obstacles(),
            description="1000 x 800 m stress field with 15 deterministic obstacles.",
        ),
    }


_SCENARIOS = _build_scenarios()


def scenario_names() -> tuple[str, ...]:
    return tuple(_SCENARIOS)


def get_scenario(name: str) -> Scenario:
    try:
        return _SCENARIOS[name]
    except KeyError as error:
        choices = ", ".join(scenario_names())
        raise ValueError(f"Unknown scenario {name!r}; choose one of: {choices}") from error
