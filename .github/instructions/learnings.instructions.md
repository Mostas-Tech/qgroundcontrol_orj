---
description: Cross-session learnings — repository-specific pitfalls and corrections discovered during agent sessions. Loaded globally so the same mistake is never paid for twice.
---

# Session Learnings

Each learning is 1-4 sentences: the pitfall, the correct approach. Add new learnings at the end
of the matching section (create a section if needed). Keep entries actionable and verified.

## Build & environment

- Before running any build/test/lint command, read `.github/skills/windows-dev-env/SKILL.md` for
  the exact working commands on this machine. `just` 1.58 is installed, the repo-local `.env`
  selects `build-spray-msvc`, and `run_with_msvc.py` supplies Visual Studio, Qt, and GStreamer
  runtime paths. Do not bypass this path with repeated hand-built shell commands.
- `build-spray-msvc/compile_commands.json` is enabled. If Ninja reports a premature dependency-file
  EOF, run `ninja -C build-spray-msvc -t recompact` once, then verify two consecutive `just build`
  calls; the second must say `ninja: no work to do`.
- Debug executables exit immediately with `0xC0000135` (missing DLL) unless both the Qt `bin`
  directory and the GStreamer `sdk\bin` directory are on `PATH`, `GST_PLUGIN_PATH` is set, and the
  Visual Studio Debug environment is active. `just run` now supplies all three automatically.
- On this Windows tree, CMake defines the `WIN32` target `QGroundControl`, producing
  `build-spray-msvc\Debug\QGroundControl.exe`. `just run` must default to that GUI executable
  (while allowing `QGC_EXECUTABLE` to override it), not the obsolete
  `QGroundControl-console.exe`, which can be stale.
- Standalone QtTest harnesses compile fine with MSVC via `qt-cmake` + Ninja; clang-repl/JIT
  approaches fail against Qt/MSVC symbols (`Symbols not found: __std_max_element_d`) — don't
  retry them.

## MissionManager / C++ API

- User-visible errors in `MissionController` use `qgcApp()->showAppMessage(...)`;
  `QGC::showAppMessage` does not exist.
- Failed `MissionController` JSON/text loads must synchronously delete the temporary
  `loadedVisualItems` model and its items. `deleteLater` is not enough: rejected items stay
  parented to `PlanMasterController` with live GeoFence signal connections until the event loop
  runs, and repeated failed loads accumulate hidden items.
- Making a complex item truly "no terrain" requires BOTH `FlightPathSegment(...,
  queryTerrainData=false, ...)` AND overriding the `VisualMissionItem`
  `coordinateTerrainAltitudeQueryEnabled()` predicate; either path can initiate terrain work.
- `SimpleMissionItem::setCoordinate` emits `coordinateChanged` twice (param5 then param6 update).
  Signal-count assertions must expect 2, not 1.
- Per-type complex-item cardinality is enforced through the
  `QGCCorePlugin::canCreateComplexMissionItem` policy hook at three points: menu availability,
  interactive insertion, and JSON loading into the temporary model. Keep all three paths covered
  when changing it.

## Process

- A 2026-08 retrospective counted 96 `task` dispatch calls once nested specialist activity was
  included. Only the dispatcher routes work; specialists reuse their existing context and never
  spawn nested agents or reviewers.
- When an agent claims "done" but its own targeted test report shows failures, reopen the task —
  a "done" test task had latest red XML reports in the build tree. A task is done when current gate
  command output is green, not when the agent says so.
- Requirements written in the plan (e.g. "no terrain queries") must appear verbatim as a checklist
  in implementation and review prompts. Reviews report all high-confidence misses in one pass.
