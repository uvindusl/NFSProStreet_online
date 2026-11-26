# .agents

Framework-neutral agent workspace for NFS ProStreet static recompilation.

This directory complements `.github/agents/` (GitHub Copilot/VS Code agent files) with conventions used by other coding-agent tools.

## Structure

- `rules/` - always-on project rules.
- `workflows/` - task workflows (boot debug, PR prep, shader checks).
- `skills/` - reusable skill playbooks for specific domains.
- `memory/` - persistent, non-sensitive project memory/checklists. Do not store secrets or copyrighted game content.

## Usage

- Read `rules/project-rules.md` before making changes.
- Pick a workflow from `workflows/` for common tasks.
- Use `.github/agents/*.agent.md` when your tool supports GitHub custom agents.
- Keep notes factual; include commands and observed output.
