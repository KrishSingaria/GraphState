# GraphState — Technical Design Specification

**GraphState** is a cross-platform (Windows · macOS · Linux) C++20 MCP server that indexes a codebase into a **FalkorDB** graph, acts as the **only write path** for AI coding agents (syntax → signature diff → blast radius → reference check → quality rules → atomic write), and gives agents **persistent memory** across sessions and crashes.

Entry for *Graph Hacks: Context for AI Agents* (WeMakeDevs × FalkorDB, Oct 15–18 2026), Tracks 1 and 2.
Spec version 1.0 · 2026-10-14 · all third-party versions/APIs verified on that date.

## Documents

| # | File | Contents | Read when |
|---|---|---|---|
| 01 | [01-product-and-scope.md](01-product-and-scope.md) | pitch, user stories, scope, decision log vs. original draft, demo script, success metrics | first |
| 02 | [02-architecture.md](02-architecture.md) | topology, threads, startup/shutdown, data flows, crash recovery, latency budget, failure modes | first |
| 03 | [03-repo-and-build.md](03-repo-and-build.md) | full file tree, pinned deps, CMake, config schema, env vars, docker-compose, CLI | M0 |
| 04 | [04-graph-schema.md](04-graph-schema.md) | graphs, labels, relationships, wire protocol, param encoding, **every Cypher query** | M1, M3, M6 |
| 05 | [05-indexer.md](05-indexer.md) | languages, facts model, Python/TS/JS AST walks, signatures, resolver, metrics, indexer, watcher | M2, M3, M7 |
| 06 | [06-gate.md](06-gate.md) | patch model, phases P0–P6, binding checker, rules engine, WAL write/recovery, violation catalogue, changesets, VCS relationship | M5, M5b |
| 07 | [07-memory.md](07-memory.md) | sessions, intents, decisions, pitfalls, remember/recall, briefing | M6 |
| 08 | [08-mcp-protocol.md](08-mcp-protocol.md) | dual-era MCP, instructions text, all 14 tool schemas with example JSON, resources, prompts | M4–M6 |
| 09 | [09-host-integration.md](09-host-integration.md) | Claude Code / Antigravity / Cursor / Claude Desktop configs, hooks, enforcement model | M8 |
| 10 | [10-testing.md](10-testing.md) | unit/integration/e2e tests, fixtures, demo project contents, benchmarks | every milestone |
| 11 | [11-build-plan.md](11-build-plan.md) | day-1 verification, milestones with acceptance checks, cut lines, README/blog/video, risks | first, then continuously |
| 12 | [12-platform.md](12-platform.md) | Windows/macOS/Linux: toolchains, presets, flags, platform layer API, path/EOL/locking rules, CI | M0 (Windows first) |
| 13 | [13-eval.md](13-eval.md) | with-vs-without evaluation: workspace, 10 tasks, `graphstate audit`, runner, metrics, report | M10 |

## For the coding agent

- Follow [11-build-plan.md](11-build-plan.md) §11.1 and the milestone order in §11.3.
- **MUST/SHOULD/MAY** are binding (RFC 2119). Code shapes (structs, class declarations, queries, JSON) are normative; prose examples are illustrative.
- When reality contradicts the spec (an API differs, a query fails on FalkorDB 6.0.2), fix the code **and** update the spec section in the same change.
- `// ponytail:` comments mark deliberate simplifications with a known ceiling; keep them in code.

## Glossary

| Term | Meaning |
|---|---|
| Workspace / root | The repository GraphState guards (`--root`). |
| FactStore | In-RAM cache of per-file parse results (`FileFacts`). Rebuilt at startup. |
| Code subgraph | `File`/`Class`/`Function` nodes + structural/call edges. Derived from disk. |
| Memory subgraph | `Session`/`Intent`/`Decision`/`Pitfall`/`Memory`. Authoritative; never rebuilt. |
| Definition range | Byte range of a symbol incl. decorators / `export` wrapper (§06.4.1). |
| Post-state | File contents after applying a patch's edits in memory, before it is written. |
| Blast radius | Call sites and importers affected by a removed or signature-changed symbol. |
| Exact / heuristic edge | `CALLS` edge resolved via imports/scope vs. by unique-name guess. Only exact edges can reject. |
| Pitfall | Recorded lesson from a rejected patch, keyed by (code, subject, rule). |
| Ungated write | A file change not made through `propose_patch` (detected by watcher/freshness check). |
| APPLIED | Patch verified and written to the working tree. Not a VCS commit — GraphState never runs git writes. |
| Changeset | A unit of multi-patch work (greenfield/refactor) in which cross-file findings become obligations; closes only when none remain; can be abandoned (restores files). |
| Criticality | PageRank tier (`high`/`medium`/`low`) of a function on the call graph, computed by FalkorDB `algo.pageRank`. |
| Arm A / B | Eval runs without / with GraphState (§13). |
| Obligation | An open deferrable finding (missing symbol, broken caller/import) inside a changeset, recomputed after every patch. |
| WAL | Write-ahead journal in `.graphstate/wal/` that makes multi-file writes atomic and crash-safe. |
| Legacy / modern era | MCP ≤ 2025-11-25 (initialize handshake) vs. 2026-07-28 (stateless, `_meta`). |

## Verified external facts this spec relies on (2026-10-14)

- FalkorDB **6.0.2** (Rust engine; `GRAPH.*` + Cypher unchanged). Params via `CYPHER k=v` prefix; list/map literals allowed; strings support `\\ \' \" \n \r \t` escapes, no `\u`. `toJSON()` exists. `GRAPH.RO_QUERY` exists. Each query atomic; MULTI/EXEC supported. Indexes/constraints build asynchronously. Data dir `/var/lib/falkordb/data`; `REDIS_ARGS` / `FALKORDB_ARGS` env vars; image bundles Browser on :3000. Plan cache default 25.
- MCP latest **2026-07-28** (no `initialize`, `server/discover`, `resultType`, `ttlMs/cacheScope`, error −32022); hosts/SDKs still default to **2025-11-25**. No JSON-RPC batching. `instructions` honored by Claude Code (≤ 4 096 chars).
- tree-sitter **0.27.1** (ABI 15, min 13; timeouts removed in 0.26 → progress callbacks). Grammars: python 0.25.0, typescript 0.23.2, javascript 0.25.0, go 0.25.0, rust 0.24.2, java 0.23.5, c 0.24.2, cpp 0.23.4.
- hiredis **1.4.1**, nlohmann/json **3.12.0**, doctest **2.5.3**, xxHash **0.8.4**.
- Hackathon rules: FalkorDB must be primary and do real work; README must include data model + Cypher queries; AI assistance must be disclosed; demo video + public repo + local setup.
