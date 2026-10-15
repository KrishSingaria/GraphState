# 08 — MCP Protocol & Tool Contracts

> Index: [README.md](README.md). Code: `src/mcp/*`. Verified 2026-10-14 against the MCP spec repo (latest `2026-07-28`) and TypeScript SDK v1.32.1 (still defaults to `2025-11-25`).

## 8.1 Transport (`mcp/transport.cpp`)

- stdio, **newline-delimited JSON-RPC 2.0**. One message per line; messages MUST NOT contain raw newlines (`json.dump()` without indent guarantees this).
- stdout carries **only** MCP messages. All logs → stderr + log file.
- `platform::init_stdio()` first (Windows: binary mode on stdin/stdout so no `\n`→`\r\n` translation; UTF-8 console code page).
- Read loop: `std::getline(std::cin, line)` with `std::ios::sync_with_stdio(false)`; strip one trailing `\r` (Windows clients may send CRLF); skip empty lines; lines > 16 MiB → JSON-RPC error `-32600` (id null) and skip.
- Write: `out_mu` lock → `std::cout << line << '\n' << std::flush`.
- EOF on stdin → graceful shutdown (§02.4).
- JSON-RPC **batching is not supported** by MCP (removed in 2025-06-18): a top-level JSON array → `-32600 Invalid Request`.

## 8.2 Protocol eras — dual-era server (MUST)

MCP `2026-07-28` removed the `initialize` handshake (stateless; per-request `_meta`), but most hosts still speak `2025-11-25`/`2025-06-18`. GraphState supports both:

| | Legacy era | Modern era |
|---|---|---|
| Versions | `2025-11-25` (preferred), `2025-06-18`, `2025-03-26`, `2024-11-05` | `2026-07-28` |
| Detected by | an `initialize` request | any request whose `params._meta["io.modelcontextprotocol/protocolVersion"]` exists |
| Handshake | `initialize` → result; then `notifications/initialized` | none; optional `server/discover` |
| Extra result fields | none | every result: `"resultType":"complete"`; list/read results: `"ttlMs"`, `"cacheScope"` |
| `ping` | supported → `{}` | removed (still answered if sent) |
| Logging | not declared (stderr only) | not used |

### 8.2.1 Legacy `initialize`

Request:
```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"claude-code","version":"2.3.0"}}}
```
Response (version negotiation: echo the client's version if it is in the legacy list, else answer `2025-11-25`):
```json
{"jsonrpc":"2.0","id":1,"result":{
  "protocolVersion":"2025-11-25",
  "capabilities":{"tools":{"listChanged":false},"resources":{"subscribe":false,"listChanged":false},"prompts":{"listChanged":false}},
  "serverInfo":{"name":"graphstate","title":"GraphState","version":"0.1.0"},
  "instructions":"<§8.4 text>"}}
```
Side effect: `Sessions::ensure(clientInfo)`.
`notifications/initialized` → no response.

### 8.2.2 Modern `server/discover`

Request:
```json
{"jsonrpc":"2.0","id":"d1","method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{},"io.modelcontextprotocol/clientInfo":{"name":"antigravity","version":"1.4"}}}}
```
Response:
```json
{"jsonrpc":"2.0","id":"d1","result":{
  "resultType":"complete",
  "supportedVersions":["2026-07-28","2025-11-25","2025-06-18","2025-03-26","2024-11-05"],
  "capabilities":{"tools":{},"resources":{},"prompts":{}},
  "_meta":{"io.modelcontextprotocol/serverInfo":{"name":"graphstate","title":"GraphState","version":"0.1.0"}},
  "instructions":"<§8.4 text>",
  "ttlMs":3600000,"cacheScope":"private"}}
```
Every modern request: validate `_meta` version ∈ `{"2026-07-28"}`; otherwise:
```json
{"jsonrpc":"2.0","id":7,"error":{"code":-32022,"message":"Unsupported protocol version","data":{"supported":["2026-07-28","2025-11-25","2025-06-18","2025-03-26","2024-11-05"],"requested":"1900-01-01"}}}
```
Modern session creation: first modern request → `Sessions::ensure(_meta clientInfo)`.

### 8.2.3 Dispatcher (`mcp/server.cpp`)

```cpp
namespace gs::mcp {
enum class Era { Unknown, Legacy, Modern };
class Server {
 public:
  Server(Engine&, Transport&);
  void run();                                   // loop until EOF
 private:
  std::optional<nlohmann::json> handle(const nlohmann::json& msg);  // nullopt for notifications
  nlohmann::json on_initialize(const nlohmann::json& params);
  nlohmann::json on_discover(const nlohmann::json& params);
  nlohmann::json on_tools_list(), on_tools_call(const nlohmann::json& params);
  nlohmann::json on_resources_list(), on_resources_read(const nlohmann::json& params);
  nlohmann::json on_prompts_list(), on_prompts_get(const nlohmann::json& params);
  nlohmann::json decorate(nlohmann::json result, bool is_list_or_read) const; // modern fields
  Era era_ = Era::Unknown;
};
}
```

| Method | Handler | Notes |
|---|---|---|
| `initialize` | legacy handshake | sets `era_ = Legacy` |
| `notifications/initialized`, `notifications/cancelled`, any `notifications/*` | ignore | never respond to messages without `id` |
| `server/discover` | modern discover | |
| `ping` | `{}` | |
| `tools/list` | §8.5 | no pagination (14 tools); deterministic order as listed |
| `tools/call` | §8.6 | |
| `resources/list`, `resources/read` | §8.7 | |
| `prompts/list`, `prompts/get` | §8.8 | |
| `logging/setLevel` | `{}` (accepted, ignored) | |
| other | `-32601 Method not found` | |

JSON-RPC errors:

| Code | When |
|---|---|
| `-32700` | line is not valid JSON (id null) |
| `-32600` | not an object / missing `jsonrpc:"2.0"` / batch / too large |
| `-32601` | unknown method |
| `-32602` | `tools/call` unknown tool name (`"Unknown tool: x"`), resource not found (modern), prompt not found, invalid params **shape** (e.g. `arguments` not an object) |
| `-32002` | resource not found (legacy era) |
| `-32603` | unexpected internal failure outside a tool handler |
| `-32022` | unsupported modern protocol version |

**Tool-level failures** (bad argument values, DB down, symbol not found, rejected patch is NOT a failure — see below) return a normal result with `isError: true` so the model can self-correct.

Cancellation: requests are processed synchronously and are all ms-scale except `reindex{full:true}`; `notifications/cancelled` is ignored. `// ponytail: no cancellation; add a cancel flag checked between index batches if full reindex on huge repos needs it.`

## 8.3 Tool result envelope

Success:
```json
{"content":[{"type":"text","text":"<rendered>"}],"structuredContent":{ ... },"isError":false}
```
- `rendered` = `structuredContent.dump()` (compact JSON) for all tools **except** `get_briefing` (markdown, §07.8) and `propose_patch` (a short human line followed by the compact JSON, §8.6.7).
- No `outputSchema` is declared in `tools/list` (the shapes below are the contract; keeps `tools/list` small and avoids strict-validation failures in hosts).

Tool error (`isError: true`):
```json
{"content":[{"type":"text","text":"ERROR SYMBOL_NOT_FOUND: no symbol 'calc_total' in billing.py. Did you mean: calculate_total?"}],
 "structuredContent":{"error":{"code":"SYMBOL_NOT_FOUND","message":"no symbol 'calc_total' in billing.py","data":{"did_you_mean":["billing.py::calculate_total"]}}},
 "isError":true}
```
Tool error codes: `INVALID_ARGUMENT`, `CHANGESET_OPEN`, `NO_OPEN_CHANGESET`, `SYMBOL_NOT_FOUND`, `SYMBOL_AMBIGUOUS`, `FILE_NOT_FOUND`, `INVALID_ANCHOR`, `EMPTY_QUERY`, `SECRET_DETECTED`, `DB_UNAVAILABLE`, `DB_QUERY`, `INTERNAL`.

**Notices**: if the watcher queued notices (ungated writes with findings, rules reloaded, DB reconnected), they're attached to the next tool result as `structuredContent._notices: [{kind, message, data}]` and appended to `text` as `\n\nNOTICES:\n- …`. Notices are delivered once.

**Argument validation** (`mcp/schema_validate.cpp`, add to tree): a hand-written validator for the JSON-Schema subset used below — `type` (object/string/integer/boolean/array), `required`, `properties`, `additionalProperties:false`, `enum`, `minimum`/`maximum`, `minLength`/`maxLength`, `items`, `minItems`/`maxItems`. Violations → `isError` `INVALID_ARGUMENT` with a JSON-pointer path (`/edits/2/op: must be one of …`).

## 8.4 Server `instructions` (≤ 4 096 chars — Claude Code truncates beyond that)

`src/mcp/instructions.inc`:
```
GraphState is the code gateway for this repository. It indexes the code into a FalkorDB graph, verifies every change before it reaches disk, and remembers past sessions.

RULES
1. Start every session with get_briefing (pass your goal). It returns user preferences, project conventions, unfinished work and known pitfalls. Follow them.
2. Write code ONLY through propose_patch. Direct file edits are disabled in this repo; edits made outside the gate are detected and reported. APPLIED means written to the working tree (GraphState never runs git).
3. Before changing a function, call read_symbol on it: you get its exact source, callers, callees, history and pitfalls. Use get_file_outline instead of reading whole files.
4. Before changing a signature or deleting a symbol, call get_impact. Put the definition change AND every caller update in ONE propose_patch (edits may span files; they are written atomically).
5. If propose_patch returns REJECTED, read every violation: each has file:line, evidence and a fix_hint. Fix all of them and resubmit. Do not retry the same patch unchanged.
6. Only call helpers that exist. If unsure, search_symbols or get_file_outline first. Calling a non-existent workspace function is rejected (MISSING_SYMBOL).
7. Use dry_run: true to validate a risky patch without writing.
8. Building something new over several patches (files that reference code you have not written yet), or a refactor spread over many steps? Call changeset {action:"begin", intent} first. Cross-file problems then become obligations instead of rejections; clear them all, then changeset {action:"finalize"}. If an open changeset is listed in the briefing, continue it. changeset {action:"abandon"} restores every file it touched.
9. When you learn a durable user preference or project convention, save it with remember. Do not store secrets.
10. Before finishing, call end_session with a 1-3 sentence summary of what was done and what is left.

PATCH TIPS
- replace_symbol: send the full new definition (decorators/export included), as returned by read_symbol. Indentation is fixed automatically.
- insert_symbol: anchor with after/before/into_class, or omit for end of file.
- replace_text: for imports, constants and non-code files; old_text must match exactly once.
- create_file / delete_file / move_file for whole files. One patch may create many files that reference each other.
```

## 8.5 `tools/list` — names, titles, annotations

Order is fixed. `inputSchema` per tool in §8.6. `annotations`:

| # | name | title | readOnlyHint | destructiveHint | idempotentHint | openWorldHint |
|---|---|---|---|---|---|---|
| 1 | `get_briefing` | Session briefing | false* | false | true | false |
| 2 | `search_symbols` | Search symbols | true | false | true | false |
| 3 | `get_file_outline` | File outline | true | false | true | false |
| 4 | `read_symbol` | Read symbol with context | true | false | true | false |
| 5 | `get_impact` | Impact analysis | true | false | true | false |
| 6 | `get_module_map` | Module map | true | false | true | false |
| 7 | `propose_patch` | Propose a gated patch | false | true | false | false |
| 8 | `remember` | Remember | false | false | true | false |
| 9 | `recall` | Recall memory | true | false | true | false |
| 10 | `recall_pitfalls` | Known pitfalls | true | false | true | false |
| 11 | `end_session` | End session | false | false | true | false |
| 12 | `reindex` | Re-index | false | false | true | false |
| 13 | `status` | Status | true | false | true | false |
| 14 | `changeset` | Changeset (incremental work) | false | true | false | false |

\* `get_briefing` writes an Intent when `goal` is given.

## 8.6 Tool contracts

Common conventions:
- **Symbol refs** (`symbol` args) accept: full id `billing.py::Invoice.total`, qualname `Invoice.total`, or bare name `total`. With optional `file` to disambiguate. Resolution via FactStore: exact id → qualname (+file) → name (+file). Multiple → `SYMBOL_AMBIGUOUS` (data.candidates: up to 10 `{id, signature}`), none → `SYMBOL_NOT_FOUND` (data.did_you_mean: up to 5 by edit distance).
- **File args** are workspace-relative with `/`.
- Line/col are 1-based.

### 8.6.1 `get_briefing`

Description (exact text shipped in tools/list): *"Call FIRST in every session. Returns user preferences, project conventions, unfinished work from earlier sessions, known pitfalls, recent changes and enforced rules. Pass your goal to record it and get related memories."*

```json
{"type":"object","additionalProperties":false,
 "properties":{"goal":{"type":"string","minLength":1,"maxLength":500,"description":"What you are about to do, one sentence."}}}
```
structuredContent:
```json
{"session_id":"s_9f1c2a7b3d4e","intent_id":"i_0a1b2c3d4e5f",
 "workspace":{"root":"/Users/me/benchmark_project","graph":"gs:benchmark_project-3f9a1c2e","files":41,"functions":212,"classes":37,
              "languages":{"python":33,"typescript":8},"last_indexed_at":1760500000000},
 "user_memories":[{"id":"m_1a2b3c4d5e6f","kind":"preference","content":"Prefers type hints on all public functions.","importance":3}],
 "project_memories":[{"id":"m_77de00112233","kind":"convention","content":"Money is Decimal, never float.","importance":3,"anchors":["billing.py"]}],
 "open_intents":[{"id":"i_5566778899aa","goal":"Add currency to totals","session_status":"INTERRUPTED","last_decision":{"status":"REJECTED","files":["billing.py"]}}],
 "recent_sessions":[{"id":"s_11aa22bb33cc","client":"claude-code","status":"ENDED","summary":"Added CSV export…","applied":4,"rejected":1,"started_at":1760400000000}],
 "top_pitfalls":[{"key":"9c0e…","code":"CALLER_INCOMPATIBLE","symbol_id":"billing.py::calculate_total","lesson":"Changing the signature of …","count":3,"unresolved":true}],
 "hot_symbols":[{"id":"billing.py::calculate_total","changes":5}],
 "critical_symbols":[{"id":"utils.py::round_money","pr_pct":98.1,"fan_in":7}],
 "ungated_changes":[{"patch_id":"x_77aa…","files":["utils.py"],"violation_codes":["CALLER_INCOMPATIBLE"]}],
 "open_changeset":{"id":"c_4e5f6a7b8c9d","intent":"Build order API","files":["api/routes.py"],"open_obligations":1,"sessions":["s_11aa22bb33cc"]},
 "related":[{"type":"decision","id":"p_…","text":"…","score":0.82}],
 "rules":[{"id":"py-mutable-default-arg","severity":"error"}]}
```

### 8.6.2 `search_symbols`

Description: *"Find functions/classes by name or words (fuzzy, prefix). Use before calling or editing code you haven't seen."*
```json
{"type":"object","additionalProperties":false,"required":["query"],
 "properties":{"query":{"type":"string","minLength":1,"maxLength":200},
               "kind":{"type":"string","enum":["any","function","class"],"default":"any"},
               "path_prefix":{"type":"string","maxLength":500},
               "limit":{"type":"integer","minimum":1,"maximum":50,"default":10}}}
```
Algorithm: exact FactStore hits (id/qualname/name equal, case-insensitive) first with score 100; then `Q_SEARCH_FUNCTIONS`/`Q_SEARCH_CLASSES` (pipelined) with `fulltext_query(query)`; filter by `path_prefix`; dedupe; sort by score; limit.
```json
{"results":[{"id":"billing.py::calculate_total","kind":"function","signature":"calculate_total(cart: Cart, tax_rate: float = …) -> Decimal","file":"billing.py","start_line":12,"doc":"Total including tax.","score":100}],
 "total":1}
```

### 8.6.3 `get_file_outline`

Description: *"Compact outline of a file: imports, classes with methods, functions with signatures and line ranges, fan-in. Cheaper than reading the file."*
```json
{"type":"object","additionalProperties":false,"required":["file"],
 "properties":{"file":{"type":"string","minLength":1,"maxLength":500}}}
```
`ensure_fresh(file)` then `Q_FILE_OUTLINE`. For syntax-only/unknown files: `{file, language, line_count, parse_ok, semantic:false}`.
```json
{"file":"billing.py","language":"python","line_count":88,"parse_ok":true,"hash":"8f2c6d1e0a9b7c3d",
 "imports":[{"file":"models.py","names":["Cart","LineItem"],"line":3}],
 "imported_by":["app.py","checkout.py","reports.py"],
 "classes":[{"id":"billing.py::Invoice","name":"Invoice","kind":"class","bases":[],"start_line":40,"end_line":88,
             "methods":[{"name":"__init__","signature":"__init__(self, cart: Cart)","start_line":41,"end_line":44},
                        {"name":"total","signature":"total(self) -> Decimal","start_line":46,"end_line":49}]}],
 "functions":[{"id":"billing.py::calculate_total","name":"calculate_total","signature":"calculate_total(cart: Cart, tax_rate: float = …) -> Decimal","start_line":12,"end_line":20,"fan_in":4}]}
```

### 8.6.4 `read_symbol`

Description: *"Exact source of one function/class plus its callers, callees, change history, pitfalls and attached memories. Use before editing it; the returned source is what replace_symbol expects."*
```json
{"type":"object","additionalProperties":false,"required":["symbol"],
 "properties":{"symbol":{"type":"string","minLength":1,"maxLength":500},
               "file":{"type":"string","maxLength":500},
               "include":{"type":"array","items":{"type":"string","enum":["source","callers","callees","history","pitfalls","memories"]},
                          "default":["source","callers","callees","history","pitfalls","memories"]},
               "max_callers":{"type":"integer","minimum":0,"maximum":200,"default":20}}}
```
Algorithm: resolve ref → `ensure_fresh(file)` → `Q_READ_SYMBOL_CONTEXT` (functions) or `Q_READ_CLASS_CONTEXT` (classes) → slice source from disk `[def_range.start, def_range.end)`; verify on-disk hash == FactStore hash (else re-index and retry once).
```json
{"id":"billing.py::calculate_total","kind":"function","file":"billing.py","start_line":12,"end_line":20,
 "signature":"calculate_total(cart: Cart, tax_rate: float = …) -> Decimal",
 "params":[{"name":"cart","kind":"positional_or_keyword","has_default":false,"type":"Cart"},
           {"name":"tax_rate","kind":"positional_or_keyword","has_default":true,"type":"float"}],
 "file_hash":"8f2c6d1e0a9b7c3d",
 "criticality":{"tier":"high","pr_pct":97.4,"pagerank":0.0213},
 "source":"def calculate_total(cart: Cart, tax_rate: float = 0.1) -> Decimal:\n    \"\"\"Total including tax.\"\"\"\n    subtotal = sum(i.price * i.qty for i in cart.items)\n    return round_money(subtotal * (1 + Decimal(str(tax_rate))))",
 "callers":[{"id":"app.py::<module>","file":"app.py","line":31,"col":9,"call":"calculate_total(cart)","confidence":"exact"},
            {"id":"checkout.py::checkout","file":"checkout.py","line":18,"col":13,"call":"calculate_total(cart, tax_rate=rate)","confidence":"exact"}],
 "caller_count":4,
 "callees":[{"id":"utils.py::round_money","file":"utils.py","signature":"round_money(x: Decimal) -> Decimal","line":16}],
 "history":[{"patch_id":"p_3c4d5e6f7a8b","status":"REJECTED","intent":"Add currency to totals","reasoning":"…","ts":1760500000000}],
 "pitfalls":[{"key":"9c0e…","code":"CALLER_INCOMPATIBLE","lesson":"Changing the signature of …","count":3}],
 "memories":[{"id":"m_77de00112233","kind":"convention","content":"Money is Decimal, never float."}]}
```
`file_hash` lets the agent pass `base_hashes` to `propose_patch`.

### 8.6.5 `get_impact`

Description: *"Transitive callers of a symbol (who breaks if it changes), grouped by depth, plus affected files. Call before changing a signature or deleting."*
```json
{"type":"object","additionalProperties":false,"required":["symbol"],
 "properties":{"symbol":{"type":"string","minLength":1,"maxLength":500},"file":{"type":"string","maxLength":500},
               "depth":{"type":"integer","minimum":1,"maximum":5,"default":3},
               "limit":{"type":"integer","minimum":1,"maximum":500,"default":100}}}
```
`Q_IMPACT_D{depth}` + `Q_IMPACT_FILES_D{depth}` (pipelined).
```json
{"id":"utils.py::round_money","depth":3,
 "criticality":{"tier":"high","pr_pct":98.1},
 "risk":"high",
 "callers":[{"id":"billing.py::calculate_total","file":"billing.py","signature":"…","depth":1,"criticality":"high"},
            {"id":"checkout.py::checkout","file":"checkout.py","signature":"…","depth":2,"criticality":"medium"}],
 "files":["billing.py","checkout.py","app.py","reports.py"],"truncated":false}
```
`risk` = `high` if the target or any depth-1 caller is `high` tier or the impact set spans ≥ 5 files; `medium` if ≥ 2 files or any `medium`; else `low`.
```json
```

### 8.6.6 `get_module_map`

Description: *"Architecture overview: files under a path with their import edges, the most central functions (PageRank), bridge modules (betweenness) and isolated files (connected components)."*
```json
{"type":"object","additionalProperties":false,
 "properties":{"path_prefix":{"type":"string","maxLength":500,"default":""},
               "limit":{"type":"integer","minimum":1,"maximum":1000,"default":200},
               "include_hotspots":{"type":"boolean","default":true}}}
```
```json
{"files":[{"file":"billing.py","language":"python","symbols":6,"imports":["models.py","utils.py"],"imported_by_count":3}],
 "hotspots":[{"id":"utils.py::round_money","signature":"round_money(x: Decimal) -> Decimal","fan_in":7,"pr_pct":98.1}],
 "bridges":[{"file":"billing.py","score":4.0}],
 "islands":["scripts/one_off.py"],
 "truncated":false}
```

### 8.6.7 `propose_patch`

Description: *"The ONLY way to change code. Applies one or more edits (across files) atomically after verification: syntax, signature changes vs all callers (blast radius), calls to missing/incompatible functions, and quality rules. Returns APPLIED (written to the working tree), VALIDATED (dry_run) or REJECTED (nothing written) with exact locations and fix hints. Include every caller update in the same patch when changing a signature. While a changeset is open, cross-file problems are tracked as obligations instead of rejections."*

```json
{"type":"object","additionalProperties":false,"required":["intent","reasoning","edits"],
 "properties":{
  "intent":{"type":"string","minLength":1,"maxLength":500,"description":"The user-level goal this patch serves."},
  "reasoning":{"type":"string","minLength":1,"maxLength":4000,"description":"Why this change, briefly."},
  "dry_run":{"type":"boolean","default":false},
  "base_hashes":{"type":"object","additionalProperties":{"type":"string"},"description":"file → file_hash you last read; mismatch rejects with STALE_BASE."},
  "edits":{"type":"array","minItems":1,"maxItems":200,"items":{
    "type":"object","additionalProperties":false,"required":["op","file"],
    "properties":{
      "op":{"type":"string","enum":["replace_symbol","insert_symbol","delete_symbol","replace_text","create_file","delete_file","move_file"]},
      "file":{"type":"string","minLength":1,"maxLength":500},
      "symbol":{"type":"string","maxLength":500,"description":"replace_symbol/delete_symbol: qualname or id"},
      "code":{"type":"string","maxLength":1048576,"description":"replace_symbol/insert_symbol: complete definition"},
      "anchor":{"type":"object","additionalProperties":false,"properties":{
         "after":{"type":"string"},"before":{"type":"string"},"into_class":{"type":"string"},"at_start":{"type":"boolean"}}},
      "old_text":{"type":"string","maxLength":1048576},
      "new_text":{"type":"string","maxLength":1048576},
      "content":{"type":"string","maxLength":1048576},
      "to":{"type":"string","maxLength":500,"description":"move_file: destination path"}}}}}}
```
Per-op required fields (validated after schema, → `PATCH_INVALID` violation, not a tool error):
`replace_symbol: symbol, code` · `insert_symbol: code` · `delete_symbol: symbol` · `replace_text: old_text, new_text` · `create_file: content` · `delete_file: —` · `move_file: to`. Anchor: at most one of after/before/into_class/at_start.

**A REJECTED patch is a successful tool call (`isError:false`)** — rejection is the gate working, and the model must read it as data. `isError:true` only for DB_UNAVAILABLE/INTERNAL.

Example 1 — rejection (single-file signature change):
```json
{"intent":"Add currency to totals","reasoning":"Totals must carry ISO currency.",
 "edits":[{"op":"replace_symbol","file":"billing.py","symbol":"calculate_total",
           "code":"def calculate_total(cart: Cart, currency: str, tax_rate: float = 0.1) -> Money:\n    ..."}]}
```
→ text: `REJECTED at BLAST_RADIUS: 3 errors, 0 warnings (patch p_3c4d5e6f7a8b)` + JSON:
```json
{"patch_id":"p_3c4d5e6f7a8b","status":"REJECTED","failed_phase":"BLAST_RADIUS",
 "violations":[
  {"code":"CALLER_INCOMPATIBLE","severity":"error","phase":"BLAST_RADIUS",
   "message":"app.py:31 calls calculate_total(cart) but the new signature requires 'currency'.",
   "symbol_id":"billing.py::calculate_total","location":{"file":"app.py","line":31,"col":9},
   "evidence":{"callee":"billing.py::calculate_total","old_signature":"calculate_total(cart: Cart, tax_rate: float = …) -> Decimal",
               "new_signature":"calculate_total(cart: Cart, currency: str, tax_rate: float = …) -> Money",
               "call":"calculate_total(cart)","reason":"missing required argument 'currency'","confidence":"exact"},
   "fix_hint":"Update this call in the same propose_patch (add an edit for app.py), or give 'currency' a default value."},
  {"code":"CALLER_INCOMPATIBLE","severity":"error","phase":"BLAST_RADIUS","message":"checkout.py:18 …","location":{"file":"checkout.py","line":18,"col":13},"evidence":{"…":"…"},"fix_hint":"…"},
  {"code":"CALLER_INCOMPATIBLE","severity":"error","phase":"BLAST_RADIUS","message":"reports.py:44 …","location":{"file":"reports.py","line":44,"col":21},"evidence":{"…":"…"},"fix_hint":"…"}],
 "warnings":[],
 "changes":[{"file":"billing.py","change":"modified","pre_hash":"8f2c6d1e0a9b7c3d","post_hash":"","symbols_added":[],"symbols_removed":[],
             "symbols_signature_changed":["billing.py::calculate_total"],"symbols_body_changed":[]}],
 "lessons":[{"code":"CALLER_INCOMPATIBLE","symbol_id":"billing.py::calculate_total","lesson":"Changing the signature of `billing.py::calculate_total` breaks 3 caller(s) (app.py, checkout.py, reports.py). Update callers in the same propose_patch or keep the change backward-compatible (defaults).","count":2}],
 "timings_ms":{"p0":0.04,"p1":0.03,"p2":0.31,"p3":0.18,"p4":0.52,"p5":0.21,"total":1.4}}
```

Example 2 — atomic multi-file apply:
```json
{"intent":"Add currency to totals","reasoning":"Callers updated together.",
 "edits":[
  {"op":"replace_symbol","file":"billing.py","symbol":"calculate_total","code":"def calculate_total(cart: Cart, currency: str, tax_rate: float = 0.1) -> Money:\n    ..."},
  {"op":"replace_text","file":"app.py","old_text":"total = calculate_total(cart)","new_text":"total = calculate_total(cart, \"USD\")"},
  {"op":"replace_text","file":"checkout.py","old_text":"calculate_total(cart, tax_rate=rate)","new_text":"calculate_total(cart, user.currency, tax_rate=rate)"},
  {"op":"replace_symbol","file":"reports.py","symbol":"daily_report","code":"def daily_report(orders):\n    ..."}]}
```
→ text `APPLIED 4 files, 2 symbols changed (patch p_9a8b7c6d5e4f) in 7.9 ms` + JSON:
```json
{"patch_id":"p_9a8b7c6d5e4f","status":"APPLIED","failed_phase":null,"violations":[],
 "warnings":[{"code":"RULE_VIOLATION","severity":"warn","phase":"QUALITY","rule_id":"max-function-lines","message":"daily_report is 92 lines (max 80).","location":{"file":"reports.py","line":30,"col":1},"evidence":{"value":92,"max":80},"fix_hint":"Extract helpers."}],
 "changes":[{"file":"billing.py","change":"modified","pre_hash":"8f2c…","post_hash":"1d7e…","symbols_added":[],"symbols_removed":[],"symbols_signature_changed":["billing.py::calculate_total"],"symbols_body_changed":[]},
            {"file":"app.py","change":"modified","pre_hash":"…","post_hash":"…","symbols_added":[],"symbols_removed":[],"symbols_signature_changed":[],"symbols_body_changed":["app.py::<module>"]}],
 "lessons":[{"code":"CALLER_INCOMPATIBLE","symbol_id":"billing.py::calculate_total","lesson":"…","count":2}],
 "timings_ms":{"p0":0.05,"p1":0.09,"p2":0.9,"p3":0.4,"p4":0.7,"p5":0.3,"p6":5.1,"total":7.9}}
```
`status` values: `APPLIED` (verified + written to the working tree; not a VCS commit) | `VALIDATED` (dry_run, no errors) | `REJECTED` (nothing written).
Optional result fields: `changeset` (while a changeset is OPEN — §06.14.7) and `suggestion` (§06.14.8).

Example 3 — inside a changeset (greenfield):
```json
{"intent":"Build order API","reasoning":"Routes first; services next patch.",
 "edits":[{"op":"create_file","file":"api/routes.py","content":"from services import create_order\n\n\ndef post_order(req):\n    return create_order(req.user_id, req.items)\n"}]}
```
→ `APPLIED` + `"changeset":{"id":"c_4e5f6a7b8c9d","open_obligations":1,"obligations":[{"code":"MISSING_MODULE","message":"api/routes.py:1 imports 'services', which does not exist yet.", …}]}`. The next patch creates `services.py` with `create_order` → `"open_obligations":0,"discharged":["…"]`.

### 8.6.8 `remember`

Description: *"Save a durable memory: a user preference (follows the user across projects), a project convention, a fact, a todo or a warning. Attach it to symbols/files with 'about'. Never store secrets."*
```json
{"type":"object","additionalProperties":false,"required":["content","kind"],
 "properties":{"content":{"type":"string","minLength":1,"maxLength":2000},
   "kind":{"type":"string","enum":["preference","convention","fact","todo","warning"]},
   "scope":{"type":"string","enum":["user","project"]},
   "about":{"type":"array","items":{"type":"string","maxLength":500},"maxItems":20},
   "tags":{"type":"array","items":{"type":"string","maxLength":50},"maxItems":10},
   "importance":{"type":"integer","minimum":1,"maximum":3,"default":2},
   "supersedes":{"type":"string","maxLength":20}}}
```
```json
{"id":"m_1a2b3c4d5e6f","scope":"user","deduplicated":false,"anchors":[]}
```

### 8.6.9 `recall`

Description: *"Search past memories, decisions, pitfalls, intents and sessions by words, or everything attached to a symbol/file."*
```json
{"type":"object","additionalProperties":false,
 "properties":{"query":{"type":"string","maxLength":500},
   "types":{"type":"array","items":{"type":"string","enum":["memory","decision","pitfall","session","intent"]}},
   "scope":{"type":"string","enum":["all","project","user"],"default":"all"},
   "about":{"type":"string","maxLength":500},
   "limit":{"type":"integer","minimum":1,"maximum":50,"default":10}}}
```
At least one of `query`/`about` (else `EMPTY_QUERY`).
```json
{"results":[{"type":"memory","id":"m_77de00112233","score":1.25,"text":"Money is Decimal, never float.","kind":"convention","scope":"project"},
            {"type":"decision","id":"p_3c4d5e6f7a8b","score":0.71,"text":"[REJECTED] Add currency to totals — Totals must carry ISO currency.","ts":1760500000000}]}
```

### 8.6.10 `recall_pitfalls`

Description: *"Known mistakes recorded from rejected patches, most frequent first. Filter by symbol, file or violation code."*
```json
{"type":"object","additionalProperties":false,
 "properties":{"symbol":{"type":"string","maxLength":500},"file":{"type":"string","maxLength":500},
   "code":{"type":"string","maxLength":64},"limit":{"type":"integer","minimum":1,"maximum":100,"default":20}}}
```
`Q_PITFALLS_LIST`.
```json
{"pitfalls":[{"key":"9c0e…","code":"CALLER_INCOMPATIBLE","rule_id":"","symbol_id":"billing.py::calculate_total","file":"billing.py",
              "lesson":"…","count":3,"resolved_count":1,"first_seen":1760400000000,"last_seen":1760500000000,"last_resolved":1760500100000}]}
```

### 8.6.11 `end_session`

Description: *"Call before finishing: store a short summary of what was done and what remains, and mark intents done/abandoned."*
```json
{"type":"object","additionalProperties":false,"required":["summary"],
 "properties":{"summary":{"type":"string","minLength":1,"maxLength":2000},
   "intents_done":{"type":"array","items":{"type":"string"}},
   "intents_abandoned":{"type":"array","items":{"type":"string"}}}}
```
```json
{"session_id":"s_9f1c2a7b3d4e","status":"ENDED","intents":{"done":["i_…"],"open":["i_…"],"abandoned":[]},
 "stats":{"patches":5,"applied":4,"rejected":1}}
```

### 8.6.12 `reindex`

Description: *"Re-index specific files or the whole workspace (normally automatic)."*
```json
{"type":"object","additionalProperties":false,
 "properties":{"files":{"type":"array","items":{"type":"string"},"maxItems":1000},"full":{"type":"boolean","default":false}}}
```
```json
{"scanned":41,"parsed":41,"changed":2,"deleted":0,"skipped":0,"ms":38.2}
```

### 8.6.13 `status`

Description: *"Engine health: database, graph sizes, index freshness, gate mode, rule errors, pending journal entries."*
```json
{"type":"object","additionalProperties":false}
```
```json
{"version":"0.1.0","root":"/Users/me/benchmark_project","graph":"gs:benchmark_project-3f9a1c2e",
 "db":{"connected":true,"ping_ms":0.21,"host":"127.0.0.1","port":6390},
 "counts":{"File":41,"Function":212,"Class":37,"Session":9,"Decision":63,"Pitfall":7,"Memory":12},
 "user_memories":5,"last_indexed_at":1760500000000,"watcher":"running","gate_mode":"enforce",
 "rules_errors":[],"wal_pending":0,"session_id":"s_9f1c2a7b3d4e"}
```

### 8.6.14 `changeset`

Description: *"Group several patches into one unit of work for building new code or multi-step refactors. begin: open (cross-file problems become obligations instead of rejections). status: list obligations. finalize: close when no obligations remain. abandon: restore every file the changeset touched."*
```json
{"type":"object","additionalProperties":false,"required":["action"],
 "properties":{"action":{"type":"string","enum":["begin","status","finalize","abandon"]},
   "intent":{"type":"string","minLength":1,"maxLength":500,"description":"begin: what you are building"}}}
```
- `begin` requires `intent` (also upserts the Intent). Error `CHANGESET_OPEN` if one is open.
- `status` / `finalize` / `abandon` act on the open changeset; none open → error `NO_OPEN_CHANGESET`.
- `finalize` with obligations → **not an error**: `{"status":"OPEN","open_obligations":N,"obligations":[…]}`; with none → `{"status":"CLOSED"}`.
- `abandon` with conflicts → `isError:false`, `{"status":"OPEN","conflicts":["web/cart.ts"],"message":"files changed outside the changeset; resolve or ask the user to run graphstate changeset abandon --force"}`.

```json
{"id":"c_4e5f6a7b8c9d","intent":"Build order API","status":"OPEN","files":["api/routes.py","services.py"],
 "open_obligations":0,"obligations":[],"discharged":["…"],"created_at":1760500000000}
```

## 8.7 Resources

`resources/list`:
```json
{"resources":[
 {"uri":"graphstate://briefing","name":"briefing","title":"Session briefing","description":"Same as get_briefing (no goal).","mimeType":"text/markdown"},
 {"uri":"graphstate://rules","name":"rules","title":"Effective quality rules","description":"Built-in + rules.json, with severities.","mimeType":"application/json"}]}
```
`resources/read {uri}` → `{"contents":[{"uri":"graphstate://briefing","mimeType":"text/markdown","text":"# GraphState briefing …"}]}`. Unknown URI → `-32002` (legacy) / `-32602` (modern) with `data.uri`. Modern adds `ttlMs: 0, cacheScope: "private"`.

## 8.8 Prompts

`prompts/list` → one prompt:
```json
{"prompts":[{"name":"graphstate_workflow","title":"Work through GraphState","description":"Start a task the GraphState way.",
             "arguments":[{"name":"task","description":"What you want done","required":true}]}]}
```
`prompts/get {name:"graphstate_workflow", arguments:{task}}` →
```json
{"description":"GraphState workflow","messages":[{"role":"user","content":{"type":"text","text":"Task: <task>\n\nWork through GraphState: 1) get_briefing with this task as goal; 2) explore with get_file_outline/search_symbols/read_symbol; 3) get_impact before changing signatures; 4) propose_patch with dry_run first for multi-file changes, then apply; 5) remember any durable preference you learn; 6) end_session with a summary."}}]}
```

## 8.9 Tool registry (`mcp/tool_registry.hpp`)

```cpp
namespace gs::mcp {
struct ToolContext { Engine& engine; const memory::SessionCtx& session; };
using Handler = std::function<nlohmann::json(ToolContext&, const nlohmann::json& args)>; // returns structuredContent; throws ToolError
struct ToolError { std::string code, message; nlohmann::json data; };
struct Tool {
  std::string name, title, description;
  nlohmann::json input_schema, annotations;
  Handler handler;
  std::function<std::string(const nlohmann::json&)> render;   // default: compact dump
};
class ToolRegistry {
 public:
  void add(Tool);
  const Tool* find(std::string_view) const;
  nlohmann::json list_json() const;                            // tools/list payload
};
void register_read_tools(ToolRegistry&);  void register_write_tools(ToolRegistry&);
void register_memory_tools(ToolRegistry&); void register_admin_tools(ToolRegistry&);
}
```
`tools/call` flow: find tool → validate args (§8.3) → `Sessions::ensure` + heartbeat → **lock `state_mu`** → handler → unlock → attach notices → render → envelope. Measure handler time; log `tool=<name> ms=<t> status=<ok|error>`.
