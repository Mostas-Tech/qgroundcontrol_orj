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
| [dispatcher](dispatcher.agent.md) | Orchestrating a whole job across the agents above — plans, routes, gates, reports |

## Typical flow

1. **dispatcher** probes once, makes a context packet, and assigns one implementation owner per
   disjoint area; overlapping files/contracts are serialized.
2. **cpp-core** and/or **qml-ui** implement, retaining their context for failure-driven follow-ups.
3. One **test-engineer** owns narrow targeted tests until green; run a related label only when
   required.
4. One command-only **build-ci** final gate runs build/lint and classifies the first real failure.
   Domain failures return to the existing owner; build-ci edits only build/tooling/lint/CI ownership.
5. One **code-reviewer** performs the consolidated final review. One failure-driven fix/review cycle
   is allowed; repeated same-class failures escalate with concise evidence.

Dispatcher routes GPT-5.6 only: terra medium (orchestration/tests), terra high (C++/QML), luna low
(final gate), and sol high (review/escalation). Hosts without explicit model choice must use another
available GPT-5.6 tier while preserving role and effort intent. If no GPT-5.6 model is available,
stop and ask the user for guidance; never silently select Claude or `gpt-5.3-codex`. Follow-ups use
the existing addressable owner context rather than replacement agents. Or describe the whole job to
**dispatcher**, which runs this flow for you. Its Claude Code entry point is the `/dispatch` slash
command (`.claude/commands/dispatch.md`) rather than a `.claude/agents/` wrapper, so orchestration
stays in the main session where you can watch task progress and intervene.

## Conventions for new agents

- Canonical file `<name>.agent.md` here, frontmatter keys: `name`, `description`, `argument-hint`,
  optional `model` (one exact runtime model ID or a prioritized array), and optional `tools` (omit
  to allow all enabled tools). Matching wrapper `<name>.md` in `.claude/agents/` with the same
  `name`/`description` and a body that only points here.
- Encode rules by *reference* to the canonical docs ([AGENTS.md](../../AGENTS.md),
  [CODING_STYLE.md](../../CODING_STYLE.md), [test/README.md](../../test/README.md)) and inline only
  the hard, CI-enforced rules — duplicated prose goes stale.
- Keep agents task-scoped: one agent per kind of work, not one per feature.
