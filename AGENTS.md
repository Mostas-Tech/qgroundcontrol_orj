# AGENTS.md

Instructions for AI coding agents (Codex, Claude Code, etc.) working on QGroundControl.

## Quick References

- [CODING_STYLE.md](CODING_STYLE.md) — Naming, formatting, C++20 features, QML style, logging
- [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md) — Architecture patterns (Fact System, Multi-Vehicle, FirmwarePlugin)
- [tools/README.md](tools/README.md) — Development scripts and tooling
- [test/README.md](test/README.md) — Test framework, base classes, CTest labels, MultiSignalSpy, coverage
- [.github/ci-overview.md](.github/ci-overview.md) — CI workflow/action/script layout and conventions
- [.pre-commit-config.yaml](.pre-commit-config.yaml) — All enforced linters (clang-format, clang-tidy, ruff, pyright, shellcheck, actionlint, zizmor, qmllint, clazy, vehicle-null-check, check-no-qassert, check-no-qtest-ignore-message)

## Custom Build Policy — stay mergeable with upstream (company rule #1)

This is a **company build** of QGroundControl. We must keep receiving upstream QGC features and
fixes, so upstream merges have to stay near-conflict-free. QGC ships a first-class mechanism for
exactly this — the [custom build overlay](https://docs.qgroundcontrol.com/master/en/qgc-dev-guide/custom_build/custom_build.html):
a `custom/` directory at the repo root is auto-detected by CMake (`QGC_CUSTOM_DIR` in
`cmake/CustomOptions.cmake`; detection + `add_subdirectory` in the root `CMakeLists.txt`), which
loads `custom/cmake/CustomOverrides.cmake` and builds the overlay. `custom-example/` is upstream's
reference template — copy patterns from it; never modify it.

**Default rule: company-specific changes live in `custom/`, not in upstream `src/`.**

Where a change goes:

1. **Branding** (app name, icons, package ids, installer art) →
   `custom/cmake/CustomOverrides.cmake` + `custom/res/`, `custom/deploy/`, `custom/android/`.
2. **Behavior & UI** → subclass overrides in `custom/src/`: `CustomPlugin : QGCCorePlugin`
   (plus `QGCOptions` / `QGCFlyViewOptions`) for settings visibility, toolbar, indicators,
   fly-view overlay; custom QML modules registered from the plugin
   (`src/API/QGCCorePlugin.h` and `src/API/QGCOptions.h` define the override surface — read them
   before concluding something "can't" be done in the overlay).
3. **Replacing stock QML/images** → resource overrides via `custom/custom.qrc` + the QML URL
   interceptor (see `CustomOverrideInterceptor` in `custom-example/src/CustomPlugin.cc`) — never
   edit the upstream file to restyle it.
4. **Firmware scope/behavior** → custom `FirmwarePluginFactory` / `FirmwarePlugin` subclass in
   `custom/src/`; restrict flight stacks via `QGC_DISABLE_APM_PLUGIN_FACTORY` /
   `QGC_DISABLE_PX4_PLUGIN_FACTORY` in `CustomOverrides.cmake`.
5. **Genuine bug fixes / improvements that upstream QGC would also want** → these MAY edit `src/`
   directly. Keep them minimal, upstream-style, and self-contained — they are candidates for
   upstream PRs.
6. **Missing hook?** When the overlay can't express a change because no extension point exists,
   add the *smallest possible* extension point to `src/` (a virtual method, `Q_PROPERTY`, or
   `QGCOptions` flag) in its own commit, then implement the company behavior in `custom/` on top
   of it.

Hard rules (code-reviewer treats violations as must-fix):

- Never hard-code company names, branding, or business logic in `src/`; never scatter
  company-specific `#ifdef`s/conditionals through upstream code.
- Never mix `custom/` and `src/` edits in the same commit — company-facing and upstream-facing
  changes must stay separable for upstream syncs (use scopes, e.g. `feat(custom): ...` vs
  `fix(Vehicle): ...`).
- Before editing any file under `src/`, ask: *could upstream ship a conflicting change here, and
  is this edit company-specific?* If both, it belongs in `custom/` via the mechanisms above.
- Standard QGC runs in Advanced Mode always; custom builds start in regular mode — hide
  vendor-preconfigured setup UI via `QGCOptions`, don't delete it.

## Custom Agents (use these for task work)

Task-specific agent definitions live in [.github/agents/](.github/agents/README.md) — the
**canonical source**, used directly by VS Code Copilot (Claude and OpenAI models alike). Claude
Code invokes the same agents through thin wrappers in `.claude/agents/`, which only point back to
the canonical files.

| Agent | Use for |
| ----- | ------- |
| `cpp-core` | C++/Qt feature work and bug fixes |
| `qml-ui` | Any user-facing QML change |
| `test-engineer` | Writing/extending tests, fixing flaky tests |
| `code-reviewer` | Pre-merge review of every branch (read-only) |
| `build-ci` | Build breakage, lint/pre-commit failures, GitHub Actions failures |

Rules for the agent files themselves:

- Every agent operates **under this AGENTS.md** — agent files add task-specific guidance on top of
  these rules, never instead of them.
- To change an agent, edit `.github/agents/<name>.agent.md`. Never put rules only in the
  `.claude/agents/` wrapper — it must stay a pointer.
- Adding/renaming an agent requires updating both directories plus the table above and the one in
  [.github/agents/README.md](.github/agents/README.md).

## Golden Rules (enforced — violations fail CI)

These are the non-negotiables. The first four are QGC's core architecture patterns; the rest are
enforced by pre-commit hooks, so ignoring them wastes a build cycle. Full list with code examples:
[.github/CONTRIBUTING.md#architecture-patterns](.github/CONTRIBUTING.md#architecture-patterns) and
[CODING_STYLE.md#common-pitfalls](CODING_STYLE.md#common-pitfalls).

- **Fact System** — ALL vehicle parameters flow through Facts; never create custom parameter storage.
- **Multi-Vehicle** — ALWAYS null-check `activeVehicle()` / `Vehicle*` before dereferencing (`vehicle-null-check`).
- **Firmware Plugin** — use `vehicle->firmwarePlugin()` for firmware-specific behavior, not `if (px4)` branches.
- **QML Integration** — register types with `QML_ELEMENT`/`QML_SINGLETON`/`QML_UNCREATABLE`; expose state via `Q_PROPERTY`.
- **No `Q_ASSERT` in production code** — use defensive checks with early returns (`check-no-qassert`).
- **No `QTest::ignoreMessage`** in tests — use `expectLogMessage`/`ignoreLogMessage` (`check-no-qtest-ignore-message`).
- **No fixed-delay `QTest::qWait(<n>)`** — use `QTRY_*_WITH_TIMEOUT` or `QSignalSpy::wait` (`check-no-fixed-qwait`).

## Critical Files (Read First!)

1. `src/FactSystem/Fact.h` — Parameter system foundation
2. `src/Vehicle/Vehicle.h` — Core vehicle model
3. `src/FirmwarePlugin/FirmwarePlugin.h` — Firmware abstraction

## Code Structure

Key modules (full tree under `src/` — ~33 subdirectories):

```text
src/
├── Vehicle/          # Vehicle state/comms
├── Comms/            # Link layer (serial, UDP, TCP, Bluetooth)
├── FactSystem/       # Parameter management
├── FirmwarePlugin/   # PX4/ArduPilot abstraction
├── AutoPilotPlugins/ # Vehicle setup UI
├── MissionManager/   # Mission planning
├── MAVLink/          # Protocol handling
├── VideoManager/     # Video pipeline (GStreamer)
├── FlyView/          # In-flight UI
├── PlanView/         # Mission planning UI
├── QmlControls/      # Reusable QML components
└── Settings/         # Persistent settings
```

## Build & Test Commands

The `just` recipes are the canonical workflow — see [tools/README.md](tools/README.md) for the full list.
[.github/ci-overview.md](.github/ci-overview.md) documents how CI invokes builds and tests; match CI, don't guess.

```bash
just configure          # CMake configure (pulls submodules first)
just build              # incremental build; uses all cores (override with JOBS=N)
just test               # ctest, LABELS="Unit|Integration" EXCLUDE="Flaky|Network"
just lint               # fast pre-commit gate (clang-format, ruff, qmllint, ...)
just check              # lint + test (run before declaring done)
just format-fix         # apply clang-format / ruff-format
just info               # print resolved versions (Qt, CMake, GStreamer)
```

- **Build incrementally** — rebuild every few file edits during multi-file C++/Qt work, not just at the end; fix build errors before continuing.
- **Tight test loops** — iterate one test with `ctest -R <name>` (or `--gtest_filter`); only run the full label on the final pass. CI runs `ctest --output-on-failure -L Unit`.
- **Match CI** — before running tests/lint locally, use the same command CI runs ([.github/ci-overview.md](.github/ci-overview.md)), not a local guess.

## Definition of Done

Before considering a change complete:

1. `just build` succeeds.
2. `just lint` (or `pre-commit run --all-files` for the full sweep) passes.
3. Relevant tests pass (`ctest -R <name>` for the touched area; full `-L Unit` on the final pass).
4. Commit message follows Conventional Commits (below).

## Commit & Review Conventions

Commit messages follow **Conventional Commits** — the type drives release automation
(`.releaserc.json` → semantic-release). Use: `feat`, `fix`, `perf`, `revert` (release-triggering);
`docs`, `style`, `chore`, `refactor`, `test`, `build`, `ci` (no release). Example: `fix(Vehicle): guard null activeVehicle in telemetry handler`.

Your output will be reviewed by another AI agent before being accepted. Keep changes focused and
minimal, use clear naming, and leave explanatory commit messages. Avoid unrelated changes,
commented-out code, or ambiguous TODOs.

---

**Key Principle**: Match the style of code you're editing. See [CODING_STYLE.md](CODING_STYLE.md) for conventions and [CODING_STYLE.md#examples](CODING_STYLE.md#examples) for canonical Vehicle/Fact/QML snippets.
