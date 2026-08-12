# Codex Shared Project Baseline

This file is the single Codex-specific baseline for the primary session and every specialist.
Read it before acting, together with the repository `AGENTS.md` and the specialist's canonical
`.github/agents/<role>.agent.md` file.

## Canonical command rule

For every executable build, test, lint, configure, or diagnostic action, run only the applicable
`just` or `.venv\Scripts\just.exe` recipe. Do not run direct CMake, CTest, Ninja, pre-commit,
Python, or any other fallback command.

If that exact `just` recipe fails before its recipe begins because of a Codex sandbox WindowsApps,
Python-launcher, or access error, retry the exact same recipe once with sandbox escalation and
user approval. This is the same canonical gate, not a different validation route. If the elevated
recipe still fails, report its first real failure and do not run another command as a substitute.

The explicit dependency-database `ninja -t recompact` action in the Windows environment skill is
maintenance only; it is never a build, test, lint, configure, or diagnostic fallback.
