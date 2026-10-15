# 04 — Graph Schema, Wire Protocol & Every Cypher Query

> Index: [README.md](README.md). Code: `src/db/*`. Target: **FalkorDB 6.0.2** (verified 2026-10-14).
> This file is also the source for the README's mandatory "graph data model" and "Cypher queries" sections (§11.5).

## 4.1 Graphs

| Graph key | Contents | Rebuildable? |
|---|---|---|
| `gs:<workspace_key>` | `Meta`, code subgraph (`File`, `Class`, `Function`), project memory (`Session`, `Intent`, `Decision`, `Pitfall`, `Memory{scope:"project"}`, `Changeset`, `Obligation`) | Code subgraph: yes (from disk). Memory: **no**. |
| `gs:__user__` | `UserProfile`, `Memory{scope:"user"}` | **no** |

`graphstate index --full` deletes only code labels (`Q_CODE_WIPE`), never memory labels.

## 4.2 Node labels & properties

Types: `str`, `int` (64-bit), `bool`, `float`, `str[]` (array of strings), `int[]`. Timestamps are **ms since Unix epoch** (`int`), produced by the engine (`core::now_ms()`) and passed as `$now`, never `timestamp()`, so the engine's clock is the single source of time.

### Code subgraph

**`(:Meta)`** — exactly one per project graph.

| Prop | Type | Notes |
|---|---|---|
| `id` | str | always `"meta"` |
| `schema_version` | int | `1` |
| `root` | str | absolute workspace path |
| `workspace_key` | str | |
| `engine_version` | str | `GS_VERSION` |
| `created_at`, `last_indexed_at` | int | |

**`(:File)`**

| Prop | Type | Example / rule |
|---|---|---|
| `id` | str | workspace-relative path, `/` separators: `"src/billing.py"` (**unique**) |
| `language` | str | `python` `typescript` `tsx` `javascript` `go` `rust` `java` `c` `cpp` `json` `unknown` |
| `semantic` | bool | true for python/typescript/tsx/javascript |
| `hash` | str | xxh3_64 hex (16 chars) of file bytes |
| `size` | int | bytes |
| `mtime_ns` | int | |
| `line_count` | int | |
| `parse_ok` | bool | false if the tree has errors |
| `symbol_count` | int | functions + classes (excl. `<module>`) |
| `skipped_reason` | str | `""` or `too_large` / `binary` / `unreadable` |
| `indexed_at` | int | |

**`(:Class)`** — Python classes, TS/JS classes, TS interfaces.

| Prop | Type | Notes |
|---|---|---|
| `id` | str | `"<file>::<qualname>"` e.g. `"src/billing.py::Invoice"` (**unique**) |
| `name` | str | `Invoice` |
| `qualname` | str | `Invoice`, nested: `Outer.Inner` |
| `file` | str | File id |
| `kind` | str | `class` \| `interface` |
| `bases` | str[] | base expressions as written: `["BaseModel", "mixins.Auditable"]` |
| `is_exported` | bool | TS/JS `export`; Python: name not starting with `_` |
| `start_line`, `end_line` | int | 1-based, inclusive, from the **definition range** (§06.4.1) |
| `start_byte`, `end_byte` | int | definition range |
| `doc` | str | first line of docstring/JSDoc, ≤ 160 chars |
| `search_text` | str | full-text helper: identifier split into words + raw name, e.g. `"invoice Invoice"` |

**`(:Function)`** — functions, methods, constructors, exported arrow functions, and one `<module>` pseudo-function per semantic file (owner of top-level calls).

| Prop | Type | Notes |
|---|---|---|
| `id` | str | `"src/billing.py::calculate_total"`, `"src/billing.py::Invoice.total"`, `"app.py::<module>"` (**unique**) |
| `name` | str | `calculate_total` / `<module>` |
| `qualname` | str | `Invoice.total` |
| `file` | str | File id |
| `kind` | str | `function` \| `method` \| `constructor` \| `arrow` \| `module` |
| `class_id` | str | owning Class id or `""` |
| `signature` | str | canonical display, e.g. `"calculate_total(cart: Cart, tax_rate: float = …) -> Decimal"` |
| `params_json` | str | JSON array of `Param` (§05.6) — authoritative for binding checks |
| `required_params` | int | count of params that must be supplied (after dropping self) |
| `max_params` | int | `-1` if variadic |
| `is_async`, `is_exported`, `is_static` | bool | |
| `decorators` | str[] | `["staticmethod", "app.route"]` (name part only) |
| `return_type` | str | annotation text or `""` |
| `start_line`, `end_line`, `start_byte`, `end_byte` | int | definition range |
| `body_hash` | str | xxh3 of body with whitespace runs collapsed |
| `loc` | int | lines in the definition range |
| `complexity` | int | cyclomatic (§05.8) |
| `max_nesting` | int | |
| `doc` | str | first docstring line, ≤ 160 chars |
| `search_text` | str | e.g. `"calculate total calculate_total"` |
| `pagerank` | float | PageRank on the `CALLS` graph (§05.14); 0 until first analytics run |
| `pr_pct` | float | percentile of `pagerank` among functions, 0–100 |
| `criticality` | str | `high` (top 5 %) \| `medium` (top 20 %) \| `low` \| `` (not computed) |

### Memory subgraph (project graph)

**`(:Session)`**

| Prop | Type | Notes |
|---|---|---|
| `id` | str | `"s_" + 12 hex` |
| `client` | str | clientInfo.name (`"claude-code"`, `"antigravity"`, …) or `"unknown"` |
| `client_version` | str | |
| `status` | str | `ACTIVE` \| `ENDED` \| `INTERRUPTED` |
| `started_at`, `last_seen_at`, `ended_at` | int | `ended_at` = 0 while active |
| `summary` | str | agent-provided via `end_session`, or auto-summary (§07.5) |
| `patch_count`, `applied_count`, `rejected_count` | int | |

**`(:Intent)`**

| Prop | Type | Notes |
|---|---|---|
| `id` | str | `"i_" + 12 hex` |
| `goal` | str | ≤ 500 chars |
| `goal_norm` | str | lowercase, whitespace-collapsed (dedupe key within a session) |
| `status` | str | `OPEN` \| `DONE` \| `ABANDONED` |
| `created_at`, `updated_at` | int | |

**`(:Decision)`** — one per `propose_patch` (non-dry-run) and per detected ungated write.

| Prop | Type | Notes |
|---|---|---|
| `patch_id` | str | (**unique**) `"p_…"` or `"x_…"` for external |
| `status` | str | `APPLIED` \| `REJECTED` \| `UNGATED` (external edit, audited) \| `SYNCED` (bulk external change, e.g. git checkout) \| `REVERTED` (changeset abandoned) |
| `actor` | str | `agent` \| `external` |
| `intent` | str | copy of the intent goal (denormalized for fast reads) |
| `reasoning` | str | ≤ 4000 chars |
| `failed_phase` | str | `""` or phase name |
| `violation_codes` | str[] | distinct error codes |
| `files` | str[] | touched file ids |
| `symbols` | str[] | touched symbol ids (authoritative; `MUTATED` edges are best-effort links) |
| `ts` | int | |
| `duration_ms` | float | |

**`(:Pitfall)`**

| Prop | Type | Notes |
|---|---|---|
| `key` | str | (**unique**) xxh3 of `code|subject|rule_id` (§06.11) |
| `code` | str | violation code |
| `rule_id` | str | `""` unless RULE_VIOLATION |
| `symbol_id` | str | subject symbol or `""` |
| `file` | str | subject file |
| `lesson` | str | generated lesson text |
| `message` | str | last violation message |
| `count` | int | times triggered |
| `resolved_count` | int | |
| `first_seen`, `last_seen`, `last_resolved` | int | `last_resolved` 0 if never |

**`(:Memory)`** — in both graphs (`scope` tells which).

| Prop | Type | Notes |
|---|---|---|
| `id` | str | `"m_" + 12 hex` (**unique**) |
| `kind` | str | `preference` \| `convention` \| `fact` \| `todo` \| `warning` |
| `scope` | str | `user` \| `project` |
| `content` | str | ≤ 2000 chars |
| `content_norm` | str | dedupe key |
| `tags` | str[] | |
| `anchors` | str[] | symbol/file ids this memory is about (authoritative) |
| `importance` | int | 1 (low) – 3 (high), default 2 |
| `archived` | bool | superseded/forgotten |
| `superseded_by` | str | |
| `source_session` | str | session id (project graph) or `"<workspace_key>/<session id>"` (user graph) |
| `created_at`, `updated_at` | int | |

**`(:Changeset)`** — incremental work unit (§06.14).

| Prop | Type | Notes |
|---|---|---|
| `id` | str | (**unique**) `"c_" + 12 hex` |
| `intent` | str | |
| `status` | str | `OPEN` \| `CLOSED` \| `ABANDONED` |
| `files` | str[] | touched files |
| `open_obligations` | int | after the latest recompute |
| `created_at`, `updated_at`, `closed_at` | int | `closed_at` 0 while open |

**`(:Obligation)`** — an open consistency finding inside a changeset (replaced on every recompute).

| Prop | Type | Notes |
|---|---|---|
| `id` | str | stable hash (§06.14.6) |
| `code` | str | deferrable violation code |
| `message`, `fix_hint` | str | |
| `file` | str | location file |
| `line`, `col` | int | |
| `symbol_id` | str | subject symbol or `""` |
| `created_at` | int | |

**`(:UserProfile)`** (user graph): `{id:"local", created_at, workspaces: str[]}`.

## 4.3 Relationships

| Type | From → To | Props | Written by |
|---|---|---|---|
| `DEFINES` | File → Class / Function (top-level only, incl. `<module>`) | — | indexer |
| `HAS_METHOD` | Class → Function | — | indexer |
| `INHERITS` | Class → Class | `confidence` str | indexer (only resolved bases) |
| `IMPORTS` | File → File | `names` str[], `line` int, `whole_module` bool, `alias` str | indexer (workspace targets only) |
| `CALLS` | Function → Function | `line` int, `col` int, `pos_args` int, `kw_names` str[], `has_star` bool, `has_dstar` bool, `confidence` str (`exact`\|`heuristic`), `call_kind` str (`plain`\|`instance`\|`classmethod`\|`constructor`\|`unbound`), `call_text` str (≤120 chars) | resolver — **one edge per call site** |
| `HAS_INTENT` | Session → Intent | — | memory |
| `MADE` | Session → Decision | — | memory |
| `RESOLVED_BY` | Intent → Decision | — | memory |
| `MUTATED` | Decision → Function / Class | `change` str (`added`\|`removed`\|`signature`\|`body`) | memory (best-effort; `Decision.symbols` is authoritative) |
| `TOUCHED` | Decision → File | — | memory |
| `TRIGGERED` | Decision → Pitfall | — | memory |
| `ABOUT` | Pitfall → Function / Class; Memory → Function / Class / File | — | memory (relinked after re-index, Q_RELINK_*) |
| `IN_FILE` | Pitfall → File | — | memory |
| `NOTED` | Session → Memory | — | memory (project-scope memories only) |
| `WORKED_ON` | Session → Changeset | — | gate (every session that applied a patch in it) |
| `INCLUDES` | Changeset → Decision | — | gate |
| `HAS_OBLIGATION` | Changeset → Obligation | — | gate (recompute) |

```
(File)-[:DEFINES]->(Function|Class)          (Class)-[:HAS_METHOD]->(Function)
(File)-[:IMPORTS]->(File)                    (Class)-[:INHERITS]->(Class)
(Function)-[:CALLS]->(Function)
(Session)-[:HAS_INTENT]->(Intent)-[:RESOLVED_BY]->(Decision)<-[:MADE]-(Session)
(Decision)-[:MUTATED]->(Function|Class)      (Decision)-[:TOUCHED]->(File)
(Decision)-[:TRIGGERED]->(Pitfall)-[:ABOUT]->(Function|Class)
(Pitfall)-[:IN_FILE]->(File)                 (Memory)-[:ABOUT]->(Function|Class|File)
(Session)-[:NOTED]->(Memory)
(Session)-[:WORKED_ON]->(Changeset)-[:INCLUDES]->(Decision)
(Changeset)-[:HAS_OBLIGATION]->(Obligation)
```

Edge deletion note: in FalkorDB, `DELETE n` on a node also removes its edges (implicit detach). We use plain `DELETE` everywhere.

## 4.4 Wire protocol (hiredis)

Query style rule (MUST): a read query that sorts/limits does it in `WITH … ORDER BY … LIMIT …` **before** the final `RETURN toJSON(...) AS j`, never after it (the final column is an opaque string).

### 4.4.1 Command shape

Every query is sent with `redisCommandArgv` / `redisAppendCommandArgv` (binary-safe; no format-string interpolation):

```
argv = ["GRAPH.QUERY" | "GRAPH.RO_QUERY", <graph>, <params_prefix + " " + cypher>, "TIMEOUT", "<ms>"]
```
- Read-only queries MUST use `GRAPH.RO_QUERY` (FalkorDB rejects writes there — a free safety check).
- Default mode (no `--compact`).

### 4.4.2 Parameter encoding (`db/cypher_params.cpp`)

FalkorDB parameters are a textual prefix: `CYPHER k1=<literal> k2=<literal> <query>`. Values MUST be literals.

```cpp
namespace gs::db {
// {"id":"a.py::f","n":3,"rows":[{"a":1}]}  →  "CYPHER id='a.py::f' n=3 rows=[{a:1}]"
std::string encode_params(const nlohmann::json& params);   // throws gs::Error(INVALID_PARAM) on bad key/value
std::string encode_literal(const nlohmann::json& v);
}
```

| JSON value | Cypher literal |
|---|---|
| `null` | `null` |
| `true`/`false` | `true`/`false` |
| integer | decimal (`-5`) |
| float | `%.17g`; NaN/Inf → throw |
| string | single-quoted; escape `\` → `\\`, `'` → `\'`, newline → `\n`, CR → `\r`, tab → `\t`; all other bytes verbatim (UTF-8 passes through; there are **no** `\u` escapes in FalkorDB) |
| array | `[` + comma-joined literals + `]` |
| object | `{` + `key:literal` pairs + `}`; keys MUST match `[A-Za-z_][A-Za-z0-9_]*` (else throw) |

Param names MUST match `[A-Za-z_][A-Za-z0-9_]*`. Nesting depth ≤ 100 (FalkorDB limit) — our max is 3.
Unit tests: round-trip strings containing `'`, `\`, `\n`, `"`, emoji, `$`, `}`; nested list of maps; empty list; key validation.

### 4.4.3 Reply parsing (`db/result.cpp`)

- Reply array of length 3 → `[header, rows, stats]`; length 1 → `[stats]` (no RETURN).
- **Convention: every read query returns exactly one column named `j` whose value is `toJSON(<map or list>)`.** The parser takes each row's single bulk string and `nlohmann::json::parse`s it. This avoids parsing FalkorDB's verbose-mode display strings for lists/maps (which are not machine-parseable).
- Write queries return nothing; the parser extracts stats lines (`"Nodes created: 3"`, `"Relationships deleted: 2"`, `"Query internal execution time: 0.31 milliseconds"`, `"Cached execution: 1"`) into `QueryStats {nodes_created, nodes_deleted, rels_created, rels_deleted, props_set, cached, exec_ms}`.
- `REDIS_REPLY_ERROR` → throw `gs::Error(DB_QUERY, reply->str, {"query_name": name})`.
- *Day-1 verification task:* run `RETURN toJSON({a:[1,'x'],b:{c:true}}) AS j` against 6.0.2 and confirm valid JSON. Fallback if `toJSON` misbehaves for some type: switch that query to `--compact` and parse `[type,value]` cells (type enum: 1 NULL, 2 STRING, 3 INTEGER, 4 BOOLEAN, 5 DOUBLE, 6 ARRAY, 10 MAP as flat `[k,[t,v],…]`).

### 4.4.4 `GraphClient` (`db/graph_client.hpp`)

```cpp
namespace gs::db {
struct QueryStats { int nodes_created=0, nodes_deleted=0, rels_created=0, rels_deleted=0, props_set=0; bool cached=false; double exec_ms=0; };
struct Query { std::string_view name; std::string_view cypher; nlohmann::json params; bool read_only = false; };

class GraphClient {
 public:
  GraphClient(std::string host, int port, std::string password, int connect_timeout_ms, int query_timeout_ms);
  ~GraphClient();                                         // redisFree
  bool ping();                                            // PING → PONG
  std::vector<nlohmann::json> rows(const std::string& graph, const Query& q);   // parses column j of every row
  nlohmann::json one(const std::string& graph, const Query& q);                 // first row or null
  QueryStats exec(const std::string& graph, const Query& q);                    // write
  // Pipelined: send all, then read all. Returns per-query results in order. Throws on first error AFTER draining.
  std::vector<nlohmann::json> pipeline(const std::string& graph, const std::vector<Query>& qs);
  // MULTI … EXEC: atomic batch of writes (FalkorDB docs recommend MULTI/EXEC for multi-query transactions).
  void transaction(const std::string& graph, const std::vector<Query>& qs);
  std::vector<std::string> list_graphs();                 // GRAPH.LIST
  void delete_graph(const std::string& graph);            // GRAPH.DELETE
  void constraint_unique(const std::string& graph, std::string_view label, std::string_view prop); // GRAPH.CONSTRAINT CREATE
  nlohmann::json raw(const std::vector<std::string>& argv); // debug (cmd_query, doctor)
 private:
  void ensure_connected();                                // reconnect once on REDIS_ERR_IO/EOF
  redisContext* ctx_ = nullptr;
};
}
```
- Connection: `redisConnectWithTimeout`; `redisSetTimeout` = query_timeout_ms + 1000; `AUTH` if password set.
- Reconnect policy: on I/O error, `redisReconnect` once, retry the command once **only for read-only queries**; write failures are surfaced (caller decides; gate turns them into `DB_UNAVAILABLE`).
- MULTI/EXEC caveat: Redis does not roll back earlier commands in a MULTI if a later one errors at runtime. All our transaction queries are idempotent upserts, so a re-run (next reconcile) heals partial application.
- Every query logs at DEBUG: name, ms, `cached`.

## 4.5 IDs

| Entity | Format | Generated by |
|---|---|---|
| File | relative path | indexer |
| Symbol | `<file>::<qualname>`; module pseudo: `<file>::<module>`; TS default export without name: `<file>::default` | extractor |
| Duplicate qualname in one file (Python redefinition; TS overload impls) | last definition wins the plain id; earlier ones get `#2`, `#3` suffix by order of appearance (`a.py::f#2`) | extractor |
| Session / Intent / Memory / Patch | `s_` / `i_` / `m_` / `p_` + 12 lowercase hex from `std::random_device`-seeded `mt19937_64` | `core::ids` |
| External decision | `x_` + 12 hex | watcher |
| Pitfall key | xxh3 hex of `code|subject|rule_id` | gate |

## 4.6 Schema bootstrap (`db/schema.cpp`) — run at every startup, idempotent

Order matters: range index → unique constraint (requires index). Index/constraint creation is **asynchronous** in FalkorDB (index fills in the background; `CALL db.indexes()` shows `UNDER CONSTRUCTION` → `OPERATIONAL`). Startup does not wait; queries work meanwhile (just slower).

"Already exists" errors MUST be ignored (match reply text containing `already` / `exists`, case-insensitive).

Project graph:
```cypher
CREATE INDEX FOR (n:File) ON (n.id)
CREATE INDEX FOR (n:Class) ON (n.id)
CREATE INDEX FOR (n:Class) ON (n.file)
CREATE INDEX FOR (n:Function) ON (n.id)
CREATE INDEX FOR (n:Function) ON (n.file)
CREATE INDEX FOR (n:Function) ON (n.name)
CREATE INDEX FOR (n:Session) ON (n.id)
CREATE INDEX FOR (n:Session) ON (n.status)
CREATE INDEX FOR (n:Intent) ON (n.id)
CREATE INDEX FOR (n:Decision) ON (n.patch_id)
CREATE INDEX FOR (n:Decision) ON (n.ts)
CREATE INDEX FOR (n:Pitfall) ON (n.key)
CREATE INDEX FOR (n:Pitfall) ON (n.symbol_id)
CREATE INDEX FOR (n:Pitfall) ON (n.file)
CREATE INDEX FOR (n:Memory) ON (n.id)
CREATE INDEX FOR (n:Meta) ON (n.id)
CREATE INDEX FOR (n:Changeset) ON (n.id)
CREATE INDEX FOR (n:Changeset) ON (n.status)
CREATE INDEX FOR (n:Obligation) ON (n.id)
CREATE FULLTEXT INDEX FOR (n:Function) ON (n.search_text)
CREATE FULLTEXT INDEX FOR (n:Class) ON (n.search_text)
CREATE FULLTEXT INDEX FOR (n:Memory) ON (n.content)
CREATE FULLTEXT INDEX FOR (n:Decision) ON (n.reasoning, n.intent)
CREATE FULLTEXT INDEX FOR (n:Pitfall) ON (n.lesson)
CREATE FULLTEXT INDEX FOR (n:Session) ON (n.summary)
CREATE FULLTEXT INDEX FOR (n:Intent) ON (n.goal)
```
Then `GRAPH.CONSTRAINT CREATE <g> UNIQUE NODE <Label> PROPERTIES 1 <prop>` for: `File id`, `Class id`, `Function id`, `Session id`, `Intent id`, `Decision patch_id`, `Pitfall key`, `Memory id`, `Meta id`, `Changeset id`. (Reply `PENDING` is success.)

User graph: `Memory(id)` index + unique constraint, `UserProfile(id)` index, `CREATE FULLTEXT INDEX FOR (n:Memory) ON (n.content)`.

*Day-1 verification:* multi-property `CREATE FULLTEXT INDEX … ON (n.a, n.b)` on 6.0.2. Fallback: `CALL db.idx.fulltext.createNodeIndex('Decision', 'reasoning', 'intent')`.

Meta:
```cypher
-- Q_META_UPSERT
MERGE (m:Meta {id:'meta'})
ON CREATE SET m.schema_version = $schema_version, m.created_at = $now
SET m.root = $root, m.workspace_key = $key, m.engine_version = $engine_version
```
```cypher
-- Q_META_GET (RO)
MATCH (m:Meta {id:'meta'}) RETURN toJSON({schema_version: m.schema_version, last_indexed_at: m.last_indexed_at}) AS j
```
Migration policy: `schema_version` absent/lower → run migrations `1..N` in order (none yet for v1); higher → refuse to start: `"graph was written by a newer graphstate (schema N)"`.

## 4.7 Code-subgraph queries (`db/queries.hpp`)

Naming: `Q_<NAME>` = `constexpr std::string_view`. `(RO)` = send with `GRAPH.RO_QUERY`. Batch params are lists of maps; batch size ≤ 2 000 rows per query (split larger).

### Writes — file sync (one MULTI/EXEC per batch of ≤ 50 files)

```cypher
-- Q_FILES_UPSERT   params: rows=[{id, language, semantic, hash, size, mtime_ns, line_count, parse_ok, symbol_count, skipped_reason}], now
UNWIND $rows AS r
MERGE (f:File {id: r.id})
SET f.language = r.language, f.semantic = r.semantic, f.hash = r.hash, f.size = r.size,
    f.mtime_ns = r.mtime_ns, f.line_count = r.line_count, f.parse_ok = r.parse_ok,
    f.symbol_count = r.symbol_count, f.skipped_reason = r.skipped_reason, f.indexed_at = $now
```

```cypher
-- Q_FUNCTIONS_DELETE_STALE   params: files=[file ids], keep=[function ids that still exist in those files]
UNWIND $files AS fid
MATCH (s:Function {file: fid})
WHERE NOT s.id IN $keep
DELETE s
```
```cypher
-- Q_CLASSES_DELETE_STALE   (same shape, :Class)
UNWIND $files AS fid
MATCH (s:Class {file: fid})
WHERE NOT s.id IN $keep
DELETE s
```

```cypher
-- Q_CLASSES_UPSERT   params: rows=[{id,name,qualname,file,kind,bases,is_exported,start_line,end_line,start_byte,end_byte,doc,search_text}]
UNWIND $rows AS r
MERGE (c:Class {id: r.id})
SET c.name = r.name, c.qualname = r.qualname, c.file = r.file, c.kind = r.kind, c.bases = r.bases,
    c.is_exported = r.is_exported, c.start_line = r.start_line, c.end_line = r.end_line,
    c.start_byte = r.start_byte, c.end_byte = r.end_byte, c.doc = r.doc, c.search_text = r.search_text
```

```cypher
-- Q_FUNCTIONS_UPSERT   params: rows=[{id,name,qualname,file,kind,class_id,signature,params_json,required_params,
--                                    max_params,is_async,is_exported,is_static,decorators,return_type,start_line,end_line,
--                                    start_byte,end_byte,body_hash,loc,complexity,max_nesting,doc,search_text}]
UNWIND $rows AS r
MERGE (f:Function {id: r.id})
SET f.name = r.name, f.qualname = r.qualname, f.file = r.file, f.kind = r.kind, f.class_id = r.class_id,
    f.signature = r.signature, f.params_json = r.params_json, f.required_params = r.required_params,
    f.max_params = r.max_params, f.is_async = r.is_async, f.is_exported = r.is_exported, f.is_static = r.is_static,
    f.decorators = r.decorators, f.return_type = r.return_type, f.start_line = r.start_line, f.end_line = r.end_line,
    f.start_byte = r.start_byte, f.end_byte = r.end_byte, f.body_hash = r.body_hash, f.loc = r.loc,
    f.complexity = r.complexity, f.max_nesting = r.max_nesting, f.doc = r.doc, f.search_text = r.search_text
```

```cypher
-- Q_STRUCT_EDGES_CLEAR   params: files
UNWIND $files AS fid
MATCH (f:File {id: fid})-[r:DEFINES|IMPORTS]->()
DELETE r
```
```cypher
-- Q_CLASS_EDGES_CLEAR   params: files
UNWIND $files AS fid
MATCH (c:Class {file: fid})-[r:HAS_METHOD|INHERITS]->()
DELETE r
```
```cypher
-- Q_DEFINES_CREATE   params: rows=[{file, sym, is_class}]
UNWIND $rows AS r
MATCH (f:File {id: r.file})
OPTIONAL MATCH (fn:Function {id: r.sym})
OPTIONAL MATCH (c:Class {id: r.sym})
WITH f, coalesce(fn, c) AS s
WHERE s IS NOT NULL
CREATE (f)-[:DEFINES]->(s)
```
```cypher
-- Q_HAS_METHOD_CREATE   params: rows=[{cls, fn}]
UNWIND $rows AS r
MATCH (c:Class {id: r.cls}), (m:Function {id: r.fn})
CREATE (c)-[:HAS_METHOD]->(m)
```
```cypher
-- Q_INHERITS_CREATE   params: rows=[{cls, base, confidence}]
UNWIND $rows AS r
MATCH (c:Class {id: r.cls}), (b:Class {id: r.base})
CREATE (c)-[:INHERITS {confidence: r.confidence}]->(b)
```
```cypher
-- Q_IMPORTS_CREATE   params: rows=[{src, dst, names, line, whole_module, alias}]
UNWIND $rows AS r
MATCH (a:File {id: r.src}), (b:File {id: r.dst})
CREATE (a)-[:IMPORTS {names: r.names, line: r.line, whole_module: r.whole_module, alias: r.alias}]->(b)
```

### Writes — calls

```cypher
-- Q_CALLS_CLEAR_FROM_FILES   params: files   (removes all outgoing call edges of every function in these files)
UNWIND $files AS fid
MATCH (s:Function {file: fid})-[r:CALLS]->()
DELETE r
```
```cypher
-- Q_CALLS_CREATE   params: rows=[{caller, callee, line, col, pos_args, kw_names, has_star, has_dstar, confidence, call_kind, call_text}]
UNWIND $rows AS r
MATCH (a:Function {id: r.caller}), (b:Function {id: r.callee})
CREATE (a)-[:CALLS {line: r.line, col: r.col, pos_args: r.pos_args, kw_names: r.kw_names, has_star: r.has_star,
                    has_dstar: r.has_dstar, confidence: r.confidence, call_kind: r.call_kind, call_text: r.call_text}]->(b)
```

### Writes — deletion / wipe

```cypher
-- Q_FILES_DELETE   params: files   (file removed from disk)
-- run Q_FUNCTIONS_DELETE_STALE and Q_CLASSES_DELETE_STALE with keep=[] first (same transaction), then:
UNWIND $files AS fid
MATCH (f:File {id: fid})
DELETE f
```
```cypher
-- Q_CODE_WIPE   (index --full)
MATCH (n) WHERE n:File OR n:Class OR n:Function DELETE n
```
```cypher
-- Q_META_TOUCH_INDEXED   params: now
MATCH (m:Meta {id:'meta'}) SET m.last_indexed_at = $now
```

**Per-batch transaction order** (`GraphWriter::sync_files`):
`Q_FILES_UPSERT → Q_FUNCTIONS_DELETE_STALE → Q_CLASSES_DELETE_STALE → Q_CLASSES_UPSERT → Q_FUNCTIONS_UPSERT → Q_STRUCT_EDGES_CLEAR → Q_CLASS_EDGES_CLEAR → Q_DEFINES_CREATE → Q_HAS_METHOD_CREATE → Q_INHERITS_CREATE → Q_IMPORTS_CREATE`
then, after resolution, a second transaction: `Q_CALLS_CLEAR_FROM_FILES(affected) → Q_CALLS_CREATE(rows for affected)` and memory relinks (§4.9 Q_RELINK_*).
`affected` = changed files ∪ files whose resolved calls changed (§05.7.4).

### Reads (all RO)

```cypher
-- Q_FILE_HASHES (RO)   → one row
MATCH (f:File) RETURN toJSON(collect({id: f.id, hash: f.hash})) AS j
```

```cypher
-- Q_READ_SYMBOL_CONTEXT (RO)   params: id, limit
MATCH (s:Function {id: $id})
RETURN toJSON({
  callers:      [(c:Function)-[e:CALLS]->(s) | {id: c.id, file: c.file, line: e.line, col: e.col, call: e.call_text, confidence: e.confidence}][0..$limit],
  caller_count: size([(c:Function)-[e:CALLS]->(s) | 1]),
  callees:      [(s)-[e:CALLS]->(t:Function) | {id: t.id, file: t.file, signature: t.signature, line: e.line}][0..$limit],
  history:      [(d:Decision)-[:MUTATED]->(s) | {patch_id: d.patch_id, status: d.status, intent: d.intent, reasoning: d.reasoning, ts: d.ts}],
  pitfalls:     [(p:Pitfall)-[:ABOUT]->(s) | {key: p.key, code: p.code, lesson: p.lesson, count: p.count, last_seen: p.last_seen}],
  memories:     [(m:Memory)-[:ABOUT]->(s) WHERE m.archived = false | {id: m.id, kind: m.kind, content: m.content}]
}) AS j
```
(C++ sorts `history` by `ts DESC` and keeps 5; same for pitfalls by `count DESC`.)

```cypher
-- Q_READ_CLASS_CONTEXT (RO)   params: id, limit
MATCH (c:Class {id: $id})
RETURN toJSON({
  methods:   [(c)-[:HAS_METHOD]->(m:Function) | {id: m.id, signature: m.signature, start_line: m.start_line}],
  bases:     [(c)-[:INHERITS]->(b:Class) | b.id],
  subclasses:[(s:Class)-[:INHERITS]->(c) | s.id],
  instantiated_by: [(x:Function)-[e:CALLS {call_kind:'constructor'}]->(:Function {class_id: $id}) | {id: x.id, file: x.file, line: e.line}][0..$limit],
  history:   [(d:Decision)-[:MUTATED]->(c) | {patch_id: d.patch_id, status: d.status, intent: d.intent, ts: d.ts}],
  pitfalls:  [(p:Pitfall)-[:ABOUT]->(c) | {key: p.key, code: p.code, lesson: p.lesson, count: p.count}]
}) AS j
```

```cypher
-- Q_CALLERS_OF_IDS (RO)   params: ids, exclude_files      ← the blast-radius query
UNWIND $ids AS tid
MATCH (c:Function)-[e:CALLS]->(t:Function {id: tid})
WHERE NOT c.file IN $exclude_files
RETURN toJSON({target: tid, caller: c.id, file: c.file, line: e.line, col: e.col, call: e.call_text,
               pos_args: e.pos_args, kw_names: e.kw_names, has_star: e.has_star, has_dstar: e.has_dstar,
               confidence: e.confidence, call_kind: e.call_kind}) AS j
```

```cypher
-- Q_IMPORTERS_OF_NAMES (RO)   params: file, names, exclude_files
MATCH (a:File)-[i:IMPORTS]->(b:File {id: $file})
WHERE NOT a.id IN $exclude_files AND any(n IN i.names WHERE n IN $names)
RETURN toJSON({file: a.id, line: i.line, names: [n IN i.names WHERE n IN $names]}) AS j
```
```cypher
-- Q_IMPORTERS_OF_FILE (RO)   params: file, exclude_files   (file deletion)
MATCH (a:File)-[i:IMPORTS]->(b:File {id: $file})
WHERE NOT a.id IN $exclude_files
RETURN toJSON({file: a.id, line: i.line, names: i.names, whole_module: i.whole_module}) AS j
```

```cypher
-- Q_IMPACT_D{1..5} (RO)   params: id, limit   — the engine substitutes the validated depth literal N∈[1,5]
-- into the query text at compile time (5 constexpr variants), because variable-length bounds cannot be parameters.
MATCH p = (c:Function)-[:CALLS*1..N]->(t:Function {id: $id})
WITH c, min(length(p)) AS depth
ORDER BY depth, c.id
LIMIT $limit
RETURN toJSON({id: c.id, file: c.file, signature: c.signature, depth: depth, criticality: c.criticality}) AS j
```
```cypher
-- Q_IMPACT_FILES_D{1..5} (RO)   params: id   — distinct files in the impact set (for summary)
MATCH (c:Function)-[:CALLS*1..N]->(t:Function {id: $id})
RETURN toJSON(collect(DISTINCT c.file)) AS j
```

```cypher
-- Q_FILE_OUTLINE (RO)   params: file
MATCH (f:File {id: $file})
RETURN toJSON({
  file: f.id, language: f.language, line_count: f.line_count, parse_ok: f.parse_ok, hash: f.hash,
  imports: [(f)-[i:IMPORTS]->(t:File) | {file: t.id, names: i.names, line: i.line}],
  imported_by: [(o:File)-[:IMPORTS]->(f) | o.id],
  classes: [(f)-[:DEFINES]->(c:Class) | {id: c.id, name: c.name, kind: c.kind, bases: c.bases, start_line: c.start_line, end_line: c.end_line,
             methods: [(c)-[:HAS_METHOD]->(m:Function) | {name: m.name, signature: m.signature, start_line: m.start_line, end_line: m.end_line}]}],
  functions: [(f)-[:DEFINES]->(fn:Function) WHERE fn.kind <> 'module' | {id: fn.id, name: fn.name, signature: fn.signature,
             start_line: fn.start_line, end_line: fn.end_line, fan_in: size([(x:Function)-[:CALLS]->(fn) | 1])}]
}) AS j
```

```cypher
-- Q_MODULE_MAP (RO)   params: prefix, limit
MATCH (f:File)
WHERE f.id STARTS WITH $prefix
WITH f
ORDER BY f.id
LIMIT $limit
RETURN toJSON({file: f.id, language: f.language, symbols: f.symbol_count,
               imports: [(f)-[:IMPORTS]->(t:File) | t.id],
               imported_by_count: size([(o:File)-[:IMPORTS]->(f) | 1])}) AS j
```
```cypher
-- Q_HOTSPOTS (RO)   params: prefix, limit   — most-called functions
MATCH (c:Function)-[:CALLS]->(t:Function)
WHERE t.file STARTS WITH $prefix
WITH t, count(c) AS fan_in
ORDER BY fan_in DESC
LIMIT $limit
RETURN toJSON({id: t.id, signature: t.signature, fan_in: fan_in}) AS j
```

```cypher
-- Q_SEARCH_FUNCTIONS (RO)   params: q, limit      (q is a sanitized RediSearch query, §4.9)
CALL db.idx.fulltext.queryNodes('Function', $q) YIELD node, score
WITH node, score WHERE node.kind <> 'module'
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({id: node.id, kind: node.kind, signature: node.signature, file: node.file,
               start_line: node.start_line, doc: node.doc, score: score}) AS j
```
```cypher
-- Q_SEARCH_CLASSES (RO)   params: q, limit
CALL db.idx.fulltext.queryNodes('Class', $q) YIELD node, score
WITH node, score
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({id: node.id, kind: node.kind, signature: node.name, file: node.file,
               start_line: node.start_line, doc: node.doc, score: score}) AS j
```

```cypher
-- Q_COUNTS (RO)   — status tool
MATCH (n)
WITH labels(n)[0] AS label, count(n) AS n
RETURN toJSON({label: label, n: n}) AS j
```

## 4.8 Memory queries (project graph unless noted)

### Sessions

```cypher
-- Q_SESSION_CREATE   params: id, client, client_version, now
CREATE (:Session {id: $id, client: $client, client_version: $client_version, status: 'ACTIVE',
                  started_at: $now, last_seen_at: $now, ended_at: 0, summary: '',
                  patch_count: 0, applied_count: 0, rejected_count: 0})
```
```cypher
-- Q_SESSION_HEARTBEAT   params: id, now     (throttled: at most every 30 s)
MATCH (s:Session {id: $id}) SET s.last_seen_at = $now
```
```cypher
-- Q_SESSIONS_MARK_INTERRUPTED   params: cutoff, now    (startup; cutoff = now - session_timeout_s*1000)
MATCH (s:Session {status: 'ACTIVE'})
WHERE s.last_seen_at < $cutoff
SET s.status = 'INTERRUPTED', s.ended_at = s.last_seen_at
RETURN toJSON(collect(s.id)) AS j
```
```cypher
-- Q_SESSION_END   params: id, summary, now
MATCH (s:Session {id: $id})
SET s.status = 'ENDED', s.summary = $summary, s.ended_at = $now
```
```cypher
-- Q_SESSION_AUTO_SUMMARY_DATA (RO)   params: id
MATCH (s:Session {id: $id})
RETURN toJSON({
  intents: [(s)-[:HAS_INTENT]->(i:Intent) | i.goal],
  decisions: [(s)-[:MADE]->(d:Decision) | {status: d.status, files: d.files}],
  patch_count: s.patch_count, applied: s.applied_count, rejected: s.rejected_count
}) AS j
```

### Intents

Intent upsert = `Q_INTENT_FIND` (RO) → `Q_INTENT_CREATE` or `Q_INTENT_TOUCH`. All run under `state_mu`, so there is no race.
```cypher
-- Q_INTENT_FIND (RO)   params: session, goal_norm
MATCH (:Session {id: $session})-[:HAS_INTENT]->(i:Intent {goal_norm: $goal_norm}) RETURN toJSON({id: i.id}) AS j
-- Q_INTENT_CREATE   params: session, id, goal, goal_norm, now
MATCH (s:Session {id: $session})
CREATE (s)-[:HAS_INTENT]->(:Intent {id: $id, goal: $goal, goal_norm: $goal_norm, status: 'OPEN', created_at: $now, updated_at: $now})
-- Q_INTENT_TOUCH   params: id, now
MATCH (i:Intent {id: $id}) SET i.updated_at = $now
```

```cypher
-- Q_INTENTS_SET_STATUS   params: ids, status, now
UNWIND $ids AS iid
MATCH (i:Intent {id: iid}) SET i.status = $status, i.updated_at = $now
```

### Decisions (one transaction with the code-graph sync on apply)

```cypher
-- Q_DECISION_RECORD   params: d={patch_id,status,actor,intent,reasoning,failed_phase,violation_codes,files,symbols,ts,duration_ms},
--                             session, intent_id, mutated=[{id, change}]
MERGE (d:Decision {patch_id: $d.patch_id})
ON CREATE SET d.status = $d.status, d.actor = $d.actor, d.intent = $d.intent, d.reasoning = $d.reasoning,
              d.failed_phase = $d.failed_phase, d.violation_codes = $d.violation_codes, d.files = $d.files,
              d.symbols = $d.symbols, d.ts = $d.ts, d.duration_ms = $d.duration_ms
WITH d
OPTIONAL MATCH (s:Session {id: $session})
FOREACH (_ IN CASE WHEN s IS NULL THEN [] ELSE [1] END |
  MERGE (s)-[:MADE]->(d)
  SET s.patch_count = s.patch_count + 1,
      s.applied_count = s.applied_count + CASE WHEN $d.status = 'APPLIED' THEN 1 ELSE 0 END,
      s.rejected_count  = s.rejected_count  + CASE WHEN $d.status = 'REJECTED'  THEN 1 ELSE 0 END)
WITH d
OPTIONAL MATCH (i:Intent {id: $intent_id})
FOREACH (_ IN CASE WHEN i IS NULL THEN [] ELSE [1] END | MERGE (i)-[:RESOLVED_BY]->(d))
WITH d
UNWIND $d.files AS fid
MATCH (f:File {id: fid})
MERGE (d)-[:TOUCHED]->(f)
```
*If map-param property access `$d.patch_id` is not supported on 6.0.2 (verify day 1), flatten to `$patch_id`, `$status`, … — keep the same query shape.*
Session counters: increment happens inside `FOREACH`; the `MERGE (s)-[:MADE]->(d)` makes a replay (WAL roll-forward) not double-count only if guarded — therefore the engine MUST skip `Q_DECISION_RECORD` when replaying if `Q_DECISION_EXISTS` returns true:
```cypher
-- Q_DECISION_EXISTS (RO)   params: patch_id
MATCH (d:Decision {patch_id: $patch_id}) RETURN toJSON(true) AS j
```
```cypher
-- Q_DECISION_LINK_MUTATED   params: patch_id, rows=[{id, change}]   (run AFTER code sync so new symbols exist)
MATCH (d:Decision {patch_id: $patch_id})
UNWIND $rows AS r
OPTIONAL MATCH (fn:Function {id: r.id})
OPTIONAL MATCH (c:Class {id: r.id})
WITH d, r, coalesce(fn, c) AS s
WHERE s IS NOT NULL
MERGE (d)-[m:MUTATED]->(s)
SET m.change = r.change
```

### Pitfalls

```cypher
-- Q_PITFALLS_UPSERT   params: patch_id, rows=[{key, code, rule_id, symbol_id, file, lesson, message}], now
MATCH (d:Decision {patch_id: $patch_id})
UNWIND $rows AS r
MERGE (p:Pitfall {key: r.key})
ON CREATE SET p.code = r.code, p.rule_id = r.rule_id, p.symbol_id = r.symbol_id, p.file = r.file,
              p.lesson = r.lesson, p.count = 0, p.resolved_count = 0, p.first_seen = $now, p.last_resolved = 0
SET p.count = p.count + 1, p.last_seen = $now, p.message = r.message,
    p.lesson = CASE WHEN size(r.lesson) > size(p.lesson) THEN r.lesson ELSE p.lesson END
MERGE (d)-[:TRIGGERED]->(p)
WITH p, r
OPTIONAL MATCH (fn:Function {id: r.symbol_id})
OPTIONAL MATCH (c:Class {id: r.symbol_id})
OPTIONAL MATCH (f:File {id: r.file})
WITH p, coalesce(fn, c) AS s, f
FOREACH (_ IN CASE WHEN s IS NULL THEN [] ELSE [1] END | MERGE (p)-[:ABOUT]->(s))
FOREACH (_ IN CASE WHEN f IS NULL THEN [] ELSE [1] END | MERGE (p)-[:IN_FILE]->(f))
```
```cypher
-- Q_PITFALLS_RESOLVE   params: symbol_ids, now     (on APPLIED)
UNWIND $symbol_ids AS sid
MATCH (p:Pitfall {symbol_id: sid})
SET p.resolved_count = p.resolved_count + 1, p.last_resolved = $now
```
```cypher
-- Q_LESSONS_FOR (RO)   params: symbol_ids, files, limit
MATCH (p:Pitfall)
WHERE p.symbol_id IN $symbol_ids OR p.file IN $files
WITH p
ORDER BY p.count DESC, p.last_seen DESC
LIMIT $limit
RETURN toJSON({key: p.key, code: p.code, rule_id: p.rule_id, symbol_id: p.symbol_id, file: p.file,
               lesson: p.lesson, count: p.count, last_seen: p.last_seen, resolved_count: p.resolved_count}) AS j
```
```cypher
-- Q_PITFALLS_LIST (RO)   params: symbol (""=any), file (""=any), code (""=any), limit
MATCH (p:Pitfall)
WHERE ($symbol = '' OR p.symbol_id = $symbol) AND ($file = '' OR p.file = $file) AND ($code = '' OR p.code = $code)
WITH p
ORDER BY p.count DESC, p.last_seen DESC
LIMIT $limit
RETURN toJSON({key: p.key, code: p.code, rule_id: p.rule_id, symbol_id: p.symbol_id, file: p.file, lesson: p.lesson,
               count: p.count, resolved_count: p.resolved_count, first_seen: p.first_seen, last_seen: p.last_seen,
               last_resolved: p.last_resolved}) AS j
```

### Memories (same queries run against project or user graph)

```cypher
-- Q_MEMORY_FIND_DUP (RO)   params: kind, content_norm
MATCH (m:Memory {kind: $kind, content_norm: $content_norm}) WHERE m.archived = false
RETURN toJSON({id: m.id}) AS j
```
```cypher
-- Q_MEMORY_CREATE   params: m={id,kind,scope,content,content_norm,tags,anchors,importance,source_session}, now
CREATE (n:Memory {id: $m.id, kind: $m.kind, scope: $m.scope, content: $m.content, content_norm: $m.content_norm,
                  tags: $m.tags, anchors: $m.anchors, importance: $m.importance, archived: false, superseded_by: '',
                  source_session: $m.source_session, created_at: $now, updated_at: $now})
```
```cypher
-- Q_MEMORY_TOUCH   params: id, importance, tags, now
MATCH (m:Memory {id: $id}) SET m.updated_at = $now, m.importance = $importance, m.tags = $tags
```
```cypher
-- Q_MEMORY_SUPERSEDE   params: old_id, new_id, now
MATCH (m:Memory {id: $old_id}) SET m.archived = true, m.superseded_by = $new_id, m.updated_at = $now
```
```cypher
-- Q_MEMORY_LINK (project graph only)   params: id, session, anchors
MATCH (m:Memory {id: $id})
OPTIONAL MATCH (s:Session {id: $session})
FOREACH (_ IN CASE WHEN s IS NULL THEN [] ELSE [1] END | MERGE (s)-[:NOTED]->(m))
WITH m
UNWIND $anchors AS aid
OPTIONAL MATCH (fn:Function {id: aid})
OPTIONAL MATCH (c:Class {id: aid})
OPTIONAL MATCH (f:File {id: aid})
WITH m, coalesce(fn, c, f) AS t
WHERE t IS NOT NULL
MERGE (m)-[:ABOUT]->(t)
```
```cypher
-- Q_MEMORY_LIST (RO)   params: kinds (list), limit
MATCH (m:Memory)
WHERE m.archived = false AND m.kind IN $kinds
WITH m
ORDER BY m.importance DESC, m.updated_at DESC
LIMIT $limit
RETURN toJSON({id: m.id, kind: m.kind, scope: m.scope, content: m.content, tags: m.tags, anchors: m.anchors,
               importance: m.importance, updated_at: m.updated_at}) AS j
```
```cypher
-- Q_MEMORY_DELETE (CLI forget)   params: id
MATCH (m:Memory {id: $id}) DELETE m
```

### Changesets (§06.14)

```cypher
-- Q_CHANGESET_CREATE   params: id, intent, session, now
MATCH (s:Session {id: $session})
CREATE (s)-[:WORKED_ON]->(:Changeset {id: $id, intent: $intent, status: 'OPEN', files: [], open_obligations: 0,
                                      created_at: $now, updated_at: $now, closed_at: 0})
```
```cypher
-- Q_CHANGESET_TOUCH   params: id, files, open_obligations, session, now
MATCH (c:Changeset {id: $id})
SET c.files = $files, c.open_obligations = $open_obligations, c.updated_at = $now
WITH c
MATCH (s:Session {id: $session})
MERGE (s)-[:WORKED_ON]->(c)
```
```cypher
-- Q_CHANGESET_SET_STATUS   params: id, status, now
MATCH (c:Changeset {id: $id}) SET c.status = $status, c.updated_at = $now, c.closed_at = $now
```
```cypher
-- Q_CHANGESET_LINK_DECISION   params: id, patch_id
MATCH (c:Changeset {id: $id}), (d:Decision {patch_id: $patch_id})
MERGE (c)-[:INCLUDES]->(d)
```
```cypher
-- Q_OBLIGATIONS_CLEAR   params: id
MATCH (:Changeset {id: $id})-[:HAS_OBLIGATION]->(o:Obligation) DELETE o
```
```cypher
-- Q_OBLIGATIONS_CREATE   params: id, rows=[{oid, code, message, fix_hint, file, line, col, symbol_id}], now
MATCH (c:Changeset {id: $id})
UNWIND $rows AS r
CREATE (c)-[:HAS_OBLIGATION]->(:Obligation {id: r.oid, code: r.code, message: r.message, fix_hint: r.fix_hint,
                                            file: r.file, line: r.line, col: r.col, symbol_id: r.symbol_id, created_at: $now})
```
`Q_OBLIGATIONS_CLEAR` + `Q_OBLIGATIONS_CREATE` + `Q_CHANGESET_TOUCH` run in one MULTI/EXEC (= `Q_OBLIGATIONS_REPLACE` in prose).
```cypher
-- Q_OPEN_CHANGESET (RO)   — at most one row
MATCH (c:Changeset {status: 'OPEN'})
RETURN toJSON({id: c.id, intent: c.intent, files: c.files, open_obligations: c.open_obligations,
               created_at: c.created_at, updated_at: c.updated_at,
               sessions: [(s:Session)-[:WORKED_ON]->(c) | s.id],
               obligations: [(c)-[:HAS_OBLIGATION]->(o:Obligation) | {id: o.id, code: o.code, message: o.message, file: o.file,
                             line: o.line, col: o.col, symbol_id: o.symbol_id, fix_hint: o.fix_hint}]}) AS j
```

### Relink after re-index (memory edges to code nodes that were deleted and recreated)

```cypher
-- Q_RELINK_PITFALLS   params: ids   (symbol ids added in this sync)
UNWIND $ids AS sid
MATCH (p:Pitfall {symbol_id: sid})
OPTIONAL MATCH (fn:Function {id: sid})
OPTIONAL MATCH (c:Class {id: sid})
WITH p, coalesce(fn, c) AS s
WHERE s IS NOT NULL
MERGE (p)-[:ABOUT]->(s)
```
```cypher
-- Q_RELINK_MEMORIES   params: ids
UNWIND $ids AS sid
MATCH (m:Memory)
WHERE sid IN m.anchors
OPTIONAL MATCH (fn:Function {id: sid})
OPTIONAL MATCH (c:Class {id: sid})
OPTIONAL MATCH (f:File {id: sid})
WITH m, coalesce(fn, c, f) AS t
WHERE t IS NOT NULL
MERGE (m)-[:ABOUT]->(t)
```
`// ponytail: Q_RELINK_MEMORIES scans all Memory nodes per id; fine below ~10k memories, add an index on an anchor join node if it grows.`

### Graph analytics (FalkorDB algorithms, §05.14)

```cypher
-- Q_ALGO_PAGERANK_FUNCTIONS (RO)   — scores use INCOMING edges: functions called by important functions rank high
CALL algo.pageRank('Function', 'CALLS') YIELD node, score
RETURN toJSON({id: node.id, score: score}) AS j
```
```cypher
-- Q_SET_CRITICALITY   params: rows=[{id, pagerank, pct, tier}]   (only rows whose tier or pct changed by ≥ 1)
UNWIND $rows AS r
MATCH (f:Function {id: r.id})
SET f.pagerank = r.pagerank, f.pr_pct = r.pct, f.criticality = r.tier
```
```cypher
-- Q_ALGO_BRIDGE_FILES (RO)   params: limit   — modules that sit on many import paths (architectural chokepoints)
CALL algo.betweenness({nodeLabels: ['File'], relationshipTypes: ['IMPORTS'], samplingSize: 64, samplingSeed: 42})
YIELD node, score
WITH node, score
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({file: node.id, score: score}) AS j
```
```cypher
-- Q_ALGO_FILE_COMPONENTS (RO)   — import-graph islands (direction ignored)
CALL algo.WCC({nodeLabels: ['File'], relationshipTypes: ['IMPORTS']}) YIELD node, componentId
WITH componentId, collect(node.id) AS files
RETURN toJSON({component: componentId, files: files}) AS j
```
```cypher
-- Q_CRITICAL_SYMBOLS (RO)   params: limit
MATCH (f:Function)
WHERE f.criticality = 'high'
WITH f
ORDER BY f.pagerank DESC
LIMIT $limit
RETURN toJSON({id: f.id, signature: f.signature, pr_pct: f.pr_pct, fan_in: size([(x:Function)-[:CALLS]->(f) | 1])}) AS j
```
*Day-1 verification V16:* the three `algo.*` procedures exist on 6.0.2 (the docs describe them; v6 is a rewrite). Fallback: criticality from fan-in percentile computed in C++ (same tiers), bridges/islands omitted.

### Briefing reads (RO)

```cypher
-- Q_RECENT_SESSIONS (RO)   params: exclude, limit
MATCH (s:Session)
WHERE s.id <> $exclude AND s.status IN ['ENDED', 'INTERRUPTED']
WITH s
ORDER BY s.started_at DESC
LIMIT $limit
RETURN toJSON({id: s.id, client: s.client, status: s.status, started_at: s.started_at, ended_at: s.ended_at,
               summary: s.summary, applied: s.applied_count, rejected: s.rejected_count,
               intents: [(s)-[:HAS_INTENT]->(i:Intent) | {id: i.id, goal: i.goal, status: i.status}]}) AS j
```
```cypher
-- Q_OPEN_INTENTS (RO)   params: exclude_session, limit
MATCH (s:Session)-[:HAS_INTENT]->(i:Intent {status: 'OPEN'})
WHERE s.id <> $exclude_session
WITH s, i
ORDER BY i.updated_at DESC
LIMIT $limit
RETURN toJSON({id: i.id, goal: i.goal, session: s.id, session_status: s.status, updated_at: i.updated_at,
               last_decision: [(i)-[:RESOLVED_BY]->(d:Decision) | {status: d.status, ts: d.ts, files: d.files}]}) AS j
```
(C++ keeps only the newest `last_decision` entry.)
```cypher
-- Q_TOP_PITFALLS (RO)   params: limit
MATCH (p:Pitfall)
WITH p
ORDER BY p.count DESC, p.last_seen DESC
LIMIT $limit
RETURN toJSON({key: p.key, code: p.code, symbol_id: p.symbol_id, file: p.file, lesson: p.lesson, count: p.count,
               unresolved: p.last_resolved < p.last_seen}) AS j
```
```cypher
-- Q_HOT_SYMBOLS (RO)   params: since, limit
MATCH (d:Decision {status: 'APPLIED'})-[:MUTATED]->(s)
WHERE d.ts >= $since
WITH s, count(d) AS n, max(d.ts) AS last
ORDER BY n DESC, last DESC
LIMIT $limit
RETURN toJSON({id: s.id, changes: n, last: last}) AS j
```
```cypher
-- Q_UNGATED_SINCE (RO)   params: since
MATCH (d:Decision {actor: 'external'})
WHERE d.ts >= $since
WITH d
ORDER BY d.ts DESC
LIMIT 10
RETURN toJSON({patch_id: d.patch_id, files: d.files, violation_codes: d.violation_codes, ts: d.ts}) AS j
```

### Recall (full-text) (RO)

```cypher
-- Q_RECALL_MEMORY (RO, run on project graph AND user graph)   params: q, limit
CALL db.idx.fulltext.queryNodes('Memory', $q) YIELD node, score
WITH node, score WHERE node.archived = false
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({type: 'memory', id: node.id, kind: node.kind, scope: node.scope, content: node.content,
               anchors: node.anchors, updated_at: node.updated_at, score: score}) AS j
```
```cypher
-- Q_RECALL_DECISIONS (RO)   params: q, limit
CALL db.idx.fulltext.queryNodes('Decision', $q) YIELD node, score
WITH node, score
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({type: 'decision', id: node.patch_id, status: node.status, intent: node.intent, reasoning: node.reasoning,
               files: node.files, symbols: node.symbols, ts: node.ts, score: score}) AS j
```
```cypher
-- Q_RECALL_PITFALLS (RO)   params: q, limit
CALL db.idx.fulltext.queryNodes('Pitfall', $q) YIELD node, score
WITH node, score
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({type: 'pitfall', id: node.key, code: node.code, lesson: node.lesson, symbol_id: node.symbol_id,
               count: node.count, score: score}) AS j
```
```cypher
-- Q_RECALL_SESSIONS (RO)   params: q, limit
CALL db.idx.fulltext.queryNodes('Session', $q) YIELD node, score
WITH node, score
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({type: 'session', id: node.id, summary: node.summary, status: node.status, started_at: node.started_at, score: score}) AS j
```
```cypher
-- Q_RECALL_INTENTS (RO)   params: q, limit
CALL db.idx.fulltext.queryNodes('Intent', $q) YIELD node, score
WITH node, score
ORDER BY score DESC
LIMIT $limit
RETURN toJSON({type: 'intent', id: node.id, goal: node.goal, status: node.status, updated_at: node.updated_at, score: score}) AS j
```
```cypher
-- Q_RECALL_BY_ANCHOR (RO)   params: id, limit    (symbol/file-anchored recall, no full-text)
MATCH (m:Memory) WHERE $id IN m.anchors AND m.archived = false
RETURN toJSON({type: 'memory', id: m.id, kind: m.kind, scope: m.scope, content: m.content, updated_at: m.updated_at, score: 100.0}) AS j
LIMIT $limit
```

### User graph

```cypher
-- Q_USER_PROFILE_UPSERT (user graph)   params: workspace, now
MERGE (u:UserProfile {id: 'local'})
ON CREATE SET u.created_at = $now, u.workspaces = []
SET u.workspaces = CASE WHEN $workspace IN u.workspaces THEN u.workspaces ELSE u.workspaces + [$workspace] END
```

## 4.9 Search query sanitization (`db::fulltext_query(std::string_view user_text)`)

FalkorDB full-text uses RediSearch syntax. Build queries from user text deterministically:
1. Split user text into tokens on any char not in `[A-Za-z0-9_]`; additionally split camelCase and snake_case (`calculateTotal` → `calculate`, `total`; `calc_total` → `calc`, `total`) and keep the original token too.
2. Lowercase; drop tokens of length < 2 and stopwords (`the a an and or of to in for on with is are be this that it`).
3. Each token `t` → `t*` if len ≥ 3 (prefix match), else `t`. Deduplicate. Max 8 tokens.
4. Join with ` | ` (OR). Empty → return no results without querying.
5. Never pass raw user text (RediSearch special chars `@ - | ( ) { } [ ] " ' ~ * % :` would break the query).

The same splitter produces `search_text` at index time (`"calculate total calculate_total"`) so prefix queries hit snake/camel parts.

## 4.10 "The graph does real work" — dependency map (for README & judges)

| Feature | Without FalkorDB |
|---|---|
| Blast radius (`Q_CALLERS_OF_IDS`, `Q_IMPORTERS_OF_NAMES`) | gate cannot apply patches (refuses: `DB_UNAVAILABLE`) |
| `read_symbol` callers/callees/history/pitfalls | unavailable |
| `get_impact` (variable-length `CALLS*1..N`) | unavailable |
| Criticality (PageRank), bridge modules (betweenness), islands (WCC) | unavailable — computed by FalkorDB algorithms |
| Memory, pitfalls, sessions, briefing, recall | unavailable (only stored in FalkorDB) |
| Restart continuity, open changesets / handoffs | lost |
