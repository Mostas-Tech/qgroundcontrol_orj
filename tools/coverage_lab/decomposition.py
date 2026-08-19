"""Scanline swaths and Boustrophedon cellular decomposition."""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass
from typing import TYPE_CHECKING

from shapely.geometry import GeometryCollection, LineString, Point, box
from shapely.ops import unary_union

from .geometry import iter_lines, iter_polygons, rotate_geometry, rotate_point
from .models import CoverageCell, PlannerConfig, SprayLeg

if TYPE_CHECKING:
    from shapely.geometry.base import BaseGeometry


@dataclass(frozen=True)
class Decomposition:
    """A free-space partition and its globally consistent spray legs."""

    angle_degrees: float
    cells: tuple[CoverageCell, ...]
    legs: tuple[SprayLeg, ...]
    free_space: BaseGeometry


@dataclass(frozen=True)
class _SlabPiece:
    node_id: int
    geometry: BaseGeometry


class _DisjointSet:
    def __init__(self, size: int) -> None:
        self._parent = list(range(size))

    def find(self, item: int) -> int:
        parent = self._parent[item]
        if parent != item:
            self._parent[item] = self.find(parent)
        return self._parent[item]

    def union(self, first: int, second: int) -> None:
        first_root = self.find(first)
        second_root = self.find(second)
        if first_root != second_root:
            self._parent[second_root] = first_root


def _critical_y_coordinates(free_space: BaseGeometry, tolerance: float) -> list[float]:
    coordinates: list[float] = []
    for polygon in iter_polygons(free_space):
        for ring in (polygon.exterior, *polygon.interiors):
            coordinates.extend(float(y_coordinate) for _, y_coordinate in ring.coords)
    coordinates.sort()
    unique: list[float] = []
    for coordinate in coordinates:
        if not unique or coordinate - unique[-1] > tolerance:
            unique.append(coordinate)
    return unique


def _slab_layers(
    free_space: BaseGeometry, critical_y: list[float], tolerance: float
) -> list[list[_SlabPiece]]:
    minimum_x, _, maximum_x, _ = free_space.bounds
    horizontal_padding = max(1.0, maximum_x - minimum_x)
    layers: list[list[_SlabPiece]] = []
    next_node_id = 0
    for minimum_y, maximum_y in itertools.pairwise(critical_y):
        if maximum_y - minimum_y <= tolerance:
            continue
        slab = box(
            minimum_x - horizontal_padding,
            minimum_y,
            maximum_x + horizontal_padding,
            maximum_y,
        )
        components = sorted(
            (
                polygon
                for polygon in iter_polygons(free_space.intersection(slab))
                if polygon.area > tolerance * tolerance
            ),
            key=lambda polygon: (polygon.bounds[0], polygon.bounds[2]),
        )
        layer: list[_SlabPiece] = []
        for component in components:
            layer.append(_SlabPiece(next_node_id, component))
            next_node_id += 1
        if layer:
            layers.append(layer)
    return layers


def _component_chains(layers: list[list[_SlabPiece]], tolerance: float) -> list[list[_SlabPiece]]:
    pieces = [piece for layer in layers for piece in layer]
    disjoint_set = _DisjointSet(len(pieces))
    edges: list[tuple[_SlabPiece, _SlabPiece]] = []
    outgoing_degree: dict[int, int] = {piece.node_id: 0 for piece in pieces}
    incoming_degree: dict[int, int] = {piece.node_id: 0 for piece in pieces}

    for previous_layer, current_layer in itertools.pairwise(layers):
        for previous in previous_layer:
            for current in current_layer:
                shared_boundary = previous.geometry.intersection(current.geometry)
                if (
                    shared_boundary.length > tolerance
                    or shared_boundary.area > tolerance * tolerance
                ):
                    edges.append((previous, current))
                    outgoing_degree[previous.node_id] += 1
                    incoming_degree[current.node_id] += 1

    for previous, current in edges:
        if outgoing_degree[previous.node_id] == 1 and incoming_degree[current.node_id] == 1:
            disjoint_set.union(previous.node_id, current.node_id)

    chains_by_root: dict[int, list[_SlabPiece]] = {}
    for piece in pieces:
        chains_by_root.setdefault(disjoint_set.find(piece.node_id), []).append(piece)
    return list(chains_by_root.values())


def _cell_geometries(free_space: BaseGeometry, config: PlannerConfig) -> list[BaseGeometry]:
    critical_y = _critical_y_coordinates(free_space, config.geometry_tolerance)
    if len(critical_y) < 2:
        return []
    layers = _slab_layers(free_space, critical_y, config.geometry_tolerance)
    chains = _component_chains(layers, config.geometry_tolerance)
    geometries = [unary_union([piece.geometry for piece in chain]) for chain in chains]
    geometries = [geometry for geometry in geometries if geometry.area > config.geometry_tolerance]
    geometries.sort(
        key=lambda geometry: (geometry.bounds[1], geometry.bounds[0], geometry.bounds[3])
    )

    covered = unary_union(geometries) if geometries else GeometryCollection()
    missing_area = free_space.difference(covered).area
    extra_area = covered.difference(free_space).area
    allowed_area_error = max(config.geometry_tolerance, free_space.area * 1e-10)
    if missing_area > allowed_area_error or extra_area > allowed_area_error:
        raise RuntimeError(
            "BCD cell union does not reproduce free space: "
            f"missing={missing_area:.9f}, extra={extra_area:.9f}"
        )
    return geometries


def _generate_local_legs(free_space: BaseGeometry, config: PlannerConfig) -> list[SprayLeg]:
    minimum_x, minimum_y, maximum_x, maximum_y = free_space.bounds
    height = maximum_y - minimum_y
    row_count = max(1, math.ceil(height / config.swath_spacing))
    first_y = minimum_y + (height - (row_count - 1) * config.swath_spacing) / 2.0
    horizontal_padding = max(1.0, maximum_x - minimum_x)
    legs: list[SprayLeg] = []

    for row_index in range(row_count):
        local_y = first_y + row_index * config.swath_spacing
        scanline = LineString(
            [
                (minimum_x - horizontal_padding, local_y),
                (maximum_x + horizontal_padding, local_y),
            ]
        )
        clipped_lines = sorted(
            iter_lines(free_space.intersection(scanline)),
            key=lambda line: min(point[0] for point in line.coords),
        )
        for clipped_line in clipped_lines:
            coordinates = tuple(clipped_line.coords)
            if len(coordinates) < 2:
                continue
            left_x = min(float(point[0]) for point in coordinates)
            right_x = max(float(point[0]) for point in coordinates)
            if right_x - left_x <= config.geometry_tolerance:
                continue
            legs.append(
                SprayLeg(
                    row_index=row_index,
                    local_y=local_y,
                    local_x_min=left_x,
                    local_x_max=right_x,
                    left=(left_x, local_y),
                    right=(right_x, local_y),
                )
            )
    return legs


def build_decomposition(
    free_space: BaseGeometry,
    angle_degrees: float,
    config: PlannerConfig,
) -> Decomposition:
    """Build BCD cells and swaths for one sweep angle."""

    local_free_space = rotate_geometry(free_space, -angle_degrees)
    local_cell_geometries = _cell_geometries(local_free_space, config)
    local_legs = _generate_local_legs(local_free_space, config)

    world_cells = [
        CoverageCell(cell_id=cell_id, geometry=rotate_geometry(geometry, angle_degrees))
        for cell_id, geometry in enumerate(local_cell_geometries)
    ]
    world_legs: list[SprayLeg] = []
    for leg in local_legs:
        world_leg = SprayLeg(
            row_index=leg.row_index,
            local_y=leg.local_y,
            local_x_min=leg.local_x_min,
            local_x_max=leg.local_x_max,
            left=rotate_point(leg.left, angle_degrees),
            right=rotate_point(leg.right, angle_degrees),
        )
        midpoint = Point(
            (world_leg.left[0] + world_leg.right[0]) / 2.0,
            (world_leg.left[1] + world_leg.right[1]) / 2.0,
        )
        candidate_cells = [cell for cell in world_cells if cell.geometry.covers(midpoint)]
        if not candidate_cells:
            candidate_cells = sorted(
                world_cells, key=lambda cell: cell.geometry.distance(midpoint)
            )[:1]
        if not candidate_cells:
            raise RuntimeError("No BCD cell accepts a generated spray leg")
        candidate_cells[0].legs.append(world_leg)
        world_legs.append(world_leg)

    return Decomposition(
        angle_degrees=angle_degrees,
        cells=tuple(world_cells),
        legs=tuple(world_legs),
        free_space=free_space,
    )
