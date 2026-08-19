"""Command-line interface for the standalone coverage planning laboratory."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .models import PlannerConfig
from .planner import CoveragePlanner
from .reporting import result_document, write_aggregate, write_result
from .scenarios import get_scenario, scenario_names


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Compare global scanlines with an obstacle-aware BCD agricultural coverage route.",
    )
    parser.add_argument("--scenario", choices=scenario_names(), default="large_circle")
    parser.add_argument("--all", action="store_true", help="run every deterministic scenario")
    parser.add_argument("--list", action="store_true", help="list scenario names and exit")
    parser.add_argument(
        "--field-vertex",
        type=int,
        default=0,
        help="field vertex whose outgoing boundary edge defines sweep direction",
    )
    parser.add_argument("--spacing", type=float, default=2.5, help="spray swath spacing in metres")
    parser.add_argument("--output-dir", type=Path, default=Path("build/coverage-lab"))
    return parser


def main() -> int:
    arguments = _parser().parse_args()
    if arguments.list:
        print("\n".join(scenario_names()))
        return 0
    if arguments.spacing <= 0.0:
        raise SystemExit("--spacing must be greater than zero")

    config = PlannerConfig(swath_spacing=arguments.spacing)
    planner = CoveragePlanner(config)
    names = scenario_names() if arguments.all else (arguments.scenario,)
    results = []
    for name in names:
        scenario = get_scenario(name)
        result = planner.plan(scenario, field_vertex_index=arguments.field_vertex)
        write_result(result, arguments.output_dir)
        results.append(result)
        document = result_document(result)
        print(
            json.dumps(
                {
                    "scenario": name,
                    "method": result.optimized.method,
                    "valid": result.optimized.metrics.valid,
                    "angle_degrees": result.optimized.angle_degrees,
                    "field_vertex_index": result.selection.vertex_index,
                    "selected_point": result.selection.point,
                    "direction_edge_end": result.selection.edge_end,
                    "cells": len(result.decomposition.cells),
                    "legacy_time_s": round(result.baseline.metrics.estimated_time_s, 3),
                    "optimized_time_s": round(result.optimized.metrics.estimated_time_s, 3),
                    "time_improvement_percent": round(
                        document["improvement"]["estimated_time_percent"], 3
                    ),
                    "output": str(arguments.output_dir / name),
                },
                sort_keys=True,
            )
        )
    write_aggregate(results, arguments.output_dir)
    return 0 if all(result.optimized.metrics.valid for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
