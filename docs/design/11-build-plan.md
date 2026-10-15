# 11 — Build Plan, Milestones & Hackathon Deliverables

> Index: [README.md](README.md). Window: **Oct 15 00:01 IST → Oct 18 23:59 IST** (96 h). Plan assumes 1–2 people + a coding agent.

## 11.1 How a coding agent should execute this spec

1. Read [README.md](README.md), then 01 → 03 fully; read 04–09 as each milestone needs them.
2. Implement milestones **in order**. Each has an acceptance check; do not start the next milestone until the check passes.
3. Where the spec says **(verify)** or "Day-1 verification", run the check first and, if reality differs, update the spec file in the same change (keep the doc truthful).
4. Never weaken a MUST to make a test pass; raise it instead.
5. Make a git commit after every milestone (the GraphState *repo itself* uses git from M0; the product never runs git).

## 11.2 Day-1 verification tasks (first 2 hours — they de-risk everything else)

| # | Check | If it fails |
|---|---|---|
| V1 | `docker compose up -d`; `redis-cli -p 6390 PING`; Browser at http://localhost:3000 | adjust compose (§03.6) |
| V2 | `GRAPH.QUERY t "CYPHER s='a\\'b\\nc' RETURN toJSON({s:$s, l:[1,'x'], m:{k:true}}) AS j"` returns valid JSON with the right string | switch reads to `--compact` parsing (§04.4.3) |
| V3 | map param property access `CYPHER d={a:1} RETURN $d.a` | flatten params (§04.8 note) |
| V4 | `CREATE FULLTEXT INDEX FOR (n:Decision) ON (n.reasoning, n.intent)` + `queryNodes` returns `score` | use `db.idx.fulltext.createNodeIndex` form |
| V5 | `GRAPH.CONSTRAINT CREATE t UNIQUE NODE File PROPERTIES 1 id` after index → `PENDING` | skip constraints (MERGE keeps ids unique anyway) |
| V6 | Pattern comprehension + slicing `[(a)-->(b) | b.id][0..2]`, `size([...])` | rewrite those queries with `OPTIONAL MATCH … collect()` |
| V7 | `ORDER BY` in `WITH` before `RETURN toJSON` | n/a (standard) |
| V8 | `CALL db.idx.fulltext.queryNodes(...) YIELD node, score WITH node, score WHERE …` | adjust |
| V9 | tree-sitter v0.27.1 + all grammars compile & `ts_parser_set_language` returns true for each (ABI 14/15) | pin older grammar tag |
| V10 | Claude Code / Antigravity: protocolVersion sent; hook stdin shapes (§09.8) | adjust templates |
| V16 | `CALL algo.pageRank('Function','CALLS')`, `algo.betweenness({...})`, `algo.WCC({...})` work on 6.0.2 (also via `GRAPH.RO_QUERY`) | fan-in percentile fallback (§04.8 analytics) |
| V17 | Claude Code headless flags for the eval (`-p`, `--output-format json`, `--mcp-config`, `--strict-mcp-config`, `--allowedTools`, `--disallowedTools`, `--max-turns`, `--permission-mode`) and the JSON fields for tokens/cost | adjust `run_eval.py` |
| V11–V15 | Windows: hiredis MSVC build, grammars with cl.exe, locked-file replace, Claude Code + Antigravity on Windows (§12.12) | RESP fallback client (§12.4); adjust retry policy |

## 11.3 Milestones

| M | Scope | Files | Acceptance check | Budget |
|---|---|---|---|---|
| **M0** Skeleton | git init, CMake + presets + deps + grammars, platform layer stubs, `graphstate version`, compose up, CI matrix | CMakeLists, CMakePresets, cmake/*, src/platform/*, main, cli skeleton, docker-compose, ci.yml | builds warning-free **on Windows (MSVC)**, then CI green on macOS + Linux; `graphstate version`; V1, V9, V11, V12 | 4 h |
| **M1** DB layer | GraphClient, params encoder, result parser, schema bootstrap, `query`/`doctor` commands | src/db/*, cmd_query, cmd_doctor | unit: cypher_params, result_parse; db: graph_client; V2–V8 resolved | 4 h |
| **M2** Parsing & facts | registry, parser pool, Python + TS + JS extractors, syntax collector, metrics, `debug-facts` | src/lang/* | unit goldens pass (py/ts/js), syntax, metrics | 8 h |
| **M3** Index | scanner, FactStore, resolver, GraphWriter, Indexer, `index`/`status` | src/index/* (minus watcher) | `graphstate index --root benchmark_project` → counts in §10.5; FalkorDB Browser shows CALLS graph; `test_graph_writer` | 6 h |
| **M3b** Analytics | `Analytics` (PageRank tiers, bridges, islands), write-back, debounce | index/analytics.* | db: test_analytics; Browser shows `criticality` on nodes; V16 | 2 h |
| **M4** MCP read path | transport, dual-era server, registry, schema validator, read tools, status/reindex | src/mcp/* (read/admin) | e2e `handshake_*`, `read_tools`, `errors`; Claude Code connects and `read_symbol` works | 5 h |
| **M5** Gate | patch model, P0–P5, BindingChecker, rules engine, DiskTxn + WAL + recovery, `move_file`, `propose_patch` | src/gate/* (minus changesets), lang/binding, platform file ops | unit: binding, apply, rules, disk_txn, platform; db: gate_e2e; e2e `gate_reject_apply`; V13 | 10 h |
| **M5b** Changesets | changesets, obligations recompute, snapshot/abandon, `changeset` tool + CLI, bulk-change rule | gate/changesets.*, tools_write, cmd_changeset | db: test_changesets; e2e `changeset_flow` | 4 h |
| **M6** Memory | sessions, intents, decisions/pitfalls recording, remember/recall/recall_pitfalls/end_session, briefing | src/memory/*, tools_memory | db: test_memory; e2e `memory_flow`; briefing renders | 6 h |
| **M7** Durability & realtime | watcher, ungated audit + notices, startup recovery paths, `test_restart` | index/watcher, engine startup | kill -9 demo works (US-8, US-9); `test_restart` | 4 h |
| **M8** Host kits | `init`, `hook deny-edit`, templates, AGENTS.md | cli/cmd_init, cmd_hook, host-kits/* | §09.8 checklist for Claude Code + Antigravity | 3 h |
| **M10a** Eval harness | eval workspace (§13.2), tests, tasks, checks, `graphstate audit`, `run_eval.py`, `report.py` | eval/*, gate/audit, cmd_audit | unit: test_audit; a 1-task × 2-arm dry run completes; V17 | 3 h |
| **M10b** Eval run | full or minimum run (§13.7), `docs/eval.md` | eval/results, docs/eval.md | table committed; numbers in README + blog | 2 h wall (parallel with M9) |
| **M9** Bench & polish | `bench`, synthetic generator, perf fixes, README, blog, demo video | cmd_bench, tools/*, README.md, docs/blog.md | §01.9 targets met or documented; demo script runs end-to-end twice in a row | 8 h |

Total ≈ 67 h of build (agent-assisted; real coding time is lower, verification/debugging dominates) → ~20% slack inside 4 days. All acceptance checks run on the Windows dev machine; CI covers macOS/Linux.

Suggested calendar (IST):
- **Day 1 (Oct 15)**: M0, M1, M2 (start).
- **Day 2 (Oct 16)**: M2 (finish), M3, M4. *Checkpoint: agent can read the graph through MCP.*
- **Day 2 add-on**: M3b right after M3 (needs only the code graph).
- **Day 3 (Oct 17)**: M5, M5b, M6; evening M10a. *Checkpoint: demo steps 1–3 work + a greenfield changeset closes + eval dry run passes.*
- **Day 4 (Oct 18)**: start M10b runs first thing (they run unattended), then M7, M8, M9; record video by 18:00; submit by 21:00 (3 h buffer).

## 11.4 Cut lines (if behind schedule, cut in this order)

1. `GS_WITH_CPP_GRAMMAR`, Rust/Java/Go/C syntax-only grammars (keep Python/TS/TSX/JS/JSON).
2. Cursor + Claude Desktop kits (keep Claude Code + Antigravity).
3. `get_module_map` hotspots, `recall` type weighting (plain score sort).
4. Custom rules `unless_query`, `#any-of?`.
5. JS extractor (keep TS/TSX; `.js` becomes syntax-only).
6. Watcher thread (keep lazy `ensure_fresh` + startup reconcile — still correct, just not "realtime").
7. Changeset `abandon` (keep begin/status/finalize; humans can still revert by hand).
8. Eval: drop to minimum config (10 tasks × 2 arms × 1 run), then to 5 tasks — but keep *some* measured result.
9. Analytics betweenness + WCC (keep PageRank criticality — it is the Track 1 "graph algorithm" story).
10. macOS/Linux CI jobs (keep Windows build + local tests) — but keep the platform layer; never put OS calls elsewhere.
**Never cut**: P2–P4 gate phases, multi-file atomic write + WAL, changeset begin/finalize, memory recording, briefing, restart recovery, Windows build — they *are* the product and the demo.

## 11.5 README.md (hackathon-mandatory contents)

The rules require: public repo, README with setup **and the graph data model**, **the Cypher queries the product depends on**, demo video, live deployment or full local setup, tracks entered, and **disclosure of AI assistant use**.

README outline:
1. **GraphState** — one-line pitch + 30-second GIF (reject → fix → apply).
2. **Tracks**: Track 1 (Agents That Act on Connected Data) and Track 2 (Agent Memory and Coordination) — one paragraph each on how.
3. **Why the graph does real work** — table from §04.10, incl. the FalkorDB algorithms (PageRank criticality, betweenness bridges, WCC islands).
3b. **Results** — the with/without table from `docs/eval.md` (§13.6) with the fairness + circularity notes; link to the raw results.
4. **Quick start** (≤ 6 commands): compose up → build → `graphstate init --host claude-code` → open Claude Code → "start with get_briefing".
5. **Graph data model** — the diagram + label/relationship tables from §04.2–4.3 (trimmed).
6. **Cypher queries** — the blast-radius query (`Q_CALLERS_OF_IDS`), impact query (`Q_IMPACT`), read context (`Q_READ_SYMBOL_CONTEXT`), pitfall upsert, briefing queries; link to `src/db/queries.hpp` for all.
7. **How the gate works** — phase diagram (§06.1) + one rejected example JSON.
8. **Memory** — model table (§07.1) + briefing sample.
9. **Durability** — AOF/volume + WAL + reconcile story; "kill -9 it" instructions.
10. **Benchmarks** — table from `docs/bench.md`.
11. **Enforcement, honestly** — §09.1 table.
12. **AI disclosure** — "Built with Claude Code (Opus 5.5) and <others>; design spec in docs/design was co-written with an AI assistant; all code reviewed and directed by <team>."
13. **License** Apache-2.0.

## 11.6 Blog post outline (`docs/blog.md`, side prize)

Title: *"I built a bouncer for AI coding agents with FalkorDB"*
1. The 3 a.m. bug: an agent changed one signature and broke four files.
2. Idea: make the codebase a graph, make the graph the only door.
3. Architecture in one diagram; why C++ and why FalkorDB (Cypher + sub-ms + Redis persistence).
4. The blast-radius query, explained line by line.
5. Call-binding: why `param_count` isn't enough (defaults, `*args`, keyword-only).
6. Memory that doesn't depend on the LLM remembering to remember (gate side effects).
7. Crash it on purpose: WAL + AOF + reconcile demo.
8. The experiment: 10 tasks, with vs. without GraphState (§13) — quality *and* cost; where the gate didn't help.
9. Graph algorithms as agent context: PageRank on the call graph tells the agent which functions are load-bearing.
10. Numbers (bench table), limits (static resolution, honest enforcement, eval circularity), what's next (embeddings, type-aware edges, post-check tests).

## 11.7 Demo video script (≤ 3 min; matches §01.8)

| t | Shot |
|---|---|
| 0:00 | Title + one sentence |
| 0:10 | FalkorDB Browser: CALLS graph of benchmark_project |
| 0:30 | Claude Code: "Add a required currency parameter to calculate_total" → REJECTED with 4 call sites |
| 1:00 | Agent retries with multi-file patch → APPLIED (7 ms) |
| 1:20 | Agent calls `utils.format_money` → MISSING_SYMBOL, did you mean `format_currency` |
| 1:40 | Browser: Pitfall node linked to calculate_total |
| 1:55 | `kill -9` engine; `docker restart graphstate-falkordb` |
| 2:10 | New session: `get_briefing` shows preference, interrupted intent, pitfall |
| 2:25 | "Build an order API": changeset → routes first (1 obligation) → services → finalize CLOSED |
| 2:40 | Eval table: broken refs / hallucinated calls without vs with |
| 2:50 | `graphstate bench` table (5 s) |
| 2:55 | Repo link + tracks + "runs on Windows, macOS, Linux" |

## 11.8 Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| FalkorDB 6.0 (Rust rewrite, 1 week old) behaves differently from docs | M | H | Day-1 V2–V8; fallback image `falkordb/falkordb:v4.22.0` (C engine, `v`-prefixed tag) — keep compose image a variable |
| Grammar compile time (cpp parser.c 17 MB) slows iteration | H | M | `GS_WITH_CPP_GRAMMAR=OFF` during dev; ccache |
| Hosts send unexpected protocol versions / shapes | M | M | dual-era server; log every initialize/discover; e2e both eras |
| Static call resolution misses edges → gate false negatives | H | M | confidence levels; exact-only errors; demo uses well-resolved code; documented limits |
| Heuristic edges cause false rejections | M | H | heuristic → warn only (never blocks) |
| Re-indent bugs corrupt Python files | M | H | P2 syntax gate runs on the post-state → corrupted indentation is rejected, never written; unit tests on tabs/spaces |
| hiredis or a grammar fails to build with MSVC | M | H | Day-1 V11/V12; RESP fallback client (§12.4); clang-cl preset |
| Windows file locking (editors/antivirus) breaks writes | M | M | retry policy in `replace_file`; Defender exclusion tip in doctor; WAL rollback guarantees no partial state |
| Time | H | H | milestones + cut lines (§11.4) |
| "Design work must begin after start" rule (§01.7) | — | — | user accepted risk; disclose spec timing honestly in README |
