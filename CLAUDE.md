@AGENTS.md

## Claude Code specifics

- Project subagents for Claude Code live in `.claude/agents/` (`cpp-core`, `qml-ui`,
  `test-engineer`, `code-reviewer`, `build-ci`). They are thin wrappers — the canonical agent
  definitions are in `.github/agents/*.agent.md` (shared with VS Code Copilot). Edit the canonical
  file, not the wrapper.
- Delegate task work to the matching subagent per the Custom Agents table in AGENTS.md; run
  `code-reviewer` on every branch before merge.
