"""Obstacle-safe visibility graph routing with a small in-house A* solver."""

from __future__ import annotations

import heapq
import math
from dataclasses import dataclass
from typing import TYPE_CHECKING

from shapely.geometry import LineString

from .geometry import boundary_vertices, distance
from .models import PlannerConfig, Point2D, RouteSegment

if TYPE_CHECKING:
    from shapely.geometry.base import BaseGeometry


@dataclass(frozen=True)
class VisibilityPath:
    points: tuple[Point2D, ...]
    length: float


class VisibilityRouter:
    """Caches a reusable boundary-vertex visibility graph for one field."""

    def __init__(self, free_space: BaseGeometry, config: PlannerConfig) -> None:
        self._visibility_space = free_space.buffer(config.geometry_tolerance)
        self._config = config
        self._nodes = boundary_vertices(free_space)
        self._adjacency: list[list[tuple[int, float]]] = [[] for _ in self._nodes]
        self._visibility_cache: dict[tuple[Point2D, Point2D], bool] = {}
        self._path_cache: dict[tuple[float, ...], VisibilityPath | None] = {}
        self._build_base_graph()

    @property
    def node_count(self) -> int:
        return len(self._nodes)

    def _is_visible(self, start: Point2D, end: Point2D) -> bool:
        if distance(start, end) <= self._config.geometry_tolerance:
            return True
        key = (start, end) if start <= end else (end, start)
        if key in self._visibility_cache:
            return self._visibility_cache[key]
        segment = LineString([start, end])
        visible = self._visibility_space.covers(segment)
        self._visibility_cache[key] = visible
        return visible

    def _build_base_graph(self) -> None:
        for first_index, first in enumerate(self._nodes):
            for second_index in range(first_index + 1, len(self._nodes)):
                second = self._nodes[second_index]
                if self._is_visible(first, second):
                    edge_length = distance(first, second)
                    self._adjacency[first_index].append((second_index, edge_length))
                    self._adjacency[second_index].append((first_index, edge_length))

    def _advances(
        self, start: Point2D, candidate: Point2D, initial_direction: Point2D | None
    ) -> bool:
        if initial_direction is None:
            return True
        delta_x = candidate[0] - start[0]
        delta_y = candidate[1] - start[1]
        dot_product = delta_x * initial_direction[0] + delta_y * initial_direction[1]
        return dot_product >= -self._config.geometry_tolerance

    def shortest_path(
        self,
        start: Point2D,
        end: Point2D,
        initial_direction: Point2D | None = None,
    ) -> VisibilityPath | None:
        if distance(start, end) <= self._config.geometry_tolerance:
            return VisibilityPath(points=(start,), length=0.0)
        direction_key = initial_direction or (0.0, 0.0)
        cache_key = (
            round(start[0], 7),
            round(start[1], 7),
            round(end[0], 7),
            round(end[1], 7),
            round(direction_key[0], 5),
            round(direction_key[1], 5),
        )
        if cache_key in self._path_cache:
            return self._path_cache[cache_key]

        base_count = len(self._nodes)
        start_index = base_count
        end_index = base_count + 1
        dynamic_adjacency: dict[int, list[tuple[int, float]]] = {
            start_index: [],
            end_index: [],
        }
        base_dynamic_edges: dict[int, list[tuple[int, float]]] = {}

        for node_index, node in enumerate(self._nodes):
            if self._is_visible(start, node) and self._advances(start, node, initial_direction):
                edge_length = distance(start, node)
                dynamic_adjacency[start_index].append((node_index, edge_length))
                base_dynamic_edges.setdefault(node_index, []).append((start_index, edge_length))
            if self._is_visible(end, node):
                edge_length = distance(end, node)
                dynamic_adjacency[end_index].append((node_index, edge_length))
                base_dynamic_edges.setdefault(node_index, []).append((end_index, edge_length))

        if self._is_visible(start, end) and self._advances(start, end, initial_direction):
            direct_length = distance(start, end)
            dynamic_adjacency[start_index].append((end_index, direct_length))
            dynamic_adjacency[end_index].append((start_index, direct_length))

        def point_for(index: int) -> Point2D:
            if index == start_index:
                return start
            if index == end_index:
                return end
            return self._nodes[index]

        def neighbors(index: int) -> list[tuple[int, float]]:
            if index in dynamic_adjacency:
                return dynamic_adjacency[index]
            return [*self._adjacency[index], *base_dynamic_edges.get(index, [])]

        frontier: list[tuple[float, float, int]] = [(distance(start, end), 0.0, start_index)]
        costs = {start_index: 0.0}
        parents: dict[int, int] = {}
        while frontier:
            _, current_cost, current = heapq.heappop(frontier)
            if current_cost > costs.get(current, math.inf) + self._config.geometry_tolerance:
                continue
            if current == end_index:
                break
            for neighbor, edge_length in neighbors(current):
                candidate_cost = current_cost + edge_length
                if candidate_cost + self._config.geometry_tolerance >= costs.get(
                    neighbor, math.inf
                ):
                    continue
                costs[neighbor] = candidate_cost
                parents[neighbor] = current
                priority = candidate_cost + distance(point_for(neighbor), end)
                heapq.heappush(frontier, (priority, candidate_cost, neighbor))

        if end_index not in costs:
            if initial_direction is not None:
                fallback = self.shortest_path(start, end, initial_direction=None)
                self._path_cache[cache_key] = fallback
                return fallback
            self._path_cache[cache_key] = None
            return None

        indices = [end_index]
        while indices[-1] != start_index:
            indices.append(parents[indices[-1]])
        indices.reverse()
        path = VisibilityPath(
            points=tuple(point_for(index) for index in indices),
            length=costs[end_index],
        )
        if initial_direction is not None:
            unconstrained = self.shortest_path(start, end, initial_direction=None)
            if unconstrained is not None:
                allowed_forward_penalty = max(
                    2.0 * self._config.swath_spacing,
                    0.1 * unconstrained.length,
                )
                if path.length > unconstrained.length + allowed_forward_penalty:
                    path = unconstrained
        self._path_cache[cache_key] = path
        return path

    def route_segments(
        self,
        start: Point2D,
        end: Point2D,
        *,
        kind: str,
        initial_direction: Point2D | None = None,
    ) -> tuple[RouteSegment, ...] | None:
        path = self.shortest_path(start, end, initial_direction)
        if path is None:
            return None
        return tuple(
            RouteSegment(start=first, end=second, spray=False, kind=kind)
            for first, second in zip(path.points, path.points[1:], strict=False)
            if distance(first, second) > self._config.geometry_tolerance
        )
