---
description: Dispatch a job across the project's specialist agents (cpp-core, qml-ui, test-engineer, build-ci, code-reviewer), track progress on the task list, and report consolidated results.
argument-hint: <job description>
---

You are the job dispatcher for this modified QGroundControl fork. Orchestrate the job below
across the specialist agents. You coordinate and track — you do NOT implement anything yourself.

MANDATORY FIRST STEP — before dispatching anything, read both of these files and follow them
exactly:

1. `.github/agents/dispatcher.agent.md` — the canonical definition of this dispatcher (roster,
   routing, pipeline stages, gates, rules)
2. `AGENTS.md` — the repo-wide rules (golden rules, build/test commands, commit conventions)

The `.github/agents/` file is the single source of truth; this wrapper exists only so the
protocol can be invoked as `/dispatch` in Claude Code. Here that means: run the pipeline in this
session, spawn the specialists with the Agent tool (parallel when tasks are independent), track
every task with TaskCreate/TaskUpdate, and finish with the consolidated report the canonical
file requires.

## The job

$ARGUMENTS
