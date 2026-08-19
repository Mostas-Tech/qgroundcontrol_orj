# Agricultural Coverage Planner Lab

This is a standalone Python laboratory for testing obstacle-aware agricultural coverage routes.
It does not integrate with QGroundControl or modify mission-plan C++/QML code.

Shapely is used only as a robust geometry kernel and Matplotlib is used for reports. The planner
implements scanline generation, Boustrophedon cellular decomposition (BCD), visibility-graph
routing, A* search, route costing, exact bitmask optimization, and the bounded heuristic locally.

## Commands

Run every command from the repository root through the canonical `just` workflow:

The standard repository `.venv` must already exist (the normal QGC development bootstrap creates
it). `coverage-lab-setup` then installs only the pinned lab and test packages into that environment.

```powershell
just coverage-lab-setup
just coverage-lab large_circle
just coverage-lab large_circle 2 8 build/coverage-lab-vertex-2
just coverage-lab-all
just coverage-lab-test
```

The second `coverage-lab` argument is the selected exterior field-vertex index. It simulates the
user tapping a polygon point. The outgoing edge from that point to the next polygon point defines
the sweep angle, and the optimized route is constrained to begin on the spray line nearest the
selected point. Vertex `0`, the first polygon point, is used by default. There is no automatic
angle search.

## Planning model

- The route is open: it starts at the selected field vertex, transits to the nearest spray line,
  and ends at the last spray leg. There is no depot return.
- The selected vertex's outgoing polygon edge fixes the direction of every parallel spray line.
- Obstacles are expanded by 5 cm before planning so numerical boundary paths remain outside the
  nominal forbidden polygons.
- Spray and transit speeds default to 7 m/s and 10 m/s. The time estimate includes a 2.5 m/s^2
  triangular/trapezoidal acceleration model plus yaw and settle penalties.
- Up to 12 active cells use exact bitmask dynamic programming. Larger cases use deterministic
  cheapest insertion with bounded orientation, 2-opt, and Or-opt improvement.
- If the optimized route is invalid or slower under the same model, the result is explicitly
  marked as a legacy fallback.

The deterministic scenario set covers an empty rectangle, a large circular obstacle, a large
polygon obstacle, an edge-touching obstacle, a concave L-shaped field, two obstacles, a narrow
corridor, and a seeded 1000 x 800 m stress field with 15 obstacles.

## Outputs

Each run writes to `build/coverage-lab/<scenario>/`:

- `comparison.png`: field, BCD cells, legacy route, and optimized route in a 2 x 2 figure.
- `metrics.json`: geometry, route, solver, validity, coverage, distance, time, and improvement data.
- `summary.csv`: baseline and optimized rows for the scenario.

An aggregate `build/coverage-lab/summary.csv` is written by the all-scenario command.
