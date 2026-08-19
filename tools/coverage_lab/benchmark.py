"""Calculation-only benchmark for the agricultural coverage planner."""

from __future__ import annotations

import argparse
import gc
import statistics
import time
from dataclasses import dataclass

from .models import PlannerConfig
from .planner import CoveragePlanner
from .scenarios import get_scenario, scenario_names


@dataclass(frozen=True)
class BenchmarkResult:
    scenario: str
    cell_count: int
    method: str
    angle_degrees: float
    samples_seconds: tuple[float, ...]

    @property
    def minimum_seconds(self) -> float:
        return min(self.samples_seconds)

    @property
    def median_seconds(self) -> float:
        return statistics.median(self.samples_seconds)

    @property
    def mean_seconds(self) -> float:
        return statistics.fmean(self.samples_seconds)

    @property
    def maximum_seconds(self) -> float:
        return max(self.samples_seconds)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Measure planner calculation time without report generation."
    )
    parser.add_argument("--scenario", choices=scenario_names(), default="large_circle")
    parser.add_argument("--all", action="store_true", help="benchmark every deterministic scenario")
    parser.add_argument("--spacing", type=float, default=8.0)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--field-vertex", type=int, default=0)
    return parser


def _measure_scenario(
    scenario_name: str,
    config: PlannerConfig,
    *,
    repeats: int,
    warmups: int,
    field_vertex_index: int,
) -> BenchmarkResult:
    scenario = get_scenario(scenario_name)
    final_result = None
    for _ in range(warmups):
        final_result = CoveragePlanner(config).plan(
            scenario,
            field_vertex_index=field_vertex_index,
        )
        if not final_result.optimized.metrics.valid:
            raise RuntimeError(f"Warmup produced an invalid route for {scenario_name}")

    samples: list[float] = []
    for _ in range(repeats):
        gc.collect()
        start_time = time.perf_counter()
        final_result = CoveragePlanner(config).plan(
            scenario,
            field_vertex_index=field_vertex_index,
        )
        samples.append(time.perf_counter() - start_time)
        if not final_result.optimized.metrics.valid:
            raise RuntimeError(f"Benchmark produced an invalid route for {scenario_name}")

    if final_result is None:
        raise RuntimeError("Benchmark requires at least one warmup or measured repeat")
    return BenchmarkResult(
        scenario=scenario_name,
        cell_count=len(final_result.decomposition.cells),
        method=final_result.optimized.method,
        angle_degrees=final_result.selection.angle_degrees,
        samples_seconds=tuple(samples),
    )


def main() -> int:
    arguments = _parser().parse_args()
    if arguments.spacing <= 0.0:
        raise SystemExit("--spacing must be greater than zero")
    if arguments.repeats <= 0:
        raise SystemExit("--repeats must be greater than zero")
    if arguments.warmups < 0:
        raise SystemExit("--warmups cannot be negative")

    config = PlannerConfig(swath_spacing=arguments.spacing)
    names = scenario_names() if arguments.all else (arguments.scenario,)
    results = [
        _measure_scenario(
            name,
            config,
            repeats=arguments.repeats,
            warmups=arguments.warmups,
            field_vertex_index=arguments.field_vertex,
        )
        for name in names
    ]

    print(
        "scenario                  vertex  angle  cells  solver                       min_ms   median_ms    mean_ms     max_ms"
    )
    for result in results:
        print(
            f"{result.scenario:<25} {arguments.field_vertex:>6} "
            f"{result.angle_degrees:>6.1f} {result.cell_count:>5}  {result.method:<27} "
            f"{result.minimum_seconds * 1000.0:>8.2f} "
            f"{result.median_seconds * 1000.0:>11.2f} "
            f"{result.mean_seconds * 1000.0:>10.2f} "
            f"{result.maximum_seconds * 1000.0:>10.2f}"
        )
    total_median = sum(result.median_seconds for result in results)
    print(f"calculation_only_median_total_ms={total_median * 1000.0:.2f}")
    print(f"spacing_m={arguments.spacing:.3f}")
    print(f"repeats={arguments.repeats}")
    print(f"warmups={arguments.warmups}")
    print(f"field_vertex_index={arguments.field_vertex}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
