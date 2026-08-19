"""Metrics, CSV, JSON, and visual artifacts for coverage-lab runs."""

from __future__ import annotations

import csv
import json
from dataclasses import asdict
from typing import TYPE_CHECKING, Any

from .geometry import iter_polygons

if TYPE_CHECKING:
    from pathlib import Path

    from matplotlib.axes import Axes

    from .planner import ScenarioResult


def _route_record(result: ScenarioResult, route_name: str) -> dict[str, Any]:
    route = result.baseline if route_name == "baseline" else result.optimized
    record: dict[str, Any] = {
        "scenario": result.scenario.name,
        "route": route_name,
        "method": route.method,
        "angle_degrees": route.angle_degrees,
        "field_vertex_index": result.selection.vertex_index,
        "cell_count": len(result.decomposition.cells),
        "active_cell_count": len(route.cell_order)
        if route.cell_order
        else int(bool(result.decomposition.legs)),
        "visibility_node_count": result.visibility_node_count,
        "used_fallback": route.used_fallback,
    }
    record.update(asdict(route.metrics))
    return record


def result_records(result: ScenarioResult) -> list[dict[str, Any]]:
    return [_route_record(result, "baseline"), _route_record(result, "optimized")]


def _percentage_reduction(baseline: float, optimized: float) -> float:
    if baseline <= 0.0:
        return 0.0
    return 100.0 * (baseline - optimized) / baseline


def result_document(result: ScenarioResult) -> dict[str, Any]:
    return {
        "scenario": {
            "name": result.scenario.name,
            "description": result.scenario.description,
            "field_area_m2": result.scenario.field.area,
            "free_area_m2": result.scenario.free_space.area,
            "navigation_area_m2": result.decomposition.free_space.area,
            "obstacle_count": len(result.scenario.obstacles),
        },
        "field_edge_selection": {
            "vertex_index": result.selection.vertex_index,
            "point": result.selection.point,
            "edge_end": result.selection.edge_end,
            "angle_degrees": result.selection.angle_degrees,
        },
        "decomposition": {
            "angle_degrees": result.decomposition.angle_degrees,
            "cell_count": len(result.decomposition.cells),
            "spray_leg_count": len(result.decomposition.legs),
            "visibility_node_count": result.visibility_node_count,
            "cells": [
                {
                    "cell_id": cell.cell_id,
                    "area_m2": cell.geometry.area,
                    "leg_count": len(cell.legs),
                    "bounds": cell.geometry.bounds,
                }
                for cell in result.decomposition.cells
            ],
        },
        "baseline": _route_record(result, "baseline"),
        "optimized": {
            **_route_record(result, "optimized"),
            "cell_order": result.optimized.cell_order,
            "variant_order": result.optimized.variant_order,
        },
        "improvement": {
            "estimated_time_percent": _percentage_reduction(
                result.baseline.metrics.estimated_time_s,
                result.optimized.metrics.estimated_time_s,
            ),
            "transit_distance_percent": _percentage_reduction(
                result.baseline.metrics.transit_distance_m,
                result.optimized.metrics.transit_distance_m,
            ),
            "connector_count_percent": _percentage_reduction(
                float(result.baseline.metrics.transit_connector_count),
                float(result.optimized.metrics.transit_connector_count),
            ),
        },
    }


def _draw_polygon(ax: Axes, geometry: Any, *, facecolor: Any, edgecolor: str, alpha: float) -> None:
    for polygon in iter_polygons(geometry):
        x_coordinates, y_coordinates = polygon.exterior.xy
        ax.fill(x_coordinates, y_coordinates, facecolor=facecolor, edgecolor=edgecolor, alpha=alpha)
        for interior in polygon.interiors:
            hole_x, hole_y = interior.xy
            ax.fill(hole_x, hole_y, facecolor="white", edgecolor=edgecolor, alpha=1.0)


def _draw_context(ax: Axes, result: ScenarioResult) -> None:
    _draw_polygon(ax, result.scenario.field, facecolor="#e9f4df", edgecolor="#324a2f", alpha=0.8)
    for obstacle in result.scenario.obstacles:
        _draw_polygon(ax, obstacle, facecolor="#d95f59", edgecolor="#7f1d1d", alpha=0.55)
    vertices = tuple(
        (float(x_coordinate), float(y_coordinate))
        for x_coordinate, y_coordinate in result.scenario.field.exterior.coords
    )[:-1]
    ax.scatter(
        [point[0] for point in vertices],
        [point[1] for point in vertices],
        color="#475569",
        edgecolor="white",
        linewidth=0.6,
        s=24,
        zorder=5,
    )
    for vertex_index, point in enumerate(vertices):
        ax.annotate(
            str(vertex_index),
            point,
            xytext=(4, 4),
            textcoords="offset points",
            fontsize=7,
            color="#334155",
        )
    selection = result.selection
    ax.plot(
        [selection.point[0], selection.edge_end[0]],
        [selection.point[1], selection.edge_end[1]],
        color="#2563eb",
        linewidth=3.0,
        alpha=0.9,
        label="selected direction edge",
        zorder=6,
    )
    ax.scatter(
        [selection.point[0]],
        [selection.point[1]],
        color="#facc15",
        edgecolor="#1e3a8a",
        linewidth=1.2,
        s=75,
        zorder=7,
    )
    ax.set_aspect("equal", adjustable="box")
    ax.grid(alpha=0.15)
    ax.set_xlabel("East [m]")
    ax.set_ylabel("North [m]")


def _draw_route(ax: Axes, result: ScenarioResult, optimized: bool) -> None:
    _draw_context(ax, result)
    route = result.optimized if optimized else result.baseline
    spray_labeled = False
    transit_labeled = False
    for segment in route.segments:
        color = "#087f5b" if segment.spray else "#e67700"
        linewidth = 0.85 if segment.spray else 1.35
        label = None
        if segment.spray and not spray_labeled:
            label = "spray"
            spray_labeled = True
        if not segment.spray and not transit_labeled:
            label = "transit"
            transit_labeled = True
        ax.plot(
            [segment.start[0], segment.end[0]],
            [segment.start[1], segment.end[1]],
            color=color,
            linewidth=linewidth,
            alpha=0.9,
            label=label,
        )
    if spray_labeled or transit_labeled:
        ax.legend(loc="best", fontsize=8)
    route_label = "Optimized" if optimized else "Legacy baseline"
    ax.set_title(
        f"{route_label}: {route.metrics.estimated_time_s:.1f} s, "
        f"transit {route.metrics.transit_distance_m:.1f} m"
    )


def _write_plot(result: ScenarioResult, destination: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    from matplotlib import pyplot as plt

    figure, axes = plt.subplots(2, 2, figsize=(15, 10), constrained_layout=True)
    context_axis = axes[0, 0]
    _draw_context(context_axis, result)
    context_axis.set_title(result.scenario.description)

    cell_axis = axes[0, 1]
    _draw_context(cell_axis, result)
    color_map = plt.get_cmap("tab20")
    for cell in result.decomposition.cells:
        _draw_polygon(
            cell_axis,
            cell.geometry,
            facecolor=color_map(cell.cell_id % 20),
            edgecolor="#1f2937",
            alpha=0.35,
        )
        representative = cell.geometry.representative_point()
        cell_axis.text(
            representative.x, representative.y, str(cell.cell_id), fontsize=7, ha="center"
        )
    cell_axis.set_title(
        f"BCD cells: {len(result.decomposition.cells)}, vertex {result.selection.vertex_index}, "
        f"angle {result.decomposition.angle_degrees:.1f} degrees"
    )

    _draw_route(axes[1, 0], result, optimized=False)
    _draw_route(axes[1, 1], result, optimized=True)
    figure.suptitle(f"Coverage planner lab - {result.scenario.name}", fontsize=15)
    figure.savefig(destination, dpi=150)
    plt.close(figure)


def _write_csv(records: list[dict[str, Any]], destination: Path) -> None:
    if not records:
        return
    with destination.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)


def write_result(result: ScenarioResult, output_root: Path) -> Path:
    scenario_directory = output_root / result.scenario.name
    scenario_directory.mkdir(parents=True, exist_ok=True)
    metrics_path = scenario_directory / "metrics.json"
    metrics_path.write_text(
        json.dumps(result_document(result), indent=2, sort_keys=True),
        encoding="utf-8",
    )
    _write_csv(result_records(result), scenario_directory / "summary.csv")
    _write_plot(result, scenario_directory / "comparison.png")
    return scenario_directory


def write_aggregate(results: list[ScenarioResult], output_root: Path) -> Path:
    records = [record for result in results for record in result_records(result)]
    output_root.mkdir(parents=True, exist_ok=True)
    destination = output_root / "summary.csv"
    _write_csv(records, destination)
    return destination
