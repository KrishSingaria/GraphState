# 01 — Product & Scope

> Part of the GraphState technical design. Index: [README.md](README.md).
> Normative keywords: **MUST**, **SHOULD**, **MAY** (RFC 2119). Anything marked MUST is a build requirement for the coding agent.

## 1.1 One-line pitch

**GraphState is a C++20 MCP server that turns a codebase into a live FalkorDB graph and becomes the only door through which an AI coding agent can change code: every write is syntax-checked, signature-diffed, blast-radius-checked against real callers, quality-checked, and remembered — across sessions and restarts.**

## 1.2 Problem

AI coding agents today:

1. **Read blindly.** They `cat`/`grep` whole files, burn context, and still miss callers in other files.
2. **Write blindly.** They change a function signature and silently break 6 call sites in 4 files; they call helpers that do not exist (hallucinated symbols); they reintroduce bugs that were fixed last week.
3. **Forget everything.** Each new session starts from zero: no memory of user preferences, past decisions, failed attempts, or project conventions.
4. **Lose state on crash.** Agent tooling state lives in process memory; kill the process and it's gone.

## 1.3 Solution — four pillars

| # | Pillar | What it does | Hackathon track |
|---|---|---|---|
| P1 | **AST Code Graph (read path)** | Tree-sitter parses the repo into FalkorDB: `File`, `Class`, `Function` nodes; `DEFINES`, `HAS_METHOD`, `INHERITS`, `IMPORTS`, `CALLS` edges. Agents query symbols, callers, callees, impact sets, file outlines — not raw files. | Track 1 (agents that act on connected data) |
| P2 | **Verification Gate (write path)** | All writes go through `propose_patch`. A 6-phase pipeline (preflight → apply → syntax → symbol diff → blast radius + reference check → quality rules) decides APPLY or REJECT with exact file/line evidence. Writes are crash-safe (journaled atomic rename) and update the graph incrementally. | Track 1 |
| P3 | **Persistent Memory** | Sessions, intents, decisions, pitfalls (rejected-patch lessons), and explicit memories (preferences, conventions, facts) live in the same graph as the code, linked to the symbols they touched. New sessions get a briefing. | Track 2 (agent memory & coordination) |
| P4 | **Durability & Realtime** | FalkorDB in Docker with AOF+RDB on a named volume. Engine is stateless-restartable: on start it recovers its write journal, reconciles disk vs graph by content hash, and marks interrupted sessions. A watcher keeps the graph in sync with out-of-band edits and flags ungated writes. | Track 2 |

"Teaches the agent to write quality code" = P2 quality rules + P3 pitfalls: every rejection returns a structured lesson, the lesson is stored, and it is surfaced proactively the next time any session touches that symbol/file/rule.

## 1.4 Users & usage

- **Primary user:** a developer running an agentic IDE/CLI (Claude Code, Google Antigravity, Cursor, Claude Desktop) on a local repo.
- **Primary actor:** the coding agent (LLM) calling MCP tools.
- **Secondary:** the developer inspecting the graph in FalkorDB Browser and via `graphstate status`.

### User stories (acceptance-level)

| ID | Story | Acceptance |
|---|---|---|
| US-1 | As an agent, I get a compact outline of a file (symbols + signatures + line ranges) without reading it. | `get_file_outline` returns all top-level and class-member symbols, p50 < 3 ms on a 1 000-line file. |
| US-2 | As an agent, I read one function's source plus who calls it and what it calls. | `read_symbol` returns source slice + callers + callees + recent history + pitfalls in one call. |
| US-3 | As an agent, if I change `calculate_total(cart)` to `calculate_total(cart, tax_rate)` without updating callers, I am stopped with every broken call site. | `propose_patch` returns `REJECTED`, phase `BLAST_RADIUS`, one violation per call site with file+line+col+call text. Disk unchanged. |
| US-4 | As an agent, I can change a signature and all its callers in **one** atomic patch. | Multi-file `propose_patch` with all callers updated → `APPLIED`; all files written or none. |
| US-5 | As an agent, if I call a function that doesn't exist in a workspace module, I'm stopped. | `REJECTED`, violation `MISSING_SYMBOL` with the module path and closest existing names. |
| US-6 | As an agent, if I submit code with a syntax error, I get exact line/col. | `REJECTED`, phase `SYNTAX`, violation `SYNTAX_ERROR` with line, col, and the offending line text. |
| US-7 | As an agent starting a new session, I learn what the user prefers and what went wrong before. | `get_briefing` returns user/project memories, last sessions' summaries, open intents, top pitfalls, recently changed symbols. |
| US-8 | As a developer, if I `kill -9` the engine or restart Docker, nothing is lost. | After restart: graph intact, memories intact, interrupted session marked `INTERRUPTED`, a crash mid-commit is rolled back cleanly. |
| US-9 | As a developer editing files in my IDE, the graph stays current. | External edit visible in `read_symbol` within ≤ 1 s (watcher) and always on the next tool call touching that file (lazy check). |
| US-11 | As an agent building a new feature over several patches, I'm not blocked by references to code I haven't written yet, but I can't finish while anything is left dangling. | `changeset begin` → patch referencing a missing module → `APPLIED` with 1 obligation → patch creating it → 0 obligations → `finalize` → `CLOSED`. `finalize` with obligations stays OPEN. |
| US-12 | As a developer without git, I can undo a whole multi-step agent attempt. | `changeset abandon` restores every touched file byte-exact; refuses (no data loss) if a file was edited outside the changeset since. |
| US-13 | As a Windows developer, everything works natively. | All acceptance checks pass on Windows 11 with MSVC + Docker Desktop; CI green on macOS/Linux. |
| US-10 | As a developer, I can encode a project rule ("no `print` in `src/`") and the gate enforces it. | Custom rule in `.graphstate/rules.json` (Tree-sitter query) → violations with severity error reject the patch. |

## 1.5 Scope

### In scope (MVP — must ship by end of hackathon)

- Languages, **semantic** (symbols, signatures, imports, calls, blast radius): **Python**, **TypeScript**, **TSX**, **JavaScript**.
- Languages, **syntax gate only**: Go, Rust, Java, C, C++, JSON. (Any file whose extension maps to one of these gets Phase 3 syntax checking; other phases are skipped.)
- Other files (Markdown, YAML, TOML, text, …): writable through the gate as pass-through (journaled + recorded, no checks) unless `gate.unknown_files = "deny"`.
- **Windows, macOS and Linux** (Windows is the primary dev platform) — [12-platform.md](12-platform.md).
- Strict mode for edits to existing code **and changeset mode for building new things / multi-step refactors** (§06.14).
- VCS-agnostic: `APPLIED` = written to the working tree; GraphState never runs git writes (§06.15).
- **Graph algorithms as agent context**: PageRank criticality on the call graph, betweenness bridge modules, WCC islands (§05.14).
- **Evaluation harness**: same tasks with vs. without GraphState, reported in `docs/eval.md` (§13).
- 14 MCP tools (see [08-mcp-protocol.md](08-mcp-protocol.md)), 2 MCP resources, 1 MCP prompt.
- CLI: `serve`, `index`, `status`, `doctor`, `query`, `bench`, `forget`, `init`, `hook`, `version` (+ hidden `debug-facts`).
- Host integration kits for Claude Code (permission deny + PreToolUse hook), Antigravity, Cursor, Claude Desktop.
- Docker Compose for FalkorDB with persistence; FalkorDB Browser for visualization.
- `benchmark_project/` demo repo + scripted demo.

### Explicitly out of scope (MVP)

| Not doing | Why | Upgrade path |
|---|---|---|
| Embedding / vector search for memory | Needs a model runtime or API key; full-text index covers the demo | Add `embeddings.provider` (Ollama/OpenAI HTTP) + FalkorDB vector index on `Memory.embedding` |
| Type inference (e.g. resolving `obj.method()` where `obj`'s type is inferred) | Requires a type checker | Optional Pyright/tsserver bridge emitting extra `CALLS` edges with `confidence:"typed"` |
| tsconfig `paths` / webpack aliases, Python namespace packages, `sys.path` hacks | Long tail | Config `resolve.aliases` map |
| Running tests as a gate phase | Slow, project-specific | Phase 7 `gate.post_check_cmd` (stretch, §06) |
| Multi-user / remote server, auth | Local single-user tool | Streamable HTTP transport + token |
| Git write operations (add/commit/branch/stash) | GraphState is VCS-agnostic; "APPLIED" means written to the working tree (§06.15) | optional `gate.on_apply_cmd` hook |

## 1.6 Changes vs. the original draft (decision log)

The draft from the earlier AI chat is the base. These are deliberate changes; the coding agent MUST follow this doc where they conflict.

| # | Draft said | This spec says | Reason |
|---|---|---|---|
| D1 | `propose_symbol_patch` edits one symbol | `propose_patch` takes **multiple edits across multiple files**, validated and written atomically | With single-symbol patches a signature change can never pass: callers mismatch until they are updated, and they can't be updated first. |
| D2 | Blast radius matches `target {name: $func_name}` | Matches by **stable symbol id** `path::Qual.name`; name-only matches are `heuristic` and only warn | Two `save()` functions in different files must not block each other. |
| D3 | Compare `param_count` vs `arg_count` | Full **call-binding check** (positional-only, keyword-only, defaults, `*args`, `**kwargs`, TS optional/rest params, keyword-argument names) | `def f(a, b=1)` called with 1 arg is valid; count equality would give false rejections. |
| D4 | Gate checks only the changed symbol | Gate also checks **outgoing calls of new code**: missing symbol in a resolved workspace module → reject; arity mismatch against existing callee → reject | Catches hallucinated helpers — the most common agent failure. |
| D5 | "Agents cannot write directly to the filesystem" | Enforced by **host configuration** (deny native edit tools + PreToolUse hook), backed by **ungated-write detection** in the engine | An MCP server cannot stop a host's built-in tools on its own; we must configure the host and detect bypasses honestly. |
| D6 | Pitfalls only | Adds **quality rules engine** (built-in metrics + project rules as Tree-sitter queries) and explicit **Memory** nodes (preferences, conventions, facts) | Delivers "teaches agent quality" and "knows the user". |
| D7 | Memory in the same graph | Code + project memory in graph `gs:<workspace>`; **user memory** in shared graph `gs:__user__` | User preferences must follow the user across projects; project facts must not leak between repos. |
| D8 | "Writes to disk and updates graph" | **Write-ahead journal + temp file + fsync + rename**, rollback on startup if interrupted; disk is the source of truth for code, graph is a materialized index | Crash safety with no partial writes. |
| D9 | Redis AOF = "zero loss" | AOF `appendfsync everysec` default (≤ 1 s window for memory nodes; code facts are re-derivable from disk); `always` configurable | Honest durability claims. |
| D10 | Store source? (unspecified) | Graph stores **byte/line ranges + hashes, not source text**; source is sliced from disk | Keeps graph small; disk is authoritative; no escaping huge strings. |
| D11 | Polyglot semantic for 9 languages incl. HTML/CSS | Semantic for Py/TS/TSX/JS; syntax-only for Go, Rust, Java, C, C++, JSON; HTML/CSS dropped | 4-day budget. HTML/CSS have no symbols worth gating. |
| D12 | Engine name "GraphGate" | Product **GraphState**, binary `graphstate`, MCP server name `graphstate` | Matches repo folder. |
| D13 | Statuses COMMITTED/REJECTED | **APPLIED**/REJECTED/VALIDATED; phases EDIT/WRITE | No git involvement: "commit" would mislead agents and users (§06.15). |
| D14 | Gate is patch-only | **Changesets** (obligations, snapshot, abandon) for greenfield and multi-step work; `move_file` op; limits 200 edits / 100 files | Building new code needs forward references and runnable intermediate states (§06.14). |
| D16 | Plain Cypher only | Also FalkorDB **algorithms** (PageRank, betweenness, WCC) feeding tools, gate warnings and briefing | Track 1 explicitly asks for graph algorithms as agent tools (§05.14). |
| D17 | No measurement | **With/without evaluation** (§13) + `graphstate audit` | A measured result is the strongest evidence for judges and the blog. |
| D15 | Unix-only implied (hiredis, POSIX) | **Windows-first cross-platform** with a single `platform/` layer | Developer machine is Windows (§12). |

## 1.7 Hackathon constraints

- Event: **Graph Hacks: Context for AI Agents** (WeMakeDevs × FalkorDB), online, **Oct 15 00:01 IST → Oct 18 23:59 IST, 2026**.
- **FalkorDB must be the primary graph database and do real work** ("if the product still works once FalkorDB is taken out, the graph is decoration"). ✔ Blast radius, impact, read context and all memory are FalkorDB queries; the gate refuses to write without the DB (§04.10).
- **"The main coding and design work must begin after the hackathon starts"** (notes, graph-model sketches, diagrams allowed before). This spec was written before the start; the team accepted that risk. Disclose it honestly in the README. **No code exists before Oct 15.**
- **AI coding assistants are allowed but must be disclosed** → README §AI disclosure (§11.5).
- Submission must include: public repo; README with setup **and the graph data model**; **the Cypher queries the product depends on**; demo video; live deployment *or* complete local setup; tracks entered (1 and 2).
- Live deployment preferred; a local dev tool can't meaningfully be hosted → one-command local setup (`docker compose up -d && cmake --build`) plus a recorded demo.
- Side prize: best blog post → outline in [11-build-plan.md](11-build-plan.md) §11.6.
- IP stays with the team. Licence: **Apache-2.0**.

## 1.8 Demo script (judges, ~3 min)

Uses `benchmark_project/` (see [10-testing.md](10-testing.md) §10.5). Pre-state: indexed, FalkorDB Browser open on the graph.

1. **"It sees the code as a graph."** `get_file_outline billing.py` → show the graph in FalkorDB Browser (`MATCH p=()-[:CALLS]->() RETURN p`).
2. **"It won't let the agent break things."** Ask the agent: *"Add a required `currency` parameter to `calculate_total`."* The agent edits only `billing.py` → **REJECTED**: 4 call sites (`app.py`, `checkout.py`, `reports.py`, and `Invoice.total` in `billing.py` itself), each with line:col. The agent retries with one multi-file patch → **APPLIED** in ~10 ms.
3. **"It won't let the agent hallucinate."** The agent writes `utils.format_money(x)` (doesn't exist; the real one is `format_currency`) → **REJECTED** `MISSING_SYMBOL`, suggestion `format_currency`.
4. **"It learns."** Show the `Pitfall` node linked to `calculate_total` in Browser. `kill -9` the engine; `docker restart falkordb`.
5. **"It remembers."** New session → `get_briefing` mentions: user prefers type hints (remembered preference), last session's interrupted intent, and the pitfall "signature changes to `calculate_total` need callers in the same patch".
6. **"It's fast."** `graphstate bench` table: p50/p99 for read_symbol, propose_patch, re-index.

## 1.9 Success metrics (targets the build MUST measure, see §10.6)

| Metric | Target |
|---|---|
| Cold full index, 1 000 files / 100 k LOC (Py+TS) | < 3 s |
| Single-file incremental re-index (graph write included) | p50 < 5 ms |
| `read_symbol` end-to-end (JSON in → JSON out) | p50 < 2 ms, p99 < 10 ms |
| `propose_patch` single file, APPLIED (excl. fsync) | p50 < 10 ms |
| `propose_patch` rejection | p50 < 5 ms |
| Engine RSS for 100 k LOC | < 150 MB |
| Engine restart to ready (100 k LOC, unchanged) | < 2 s |
| Eval (§13): introduced broken refs + hallucinated calls, arm B vs arm A | B ≤ 10 % of A, with task success not lower than A |
