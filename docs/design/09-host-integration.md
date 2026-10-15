# 09 — Host Integration & Enforcement

> Index: [README.md](README.md). Code: `src/cli/cmd_init.cpp`, `src/cli/cmd_hook.cpp`, templates in `host-kits/`. Verified 2026-10-14 against code.claude.com, cursor.com/docs, antigravity.google/docs. Items marked **(verify)** were not fully confirmed — check on day 1 and adjust templates.

## 9.1 Enforcement model (honest version)

An MCP server cannot disable a host's built-in file tools. "Agents can only write through the gate" is achieved by three layers:

| Layer | Mechanism | Strength |
|---|---|---|
| L1 Instruct | MCP `instructions` (§08.4), rules file (`AGENTS.md` / host rules) | advisory |
| L2 Block | Host permissions deny native edit tools + PreToolUse hook that blocks them with a message pointing to `propose_patch` | strong for native edit tools; **does not** stop an agent running e.g. `python -c "open('x','w')…"` via shell (Claude Code docs: Edit deny rules cover recognized shell file commands like `sed`/`tee`/redirection, not arbitrary subprocesses) |
| L3 Detect | Watcher + lazy freshness check record every out-of-band change as `Decision{actor:"external", status:"UNGATED"}`, run the gate's checks retroactively, and push findings into the next tool result and the next briefing | complete detection, after the fact |

The README and demo MUST describe it this way (no "impossible to bypass" claims).

## 9.2 `graphstate init --host <h>`

- Writes the host-kit files below into `--root` (default cwd). For existing JSON files: deep-merge (`mcpServers.graphstate`, permission arrays de-duplicated, hook arrays appended if not present); never delete user keys. Existing non-JSON files (rules markdown) → skip with a message unless `--force`.
- Placeholders substituted: `{{GRAPHSTATE_BIN}}` = absolute path of the running binary (`platform::executable_path()`), `{{ROOT}}` = absolute workspace root — both written with **forward slashes** on every OS (`C:/Users/me/bin/graphstate.exe`), and quoted inside hook command strings because Windows user paths often contain spaces (§12.10).
- Also: creates `.graphstate/`, appends `.graphstate/wal/` and `.graphstate/logs/` to `.gitignore`, runs `doctor`, prints next steps.

## 9.3 Claude Code

`.mcp.json` (project scope; Claude Code asks the user to approve project servers once):
```json
{
  "mcpServers": {
    "graphstate": {
      "command": "{{GRAPHSTATE_BIN}}",
      "args": ["serve", "--root", "{{ROOT}}"],
      "env": { "GRAPHSTATE_LOG_LEVEL": "info" }
    }
  }
}
```
`.claude/settings.json`:
```json
{
  "permissions": {
    "allow": ["mcp__graphstate__*"],
    "deny": ["Edit", "Write", "NotebookEdit"]
  },
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "^(Edit|Write|MultiEdit|NotebookEdit)$",
        "hooks": [ { "type": "command", "command": "{{GRAPHSTATE_BIN}} hook deny-edit --host claude-code" } ]
      }
    ]
  }
}
```
Notes:
- MCP tool permission names: `mcp__graphstate__<tool>`; `mcp__graphstate__*` = whole server.
- Hook matchers are regexes — anchor them (`Edit.*` would also match `NotebookEdit`).
- Deny rules apply regardless of hooks; the hook exists to give the model an actionable message.
- Claude Code honours the server `instructions` (truncated at 4 096 chars).

`graphstate hook deny-edit --host claude-code` contract:
- stdin: hook JSON (`{"tool_name":"Edit","tool_input":{"file_path":"/abs/path/x.py",…},…}`).
- If `tool_input.file_path` (or `notebook_path`) is **inside** the workspace root → print to **stderr**:
  `GraphState: direct edits are disabled in this repository. Use mcp__graphstate__propose_patch (ops: replace_symbol, insert_symbol, delete_symbol, replace_text, create_file, delete_file). Read the target first with mcp__graphstate__read_symbol or get_file_outline.` → **exit 2** (blocks; stderr is shown to the model).
- Outside the workspace (or unparsable input) → exit 0 (allow; the deny rule still applies if configured).
- Must finish in < 50 ms (no DB connection).

## 9.4 Google Antigravity

`.agents/mcp_config.json` (workspace) — or global `~/.gemini/config/mcp_config.json`:
```json
{
  "mcpServers": {
    "graphstate": {
      "command": "{{GRAPHSTATE_BIN}}",
      "args": ["serve", "--root", "{{ROOT}}"],
      "cwd": "{{ROOT}}"
    }
  }
}
```
`.agents/hooks.json` **(verify exit-code semantics; output JSON is documented)**:
```json
{
  "graphstate-gate": {
    "PreToolUse": [
      {
        "matcher": "write_to_file|replace_file_content|multi_replace_file_content",
        "hooks": [ { "type": "command", "command": "{{GRAPHSTATE_BIN}} hook deny-edit --host antigravity" } ]
      }
    ]
  }
}
```
`hook deny-edit --host antigravity` → stdout `{"decision":"deny","reason":"GraphState: direct edits are disabled… use the graphstate MCP tool propose_patch …"}`, exit 0. Path detection: read `tool_input`'s target-file field **(verify field name; if absent, deny unconditionally inside a workspace that has `.graphstate/`)**.

Antigravity CLI permissions (`~/.gemini/antigravity-cli/settings.json`) — optional, user-level:
```json
{ "permissions": { "deny": ["write_file(*)"], "allow": ["mcp(graphstate/*)"] } }
```
Rules file `.agents/rules/graphstate.md`:
```markdown
---
trigger: always_on
---
@AGENTS.md
```
(If `@` includes are unsupported **(verify)**, inline the AGENTS.md text.)

## 9.5 Cursor

`.cursor/mcp.json`:
```json
{ "mcpServers": { "graphstate": { "command": "{{GRAPHSTATE_BIN}}", "args": ["serve", "--root", "${workspaceFolder}"] } } }
```
`.cursor/hooks.json` **(verify the edit tool names covered by matcher `Write`)**:
```json
{ "version": 1, "hooks": { "preToolUse": [ { "command": "{{GRAPHSTATE_BIN}} hook deny-edit --host cursor", "matcher": "Write" } ] } }
```
`hook deny-edit --host cursor` → stdout `{"permission":"deny","agent_message":"GraphState: … use propose_patch …","user_message":"Blocked a direct edit; GraphState gate is active."}`, exit 0.
Rules `.cursor/rules/graphstate.mdc`:
```markdown
---
description: GraphState gate rules
alwaysApply: true
---
<AGENTS.md text>
```

## 9.6 Claude Desktop

`~/Library/Application Support/Claude/claude_desktop_config.json` (macOS) — `init --host claude-desktop` prints the snippet instead of writing it (file is outside the workspace; user pastes it):
```json
{ "mcpServers": { "graphstate": { "command": "{{GRAPHSTATE_BIN}}", "args": ["serve", "--root", "{{ROOT}}"] } } }
```
Claude Desktop has no native file-write tools, so the gate is the only write path. Logs: `~/Library/Logs/Claude/mcp*.log`.

## 9.7 `host-kits/AGENTS.md` (workspace rules template)

```markdown
# Working in this repository (GraphState gate is active)

- Start with `get_briefing` (pass your goal). Respect the preferences, conventions and pitfalls it lists.
- Do not edit files directly. All changes go through `propose_patch`; direct edits are blocked or flagged.
- Read before you write: `get_file_outline` for structure, `read_symbol` for the exact source + callers.
- Changing a signature or removing a symbol? Run `get_impact`, then update the definition and ALL callers in ONE `propose_patch`.
- Only call functions that exist (`search_symbols` if unsure).
- On REJECTED: fix every violation listed (file:line + fix_hint), then resubmit. Never resubmit unchanged.
- Save durable preferences/conventions with `remember`. Never store secrets.
- Finish with `end_session` and a short summary.
```

## 9.8 Verification checklist (day 1, 30 minutes — on Windows first, see also §12.12)

- [ ] Claude Code: `claude mcp list` shows graphstate connected; asking the agent to "edit README.md directly" is blocked with the GraphState message; `propose_patch` works.
- [ ] Claude Code: which `protocolVersion` does it send in `initialize`? Log it (engine logs every initialize at INFO).
- [ ] Antigravity: MCP server appears; hook blocks `write_to_file`; record actual hook stdin JSON shape in `docs/design/09-host-integration.md`.
- [ ] Cursor: same.
- [ ] Bypass test: ask the agent to write a file via `python -c` → watcher records `UNGATED` and the next tool result carries a notice.
