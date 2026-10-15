# 07 — Persistent Memory (episodic, procedural, semantic)

> Index: [README.md](README.md). Code: `src/memory/*`. Queries: [04-graph-schema.md](04-graph-schema.md) §4.8. Tools: [08-mcp-protocol.md](08-mcp-protocol.md) §8.6.

## 7.1 Memory model

| Memory type (Track 2 vocabulary) | GraphState node | Written by | Read by |
|---|---|---|---|
| **Episodic** — what happened | `Session`, `Intent`, `Decision` (+ `MUTATED`, `TOUCHED`) | engine, automatically on every session/patch | `get_briefing`, `read_symbol.history`, `recall` |
| **Procedural** — how not to fail | `Pitfall` (+ lessons), quality rules | gate, automatically on rejection | every `propose_patch` result (`lessons`), `read_symbol.pitfalls`, `get_briefing`, `recall_pitfalls` |
| **Semantic** — durable facts | `Memory{kind: preference|convention|fact|todo|warning}` | agent via `remember` | `get_briefing`, `recall`, `read_symbol.memories` |
| **Per-user vs per-project** | `Memory.scope` → user graph `gs:__user__` vs project graph | `remember(scope)` | both merged in briefing/recall |
| **Working memory / handoff** — unfinished multi-step work | `Changeset` + `Obligation` (§06.14) | gate | briefing (`open_changeset`), `changeset{status}`; any later session/agent continues it |

Everything except `Memory` is written **without the agent having to remember to do it** — the gate and session manager record it as a side effect. This is the key reliability property: memory doesn't depend on LLM diligence.

## 7.2 Session lifecycle (`memory/sessions.cpp`)

One `graphstate serve` process = one Session.

```
process start ──► (first MCP request: `initialize` [legacy] or any request carrying _meta [modern])
                    └─ Q_SESSION_CREATE {id, client = clientInfo.name or "unknown", client_version}
                       Q_USER_PROFILE_UPSERT (user graph)
every tool call ──► heartbeat (Q_SESSION_HEARTBEAT, at most once / 30 s, pipelined with the tool's own queries)
end_session ──────► intents status update + Q_SESSION_END(summary)   (session stays usable; a later tool call re-opens it as ACTIVE)
stdin EOF/SIGTERM ► if not ended: auto-summary (§7.5) + Q_SESSION_END
kill -9 / crash ──► nothing; next startup: Q_SESSIONS_MARK_INTERRUPTED (last_seen_at older than session_timeout_s)
```

```cpp
namespace gs::memory {
struct SessionCtx { std::string session_id; std::string current_intent_id; std::string client; };
class Sessions {
 public:
  Sessions(Engine&);
  void mark_interrupted_on_startup();
  const SessionCtx& ensure(const nlohmann::json& client_info);  // idempotent; creates on first call
  void heartbeat();
  std::string upsert_intent(std::string_view goal);              // returns intent id, sets current_intent_id
  void end(std::string_view summary, const std::vector<std::string>& done, const std::vector<std::string>& abandoned);
  void end_on_shutdown();                                        // auto-summary if not ended
  const SessionCtx& ctx() const;
};
}
```

Session timeout: an ACTIVE session whose process died is marked INTERRUPTED at the next startup only if `last_seen_at < now − session_timeout_s` (120 s). Because there is one process per session, a second concurrently running engine on the same workspace (two IDE windows) keeps heartbeating and is not marked.

## 7.3 Intents

- Created by `get_briefing{goal}` or by the `intent` field of `propose_patch` (mandatory there).
- Dedupe within a session by `goal_norm` = lowercase, trimmed, whitespace runs → single space, trailing punctuation stripped.
- Status rules:
  - `end_session{intents_done, intents_abandoned}` sets them explicitly.
  - On `end_session` / shutdown, any intent of this session not listed: `DONE` if it has ≥ 1 `APPLIED` decision and its latest decision is `APPLIED`; else stays `OPEN`.
  - Interrupted sessions leave intents `OPEN` → they appear in the next briefing as `open_intents` with `session_status: "INTERRUPTED"` — "you were in the middle of X when the session died".

## 7.4 Decisions & pitfalls (recorded by the gate)

On every non-dry-run `propose_patch` (both outcomes) the gate calls `MemoryStore::record_patch(result, request, session)`:

1. `intent_id = sessions.upsert_intent(request.intent)`.
2. `Q_DECISION_RECORD` with `d = {patch_id, status, actor:"agent", intent, reasoning, failed_phase, violation_codes, files, symbols, ts, duration_ms}`.
   - `symbols` = ids in `added ∪ removed ∪ sig_changed ∪ body_changed` (post or pre ids).
3. APPLIED: after the code sync, `Q_DECISION_LINK_MUTATED(rows=[{id, change}])` (removed symbols simply won't link) and `Q_PITFALLS_RESOLVE(symbol_ids)`.
4. REJECTED: `Q_PITFALLS_UPSERT(rows)` (one row per error violation except environmental codes, §06.11).
5. All of 2–4 for APPLIED go in the **same MULTI/EXEC** as the code-graph sync, so code state and its Decision appear together.

Dry runs (`dry_run: true`) record nothing (agents use them to probe).

## 7.5 Auto-summary (deterministic, no LLM)

Used when the agent never called `end_session`:
```
"Auto-summary: {applied} applied / {rejected} rejected patches. Intents: {goal1} [{status}]; {goal2} [{status}]. Files: {top 5 files by decision count}."
```
Built from `Q_SESSION_AUTO_SUMMARY_DATA`. Truncate to 600 chars.

## 7.6 `remember` semantics (`MemoryStore::remember`)

Input: `{content, kind, scope?, about?: [refs], tags?: [], importance?: 1..3, supersedes?: memory_id}`.

1. Validate: `content` 1..2000 chars; `kind ∈ {preference, convention, fact, todo, warning}`.
2. `scope` default: `preference → user`, everything else → `project`.
3. Resolve `about` refs → ids: each ref may be a symbol id, qualname, name (must be unique), or file path. Unresolvable ref → tool error `INVALID_ANCHOR` with candidates (don't silently drop).
4. Dedupe: `Q_MEMORY_FIND_DUP(kind, content_norm)` on the target graph → if found: `Q_MEMORY_TOUCH` (merge tags, max importance) and return `{id, deduplicated: true}`.
5. `Q_MEMORY_CREATE` on the target graph; project scope → `Q_MEMORY_LINK(id, session, anchors)`. User scope → `source_session = "<workspace_key>/<session_id>"`, no edges (anchors kept as property only — code nodes live in another graph).
6. `supersedes` → `Q_MEMORY_SUPERSEDE(old, new)` on whichever graph holds `old` (try project, then user).
7. Secret guard (MUST): reject content matching likely secrets — regexes for `AKIA[0-9A-Z]{16}`, `-----BEGIN [A-Z ]*PRIVATE KEY-----`, `ghp_[A-Za-z0-9]{36}`, `sk-[A-Za-z0-9_-]{20,}`, `xox[baprs]-[A-Za-z0-9-]{10,}`, and `(?i)(password|secret|token)\s*[:=]\s*\S{8,}` → tool error `SECRET_DETECTED`. Memory persists on disk forever; never store credentials.

## 7.7 `recall` semantics (`MemoryStore::recall`)

Input: `{query, types?: ["memory","decision","pitfall","session","intent"], scope?: "all"|"project"|"user", about?: ref, limit?: 10}`.

1. `q = fulltext_query(query)` (§04.9). If empty and no `about` → error `EMPTY_QUERY`.
2. Run (pipelined) for each requested type: `Q_RECALL_MEMORY` (project graph and/or user graph per scope), `Q_RECALL_DECISIONS`, `Q_RECALL_PITFALLS`, `Q_RECALL_SESSIONS`, `Q_RECALL_INTENTS`, each with `limit`.
3. If `about` resolves to an id → `Q_RECALL_BY_ANCHOR(id)` (score 100 — anchored memories always rank first).
4. Merge: normalize each type's scores by dividing by that type's max score (RediSearch TF-IDF scores aren't comparable across indexes), multiply by type weight (`memory 1.0, pitfall 0.9, decision 0.8, intent 0.6, session 0.5`) and recency boost `1 + 0.25 * exp(-age_days / 14)`. Sort desc, take `limit`.
5. Each item: `{type, id, score, text (≤300 chars), meta…}`.

`// ponytail: lexical full-text recall; add a vector index (FalkorDB CREATE VECTOR INDEX … OPTIONS {dimension, similarityFunction:'cosine'}) with an embeddings provider when paraphrase recall matters.`

## 7.8 Briefing (`memory/briefing.cpp`) — `get_briefing{goal?}`

All reads in ONE pipeline (project graph) + one pipeline (user graph):

| Section | Query | Limit | Notes |
|---|---|---|---|
| `user_memories` | `Q_MEMORY_LIST(kinds=all)` on user graph | 15 | importance DESC, updated DESC |
| `project_memories` | `Q_MEMORY_LIST(kinds=all)` on project graph | 15 | `convention` and `warning` first |
| `open_changeset` | `Q_OPEN_CHANGESET` | 1 (+ first 10 obligations) | shown first: "continue this" |
| `open_intents` | `Q_OPEN_INTENTS(exclude_session=current)` | 5 | flag INTERRUPTED |
| `recent_sessions` | `Q_RECENT_SESSIONS(exclude=current)` | 3 | |
| `top_pitfalls` | `Q_TOP_PITFALLS` | 7 | `unresolved` first |
| `hot_symbols` | `Q_HOT_SYMBOLS(since = now − 7 d)` | 10 | |
| `critical_symbols` | `Q_CRITICAL_SYMBOLS` | 5 | PageRank top tier — "change these with care" |
| `ungated_changes` | `Q_UNGATED_SINCE(since = last ended session's ended_at)` | 10 | |
| `related` (only with goal) | recall(goal, all types, limit 8) | 8 | |
| `workspace` | FactStore stats + `Q_META_GET` | — | files, functions, classes, languages, last_indexed_at, graph name |
| `rules` | RulesEngine: rules with severity `error` | — | id + message |

If `goal` given → `upsert_intent(goal)` first; the response includes `intent_id`.

**Budget**: total rendered text ≤ 10 000 chars (~2.5k tokens). Truncation order when over budget: `related` → `hot_symbols` → `recent_sessions` summaries (to 200 chars) → memories (content to 200 chars) → drop lowest-importance memories.

**Rendering** (content[0].text, markdown, exactly these headings, omit empty sections):
```markdown
# GraphState briefing — benchmark_project (session s_9f1c…)
## You (user preferences)
- [preference] Prefers type hints on all public functions. (m_1a2b…)
## This project
- [convention] Money is Decimal, never float. (m_77de…)
## Open changeset (continue it)
- c_4e5f… "Build order API" — 2 files, 1 obligation: api/routes.py:1 MISSING_MODULE services
## Unfinished work
- "Add currency to totals" — previous session INTERRUPTED; last patch REJECTED (billing.py)
## Known pitfalls (most frequent first)
- ×3 CALLER_INCOMPATIBLE billing.py::calculate_total — Changing the signature … same propose_patch …
## Recent sessions
- 2026-10-15 claude-code ENDED: Added CSV export … (4 applied, 1 rejected)
## Recently changed hot spots
- billing.py::calculate_total (5 changes)
## Changes made outside the gate since your last session
- utils.py (UNGATED, findings: CALLER_INCOMPATIBLE)
## Enforced rules
- py-mutable-default-arg, py-bare-except, no-requests-without-timeout
## Workspace
- 41 files (python 33, typescript 8) · 212 functions · 37 classes · indexed 2 min ago
```
`structuredContent` carries the same data as JSON (schema in §08.6.1).

## 7.9 Cross-project user memory

- `gs:__user__` is shared by every workspace on this FalkorDB instance → preferences ("prefers pytest over unittest", "never add new dependencies without asking") follow the user.
- Project facts never go to the user graph unless `scope:"user"` is explicitly passed.
- `UserProfile.workspaces` lists workspace keys that have contributed (for `status`).

## 7.10 Privacy & retention

- Memory is local (FalkorDB on localhost, Docker volume). Nothing leaves the machine.
- `graphstate forget` (CLI only) deletes memories/pitfalls; there is deliberately no MCP delete tool (an agent should not be able to erase the user's history). Agents can only `supersedes` (archive) memories.
- Decisions store `reasoning` text written by the agent; the secret guard (§7.6) also runs on `reasoning` and `intent` → if a secret is detected the text is replaced by `"[redacted: possible secret]"` (the patch itself is not blocked).
