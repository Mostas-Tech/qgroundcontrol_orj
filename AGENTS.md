# AGENTS.md

Instructions for AI coding agents (Codex, Claude Code, etc.) working on QGroundControl.

## Quick References

- [CODING_STYLE.md](CODING_STYLE.md) — Naming, formatting, C++20 features, QML style, logging
- [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md) — Architecture patterns (Fact System, Multi-Vehicle, FirmwarePlugin)
- [tools/README.md](tools/README.md) — Development scripts and tooling
- [test/README.md](test/README.md) — Test framework, base classes, CTest labels, MultiSignalSpy, coverage
- [.github/ci-overview.md](.github/ci-overview.md) — CI workflow/action/script layout and conventions
- [.pre-commit-config.yaml](.pre-commit-config.yaml) — All enforced linters (clang-format, clang-tidy, ruff, pyright, shellcheck, actionlint, zizmor, qmllint, clazy, vehicle-null-check, check-no-qassert, check-no-qtest-ignore-message)
- [.github/instructions/learnings.instructions.md](.github/instructions/learnings.instructions.md) — Verified cross-session pitfalls and process corrections
- [.github/skills/windows-dev-env/SKILL.md](.github/skills/windows-dev-env/SKILL.md) — Verified commands and paths for this Windows development machine

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
the canonical files. Codex invokes the specialist roster through `.codex/agents/*.toml` adapters,
which first load `.codex/BASE_INSTRUCTIONS.md` and the matching canonical file. Codex keeps
dispatcher orchestration in its primary session: it adopts `.github/agents/dispatcher.agent.md`
for whole jobs and never spawns a separate dispatcher agent, avoiding nested orchestration and
context replay.

| Agent | Use for |
| ----- | ------- |
| `cpp-core` | C++/Qt feature work and bug fixes |
| `qml-ui` | Any user-facing QML change |
| `test-engineer` | Risk-selected regression tests and fixing failing/flaky tests; not a routine stage |
| `code-reviewer` | Pre-merge review of every branch (read-only) |
| `build-ci` | Build breakage, lint/pre-commit failures, GitHub Actions failures |
| `dispatcher` | Orchestrating a whole job across the other agents (Claude Code: `/dispatch`) |

Rules for the agent files themselves:

- Every agent operates **under this AGENTS.md** — agent files add task-specific guidance on top of
  these rules, never instead of them.
- To change an agent, edit `.github/agents/<name>.agent.md`. Never put rules only in a host
  adapter: `.claude/agents/` wrappers and `.codex/agents/` TOML files must point to the canonical
  rules.
- Adding/renaming a specialist agent requires updating the canonical file, both host adapter
  directories, the table above, and the one in [.github/agents/README.md](.github/agents/README.md).
  `dispatcher` remains a Claude primary-session command and a Codex primary-session role; do not
  add a `.claude/agents/` or `.codex/agents/` dispatcher adapter.

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

The `just` recipes are the canonical workflow where they are available — see
[tools/README.md](tools/README.md) for the full list. On the verified Windows development machine,
read [.github/skills/windows-dev-env/SKILL.md](.github/skills/windows-dev-env/SKILL.md) first and
reuse its configured build tree. [.github/ci-overview.md](.github/ci-overview.md) documents how CI
invokes builds and tests; match CI, don't guess.
For Codex, `.codex/BASE_INSTRUCTIONS.md` is the shared command baseline for the primary session
and every specialist; follow it before classifying a sandbox or environment failure.

```bash
just configure          # CMake configure (pulls submodules first)
just build              # incremental build; uses all cores (override with JOBS=N)
just test               # ctest, LABELS="Unit|Integration" EXCLUDE="Flaky|Network"
just lint               # fast pre-commit gate (clang-format, ruff, qmllint, ...)
just check              # lint + test; use only when the selected validation scope requires both
just format-fix         # apply clang-format / ruff-format
just info               # print resolved versions (Qt, CMake, GStreamer)
```

- **Build coherent batches** — compile once after a coherent implementation batch; rebuild only
  after a failure-driven fix.
- **Tight test loops** — when a test is justified by the policy below, run the narrowest selection
  once with `just test-one <name>` and repeat only after a failure-driven fix.
- **Match CI** — before running tests/lint locally, use the same command CI runs ([.github/ci-overview.md](.github/ci-overview.md)), not a local guess.
- **Reuse evidence** — in dispatched jobs, each owner runs its gate once and forwards the real
  command output; other agents do not rerun a green gate.

## Risk-Based Test Policy (cost default)

**Default: do not create or modify tests, do not dispatch `test-engineer`, and do not run a test
command.** A behavior change by itself is not a reason to add tests. Build/lint/visual verification
may be the complete validation for a low-risk change.

Tests are justified only when at least one of these is true:

1. The user, issue, or acceptance criteria explicitly require tests.
2. A bug fix has a concrete regression scenario and a stable focused test would have caught it.
3. The change affects high-risk logic: flight safety, mission/path generation, arming/vehicle
   commands, failsafe behavior, parameter persistence, link/protocol parsing, security, or data loss.
4. A non-trivial algorithm/state machine changes in a way with distinct edge cases not covered by
   an existing test.
5. An existing test is failing or flaky and the task is to repair it.

Normally skip new tests for visual QML/layout changes, text, branding, resources, documentation,
agent/config/build metadata, mechanical refactors, simple property/signal wiring, logging, defensive
guards, and behavior already covered by an existing test. Line count, a new code path, or a desire
to increase coverage is not enough by itself.

When a trigger applies, add at most one focused regression-test batch, preferably in an existing
test file, and run only `just test-one <name>`. Do not run a label or full suite unless the user or
CI explicitly requests it. When no trigger applies, record a one-line reason such as
`Tests not added/run: low-risk visual-only change`; do not produce a test plan or coverage essay.

## Definition of Done

Before considering a change complete:

1. Where available, the canonical `just build` and `just lint` gates pass. Where they are
   unavailable, use the documented platform fallback (on the verified Windows machine, see
   [.github/skills/windows-dev-env/SKILL.md](.github/skills/windows-dev-env/SKILL.md)).
2. Tests are conditional under the risk-based policy above. When a trigger applies, the smallest
   relevant selection passes via `just test-one <name>`; otherwise no test run is required.
3. Run `just test` only when the user or CI explicitly requests a broader suite.
4. When a commit is requested, its message follows Conventional Commits (below).

## Commit & Review Conventions

Commit messages follow **Conventional Commits** — the type drives release automation
(`.releaserc.json` → semantic-release). Use: `feat`, `fix`, `perf`, `revert` (release-triggering);
`docs`, `style`, `chore`, `refactor`, `test`, `build`, `ci` (no release). Example: `fix(Vehicle): guard null activeVehicle in telemetry handler`.

Your output will be reviewed by another AI agent before being accepted. Keep changes focused and
minimal, use clear naming, and leave explanatory commit messages. Avoid unrelated changes,
commented-out code, or ambiguous TODOs.

---

**Key Principle**: Match the style of code you're editing. See [CODING_STYLE.md](CODING_STYLE.md) for conventions and [CODING_STYLE.md#examples](CODING_STYLE.md#examples) for canonical Vehicle/Fact/QML snippets.
