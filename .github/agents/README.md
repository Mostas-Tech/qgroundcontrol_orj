# Custom Agents

Project-specific AI agents for QGroundControl development. Each `*.agent.md` file defines one
agent with the project rules baked in, so agent output stays consistent regardless of who (or
which tool) invokes it.

## Two locations, one source of truth

| Location | Consumed by | Role |
| -------- | ----------- | ---- |
| `.github/agents/*.agent.md` | VS Code Copilot (Claude and OpenAI models) | **Canonical** — all rules live here |
| `.claude/agents/*.md` | Claude Code | Thin wrappers that point to the canonical file |

Edit only the canonical `.github/agents/` file; the `.claude/agents/` wrapper's first instruction
is to read it, so changes propagate automatically. When adding or renaming an agent, create/rename
the file in **both** directories and update the tables here and in [AGENTS.md](../../AGENTS.md).

Every agent operates under the repo-wide rules in [AGENTS.md](../../AGENTS.md) (imported by
`CLAUDE.md`); agent files add task-specific guidance on top, never instead.

| Agent | Use for |
| ----- | ------- |
| [cpp-core](cpp-core.agent.md) | C++/Qt feature work and bug fixes (Vehicle, Comms, FactSystem, MissionManager, ...) |
| [qml-ui](qml-ui.agent.md) | Any user-facing QML change (FlyView, PlanView, QmlControls, setup pages) |
| [test-engineer](test-engineer.agent.md) | Writing/extending tests, fixing flaky tests |
| [code-reviewer](code-reviewer.agent.md) | Pre-merge review of every branch — read-only, reports findings |
| [build-ci](build-ci.agent.md) | Build breakage, pre-commit/lint failures, GitHub Actions failures |

## Typical flow

1. Implement with **cpp-core** and/or **qml-ui** (they hand off to each other at the C++/QML boundary).
2. Cover behavior changes with **test-engineer**.
3. If anything goes red, hand the failure to **build-ci**.
4. Before merging, run **code-reviewer** on the branch and fix its must-fix findings.

## Conventions for new agents

- Canonical file `<name>.agent.md` here, frontmatter keys: `name`, `description`, `argument-hint`,
  optional `tools` (omit to allow all enabled tools). Matching wrapper `<name>.md` in
  `.claude/agents/` with the same `name`/`description` and a body that only points here.
- Encode rules by *reference* to the canonical docs ([AGENTS.md](../../AGENTS.md),
  [CODING_STYLE.md](../../CODING_STYLE.md), [test/README.md](../../test/README.md)) and inline only
  the hard, CI-enforced rules — duplicated prose goes stale.
- Keep agents task-scoped: one agent per kind of work, not one per feature.
