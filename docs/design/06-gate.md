# 06 — The Verification Gate (write path)

> Index: [README.md](README.md). Code: `src/gate/*`. Tool contract: [08-mcp-protocol.md](08-mcp-protocol.md) §8.6.7.
> Depends on: FactStore/Extractors ([05-indexer.md](05-indexer.md)), queries ([04-graph-schema.md](04-graph-schema.md)), memory recording ([07-memory.md](07-memory.md)).

## 6.1 Contract in one paragraph

`Gate::propose(PatchRequest) -> PatchResult` takes a list of edits across up to 100 files, computes the full **post-state** of every touched file in memory, and runs phases P0–P5 against it. If no phase produces an `error`-severity violation (and `dry_run` is false), P6 commits every file atomically (all or nothing, crash-safe), updates FactStore and the graph, and records a `APPLIED` Decision. Otherwise nothing on disk changes, and a `REJECTED` Decision plus Pitfalls are recorded. Phases short-circuit: P2 errors stop before P3, and so on. **Exception:** P4 and P5 both run, so the agent gets all semantic + quality feedback in one round trip.

**Terminology (no VCS):** `APPLIED` means *verified and written to the working tree* — not a git commit. `REJECTED` means *nothing was written*. GraphState never runs git write commands (§6.15).

**Two working modes:** *strict* (default; every patch must leave the workspace consistent) and *changeset* (incremental/greenfield building; cross-file consistency checks become tracked obligations that must be cleared before the changeset closes — §6.14).

```
P0 Preflight ─► P1 Edit ─► P2 Syntax ─► P3 Symbol diff ─► P4 Blast radius + Reference check ─► P5 Quality rules ─► P6 Write
     │             │            │                                     │                            │
     └── reject ───┴── reject ──┴────────── reject ───────────────────┴────── (P4+P5 merged) ──────┘
```

## 6.2 Data model (`src/gate/patch.hpp`)

```cpp
namespace gs::gate {

enum class OpKind { ReplaceSymbol, InsertSymbol, DeleteSymbol, ReplaceText, CreateFile, DeleteFile, MoveFile };

struct InsertAnchor {                     // exactly one of these is set; default = at_end of file
  std::optional<std::string> after;       // qualname in same file, e.g. "Invoice.total"
  std::optional<std::string> before;      // qualname
  std::optional<std::string> into_class;  // qualname of class → appended at end of class body
  bool at_start = false;                  // after module docstring + imports block
};

struct Edit {
  OpKind op;
  std::string file;                       // workspace-relative, '/' separators
  std::optional<std::string> symbol;      // ReplaceSymbol/DeleteSymbol: qualname or full id
  std::optional<std::string> code;        // ReplaceSymbol/InsertSymbol: full definition text
  std::optional<InsertAnchor> anchor;     // InsertSymbol
  std::optional<std::string> old_text;    // ReplaceText: must occur exactly once in current buffer
  std::optional<std::string> new_text;    // ReplaceText
  std::optional<std::string> content;     // CreateFile
  std::optional<std::string> to;          // MoveFile: destination path
};

struct PatchRequest {
  std::string intent;                     // 1..500 chars, required
  std::string reasoning;                  // 1..4000 chars, required
  std::vector<Edit> edits;                // 1..200
  std::map<std::string, std::string> base_hashes; // optional: file → hash the agent last saw
  bool dry_run = false;
};

enum class Severity { Error, Warn, Info };

struct Location { std::string file; int line = 0; int col = 0; };   // 1-based; col in UTF-8 bytes + 1

struct Violation {
  std::string code;                       // see §6.10 catalogue
  Severity severity = Severity::Error;
  std::string phase;                      // "PREFLIGHT"|"EDIT"|"SYNTAX"|"BLAST_RADIUS"|"REFERENCES"|"QUALITY"|"WRITE"
  std::string message;                    // one sentence, imperative fix where possible
  std::optional<std::string> symbol_id;   // the symbol the violation is ABOUT (callee for caller breaks)
  std::optional<Location> location;       // where to look (call site, syntax error, rule hit)
  nlohmann::json evidence = nlohmann::json::object(); // code-specific, see §6.10
  std::string fix_hint;                   // concrete suggestion
  std::optional<std::string> rule_id;     // for RULE_VIOLATION
};

enum class PatchStatus { Applied, Rejected, Validated /*dry_run ok*/ };

struct FileChange {
  std::string file; std::string change;   // "modified"|"created"|"deleted"|"moved"
  std::string pre_hash, post_hash;        // "" when absent
  std::vector<std::string> symbols_added, symbols_removed, symbols_signature_changed, symbols_body_changed;
};

struct PatchResult {
  std::string patch_id;                   // "p_" + 12 hex (Ids::patch())
  PatchStatus status;
  std::optional<std::string> failed_phase;
  std::vector<Violation> violations;      // severity Error
  std::vector<Violation> warnings;        // severity Warn/Info
  std::vector<FileChange> changes;        // post-state diff summary (also for REJECTED: what WOULD have changed)
  std::vector<nlohmann::json> lessons;    // past pitfalls relevant to touched symbols (§6.8)
  std::map<std::string, double> timings_ms; // "p0".."p6","total"
  std::optional<ChangesetState> changeset;  // present while a changeset is OPEN (§6.14)
  std::optional<std::string> suggestion;    // §6.14.8
};

class Gate {
 public:
  Gate(Engine& engine);                   // engine gives FactStore, Resolver, GraphClient, Config, Memory
  PatchResult propose(const PatchRequest& req, const SessionCtx& session);
  std::vector<Violation> audit(const std::string& file, const FileFacts& pre, const FileFacts& post); // watcher
 private:
  // phases — each returns violations; private members hold the in-flight WorkingSet
};

} // namespace gs::gate
```

`WorkingSet` (internal): `std::map<std::string /*file*/, Buffer>` where `Buffer { std::optional<std::string> pre; std::optional<std::string> post; Language lang; std::vector<ByteRange> edited_ranges; FileFacts pre_facts, post_facts; }`.

## 6.3 P0 — Preflight

For every distinct file in `edits`, in order:

| Check | Violation |
|---|---|
| `edits.size()` in 1..`gate.max_edits` (200), distinct files ≤ `gate.max_files` (100), each `code`/`content`/`new_text` ≤ 1 MiB, total payload ≤ `gate.max_patch_bytes` (8 MiB), `intent` and `reasoning` non-empty | `PATCH_INVALID` |
| New file names (`create_file`, `move_file.to`) must be portable (§12.6: no Windows reserved names, no trailing dot/space, no `<>:"\|?*`) | `PATH_INVALID` |
| Normalize path (§12.6): `\` → `/`; reject absolute paths (`/x`, `C:\x`), drive-relative, UNC, `..` segments; resolve case on case-insensitive file systems; `weakly_canonical(root/file)` MUST start with `root/`; if the file exists, `canonical()` (symlinks resolved) MUST also start with `root/` | `PATH_OUTSIDE_WORKSPACE` |
| Path matches `gate.protected` globs (default: `.git/**`, `.graphstate/**`, `**/.env`, `**/.env.*`, `**/*.lock`, `**/package-lock.json`, `**/node_modules/**`) | `PATH_PROTECTED` |
| Path matches index ignore rules but is not `create_file` of a new source file | `PATH_IGNORED` |
| File must exist for Replace/Insert/Delete/ReplaceText/DeleteFile/MoveFile; must not exist for CreateFile and `MoveFile.to` | `FILE_NOT_FOUND` / `FILE_EXISTS` |
| `base_hashes[file]` present and ≠ current disk hash | `STALE_BASE` (evidence: `{expected, actual}`; fix_hint: "re-read the symbol and rebuild the edit") |
| Disk hash ≠ FactStore hash (external edit not yet seen) | not a violation: **re-index that file synchronously first** (FactStore + graph), add an `EXTERNAL_CHANGE_SYNCED` Info warning |
| Symbol ops (Replace/Insert/DeleteSymbol) on a language without semantic support (§05.2) | `UNSUPPORTED_OP` (fix_hint: "use replace_text") |
| Language unknown and `gate.unknown_files == "deny"` | `PATH_PROTECTED` |

Load `pre` contents from disk (one read per file) into the WorkingSet.

## 6.4 P1 — Edit (build the post-state)

Edits are applied **in array order**; edits to the same file see the result of earlier edits. Symbol lookups re-parse the current buffer (cheap: ~0.3 ms) so byte ranges are always fresh.

### 6.4.1 Symbol range (shared by Replace/Delete/Insert-anchor)

The *definition range* of a symbol is the byte range of its definition node **expanded to include**:
- Python: the enclosing `decorated_definition` if present (so decorators move with the function).
- TS/JS: the enclosing `export_statement` if the declaration is its direct child (`export function f…`, `export default class …`, `export const f = () => …` — for the last, the range is the whole `lexical_declaration`/`export_statement`).
- Leading line indentation is NOT part of the range; the range starts at the first byte of the node.

`read_symbol` returns exactly this range as `source`, so a read → modify → `replace_symbol` round trip is lossless.

### 6.4.2 `replace_symbol {file, symbol, code}`

1. Resolve `symbol` in the current buffer's facts: accept `path::Qual.name`, `Qual.name`, or bare `name` if unique in the file. Not found → `SYMBOL_NOT_FOUND` (evidence: up to 5 nearest qualnames by edit distance). Multiple → `SYMBOL_AMBIGUOUS` (evidence: candidates).
2. Parse `code` standalone with the file's language. It MUST contain exactly one top-level definition (function/class/method, optionally decorated/exported) and nothing else except comments → else `CODE_NOT_SINGLE_SYMBOL`.
   For methods (symbol is inside a class) parse the dedented `code` as top-level — a method definition is a valid top-level function in Python; in TS wrap it as `class __gs__ {\n<code>\n}` for parsing.
3. **Re-indent** (Python and TS): strip the common leading whitespace of all non-blank lines of `code` (like `textwrap.dedent`), then prefix every non-blank line except the first with the original symbol's indentation string (the exact whitespace preceding the symbol's first byte on its line). The first line is inserted at the range start, which already sits after that indentation.
4. **EOL**: if `pre` contains `\r\n`, convert `\n` in code to `\r\n`.
5. Splice: `buf = buf[0:start) + code' + buf[end:)`. Record the edited range `[start, start+len(code'))` in `edited_ranges` (shifting earlier recorded ranges after `start` by the length delta).
6. A changed name is allowed (it becomes remove+add in P3).

### 6.4.3 `insert_symbol {file, code, anchor}`

- Same parse/single-symbol/dedent/EOL rules as replace.
- Position:
  - `after: Q` → immediately after Q's definition range end, preceded by a separator.
  - `before: Q` → at Q's range start (after its indentation), followed by a separator.
  - `into_class: C` → after the last member of C's body; indentation = indentation of C's first member (or C's indentation + 4 spaces / the file's detected indent unit if the body is `pass`/empty).
  - `at_start` → after the leading module docstring/comments and the contiguous top-of-file import block.
  - default → end of file (ensure the file ends with a newline first).
- Separator: Python top level `\n\n\n` (PEP 8: two blank lines), Python in class `\n\n`, TS/JS `\n\n`. Avoid tripling blank lines: trim trailing blank lines before inserting.

### 6.4.4 `delete_symbol {file, symbol}`

Remove the definition range plus the indentation before it and the trailing newline; collapse ≥3 consecutive blank lines left behind to 2. If deleting the last member of a Python class body would leave the body empty, insert `pass` at the member indentation.

### 6.4.5 `replace_text {file, old_text, new_text}`

`old_text` MUST occur exactly once in the current buffer (byte compare; EOL-normalized: compare after converting both to `\n` if the file is CRLF). 0 → `TEXT_NOT_FOUND` (evidence: the most similar line, by edit distance, with its line number). >1 → `TEXT_NOT_UNIQUE` (evidence: line numbers of all occurrences, max 10). Used for imports, module-level constants, non-symbol code, and any language.

### 6.4.6 `create_file {file, content}` / `delete_file {file}`

Create: parent directories are created at write time. Ensure trailing newline; EOL per `gate.new_file_eol` (§12.7). Delete: post-state = absent.

### 6.4.7 `move_file {file, to}`

Post-state: `file` absent, `to` present with identical bytes (later edits in the same patch may target `to`). P3 treats it as delete + create (symbol ids embed the path, so they change). Importers of the old path surface as `BROKEN_IMPORT` in P4 — update them in the same patch (or inside a changeset). P6 writes `to` via tmp+rename and removes `file`; both are journaled.

## 6.5 P2 — Syntax gate

For each file in the WorkingSet with a known grammar (semantic or syntax-only):

1. Parse `post` (thread-local `TSParser`, `ts_parser_parse_string`).
2. If `!ts_node_has_error(root)` → pass.
3. Otherwise walk with a `TSTreeCursor`, descending only into nodes where `ts_node_has_error` is true. Collect each node where `ts_node_is_error(n) || ts_node_is_missing(n)`:
   - `kind`: `"missing"` if `is_missing` (evidence `expected` = `ts_node_type(n)`, e.g. `")"`, `":"`), else `"unexpected"`.
   - `location`: `ts_node_start_point(n)` → `line = row+1`, `col = column+1`.
   - `evidence.line_text`: that full line (trimmed to 200 chars), `evidence.pointer`: spaces + `^` under the column.
   - `evidence.enclosing_symbol`: qualname of the innermost function/class containing the point (from post facts, if extractable).
4. **Pre-existing errors don't block.** If `pre` existed and also had errors: an error is *introduced* iff its start byte lies inside any `edited_ranges` entry or the total error count increased. Only introduced errors are violations; pre-existing ones are added as one Info warning `PREEXISTING_SYNTAX_ERRORS` (count).
5. Report at most 10 errors per file (`evidence.truncated = true` if more).
6. Any violation → status REJECTED, `failed_phase = "SYNTAX"`, stop.

Message format: `"Syntax error in billing.py at 14:23: missing ')'"`, fix_hint: `"Close the call opened at 14:9."` (when the missing token is a closer, find the nearest unmatched opener on the same or previous line; else generic "Fix the syntax at the marked position.").

## 6.6 P3 — Symbol diff

For each file with semantic support, `post_facts = Extractor::extract(post)` (§05.5). Compare with `pre_facts` by symbol **id**:

| Class | Rule |
|---|---|
| `added` | id in post, not in pre |
| `removed` | id in pre, not in post |
| `signature_changed` | in both; `Signature::canonical()` differs (param names, kinds, defaults-present flags, annotations, async flag, return type — see §05.6) |
| `body_changed` | in both; `body_hash` differs (xxh3 of the body bytes with whitespace runs collapsed) |
| `renamed` (info only) | a removed and an added symbol in the same file with equal `body_hash` and same kind → emit Info `SYMBOL_RENAMED {from,to}`; still treated as remove+add for P4 |

Output: `SymbolDelta { added, removed, sig_changed:(pre_sig, post_sig), body_changed }` per file, plus the `FileChange` summary for the result.

Files that are created/deleted: all symbols are added/removed.

## 6.7 P4 — Blast radius & reference check

All checks use the **post-state world** = graph state, overridden by post facts for files in the WorkingSet.

### 6.7.1 Callers of changed/removed symbols (graph-powered)

```
targets = removed ∪ sig_changed      (ids)
rows = Q_CALLERS_OF_IDS(ids=targets, exclude_files=WorkingSet.files)      // §04.7 — ONE query
     ∪ post-state call sites inside WorkingSet files whose resolved callee ∈ targets   // from Resolver over post facts
```

For each caller row `{caller_id, caller_file, line, col, call_text, pos_args, kw_names, has_star, has_dstar, confidence, call_kind}`:

- target removed (and not re-added with the same id) → `REMOVED_SYMBOL_IN_USE` (severity: `error` if `confidence == "exact"`, else `warn`).
- target signature changed → `BindingChecker::check(post_sig, callsite, call_kind)` (§6.7.4):
  - `Incompatible` → `CALLER_INCOMPATIBLE` (error if exact, warn if heuristic).
  - `Unknown` (splats) → Info `CALLER_UNCHECKED`.
  - `Ok` → nothing.

Evidence: `{"callee": id, "old_signature": "...", "new_signature": "...", "call": "calculate_total(cart)", "reason": "missing required argument 'currency'"}`.
fix_hint: `"Update this call in the same propose_patch (add an edit for app.py), or give 'currency' a default value."`

### 6.7.2 Broken imports (graph-powered)

```
removed_names_by_file = { file → [top-level names removed (functions/classes)] }
rows = Q_IMPORTERS_OF_NAMES(file, names, exclude_files=WorkingSet.files)   // §04.7
     ∪ post-state imports in WorkingSet files
```
Each importer that imports a removed name → `BROKEN_IMPORT` (error). Deleted files: every importer of that file → `BROKEN_IMPORT` with `evidence.module_deleted = true`.

### 6.7.3 Outgoing references of new/changed code (anti-hallucination)

For each symbol in `added ∪ body_changed ∪ sig_changed` and each new import statement in WorkingSet files (imports present in post but not pre):

| Situation | Violation |
|---|---|
| Call resolves (exact) to a workspace function/class, and `BindingChecker` says `Incompatible` | `CALL_INCOMPATIBLE` (error). Evidence: callee signature + reason. |
| `from M import name` / `import M` + `M.name(...)` / TS `import {name} from './m'` where `M` resolves to a **workspace** module and `name` is not a top-level symbol, top-level variable, or re-export of `M` | `MISSING_SYMBOL` (error). Evidence: `{"module": "utils.py", "name": "format_money", "did_you_mean": ["format_currency", …]}` — up to 3 names from M's exports with Damerau-Levenshtein distance ≤ max(2, len/3), plus any name sharing a ≥ 4-char prefix. |
| `self.name(...)` inside a class where neither the class nor any **resolved** workspace base class defines `name` (and no base is unresolved/external) | `MISSING_SYMBOL` (error), `evidence.class` set. |
| Python bare call `name(...)` where `name` is not: a builtin (embedded list §05.9), a parameter, a name assigned/defined anywhere in the enclosing function(s), a module-level def/class/assignment/import, or a `global`/`nonlocal` | `UNDEFINED_NAME` (**warn** — scope analysis is approximate) |
| Import of a workspace-relative module path that does not exist (TS `./foo` with no file; Python relative `from .foo import x` with no file) | `MISSING_MODULE` (error) |
| Anything external (stdlib, node_modules, site-packages) | not checked |

### 6.7.4 BindingChecker (`src/lang/binding.hpp`) — normative algorithm

```cpp
namespace gs::lang {
enum class ParamKind { PositionalOnly, PositionalOrKeyword, VarPositional, KeywordOnly, VarKeyword };
struct Param { std::string name; ParamKind kind; bool has_default; bool optional /*TS ?*/; std::string type; };
struct Signature {
  std::vector<Param> params; bool is_async = false; std::string return_type;
  bool drop_first_for_instance_call = false;  // Python methods w/o @staticmethod (self/cls)
  bool has_overloads = false;                 // TS overload signatures exist → skip checks
  std::string canonical() const;              // stable text, e.g. "(cart, tax_rate=…, *, currency: str) -> Decimal"
};
struct CallSite { int pos_args; std::vector<std::string> kw_names; bool has_star; bool has_dstar; };
enum class CallKind { Plain, InstanceMethod, ClassMethodViaClass, Constructor, Unbound };
struct BindResult { enum { Ok, Incompatible, Unknown } verdict; std::string reason; };
BindResult check(const Signature&, const CallSite&, CallKind, Language);
}
```

**Python** (`Language::Python`):
1. If `has_overloads`… (n/a for Python). If `has_star || has_dstar` → `Unknown`.
2. `ps = params`; if `drop_first_for_instance_call` and `call_kind ∈ {InstanceMethod, Constructor}` → drop `ps[0]`. (Constructor = `ClassName(...)` checked against `__init__`; if the class has no `__init__` in itself or resolved bases → `Unknown`; `@dataclass` classes → `Unknown`.)
3. `positional = [p ∈ ps | kind ∈ {PositionalOnly, PositionalOrKeyword}]` (in order).
4. If `pos_args > |positional|` and no `VarPositional` → `Incompatible("takes N positional arguments but M were given")`.
5. `bound = {positional[i].name | i < min(pos_args, |positional|)}`.
6. For each `k ∈ kw_names`:
   - `p` = param named `k` with kind `PositionalOrKeyword` or `KeywordOnly`.
   - none → if `VarKeyword` exists: ok; else `Incompatible("unexpected keyword argument 'k'")`. (A `PositionalOnly` param named k does not match.)
   - `k ∈ bound` → `Incompatible("multiple values for argument 'k'")`. Else add to bound.
7. For each `p ∈ ps` with kind ∉ {VarPositional, VarKeyword}, `!has_default`, `p.name ∉ bound` → `Incompatible("missing required argument 'p'")` (report the first; list all in evidence).
8. `Ok`.

**TypeScript/TSX** — positional only (no keyword args):
1. `has_overloads` or `has_star` (spread) → `Unknown`.
2. Drop a leading param named `this`.
3. `required_min = 1 + index of last param with !optional && !has_default && kind != VarPositional` (0 if none).
4. `max = has VarPositional ? ∞ : |params|`.
5. `pos_args < required_min` → `Incompatible("expected at least X arguments, got N")`; `pos_args > max` → `Incompatible("expected at most Y arguments, got N")`.

**JavaScript**: same algorithm as TS, but any `Incompatible` is downgraded to `warn` (JS tolerates arity mismatch at runtime).

Unit tests for this function are mandatory (§10.2) — it is the heart of the demo.

## 6.8 P5 — Quality rules

Rules run **only on nodes inside `added ∪ sig_changed ∪ body_changed` symbol ranges** of post-state (plus whole content for `create_file`). Legacy code never blocks a patch.

### 6.8.1 Built-in rules

| id | Lang | Default severity | Default params | Detection |
|---|---|---|---|---|
| `max-function-lines` | all semantic | warn | `max: 80` | `end_line - start_line + 1` |
| `max-complexity` | all semantic | warn | `max: 12` | Cyclomatic = 1 + count of decision nodes (§05.8) |
| `max-nesting` | all semantic | warn | `max: 4` | max depth of nested block statements (§05.8) |
| `max-params` | all semantic | warn | `max: 6` | params excluding self/cls/this |
| `py-mutable-default-arg` | python | **error** | — | `default_parameter`/`typed_default_parameter` whose value is `list`, `dictionary`, `set`, or a `call` to `list`/`dict`/`set` |
| `py-bare-except` | python | **error** | — | `except_clause` with no exception type child |
| `py-public-type-hints` | python | off | — | public function (name not starting with `_`) with any non-self param lacking annotation or missing return annotation |
| `py-public-docstring` | python | off | `min_lines: 10` | public function longer than `min_lines` whose body's first statement isn't a string expression |
| `no-debug-print` | python, ts, tsx, js | warn | `exclude: ["**/test*/**","**/*_test.py","**/*.test.*","**/*.spec.*","scripts/**"]` | python `call` with function identifier `print`; ts/js `call_expression` whose function is member `console.log`/`console.debug` |
| `ts-no-explicit-any` | ts, tsx | warn | — | `predefined_type` node with text `any` |
| `no-empty-catch` | python, ts, tsx, js | warn | — | Python `except_clause` whose block is only `pass`; TS/JS `catch_clause` with empty `statement_block` |

### 6.8.2 `.graphstate/rules.json` (user/project-editable)

```json
{
  "version": 1,
  "rules": {
    "max-function-lines": { "severity": "warn", "max": 60 },
    "py-public-type-hints": { "severity": "error" },
    "no-debug-print": { "severity": "off" }
  },
  "custom": [
    {
      "id": "no-requests-without-timeout",
      "language": "python",
      "severity": "error",
      "paths": ["src/**"],
      "query": "((call function: (attribute object: (identifier) @mod attribute: (identifier) @fn) arguments: (argument_list) @args) @hit (#eq? @mod \"requests\") (#match? @fn \"^(get|post|put|delete)$\"))",
      "unless_query": "(keyword_argument name: (identifier) @k (#eq? @k \"timeout\"))",
      "message": "requests calls must pass timeout=",
      "fix_hint": "Add timeout=10 (seconds)."
    }
  ]
}
```

- `severity`: `"error"` (blocks), `"warn"`, `"off"`.
- Custom rule: `query` is a Tree-sitter query; each match is a hit at capture `@hit` (or the first capture if no `@hit`). Optional `unless_query` is run on the `@hit` node's subtree; if it matches, the hit is suppressed. `paths` (globs, default all) limits files.
- Tree-sitter core does **not** evaluate predicates; `RulesEngine` MUST implement `#eq?`, `#not-eq?`, `#match?`, `#not-match?`, `#any-of?` via `ts_query_predicates_for_pattern` (regexes compiled once with `std::regex::ECMAScript | std::regex::optimize`). Unknown predicates → rule load error (logged, rule disabled, `status` reports it).
- Invalid query (`ts_query_new` returns NULL) → rule disabled; error offset + type in `status.rules_errors`.
- Reloaded when `rules.json` mtime changes (checked by watcher).

Violation: code `RULE_VIOLATION`, `rule_id`, message from rule, location at hit, `evidence.snippet` (hit line).

## 6.9 P6 — Write (WAL + atomic rename)

Precondition: no error violations, `dry_run == false`, DB reachable (unless `gate.allow_write_without_db`). Add the paths to `Engine::inflight_paths` (the watcher skips them).

```
id = patch_id;  W = .graphstate/wal
1  mkdir W/<id>/
1b if a changeset is OPEN: Changesets::before_write(paths)          // first-touch snapshot (§6.14.5)
2  for each modified/deleted/moved file f:  platform::hardlink_or_copy(root/f, W/<id>/<escaped f>)   // O(1) backup; copy fallback
3  for each modified/created file f:
       mkdir -p parent(root/f)   (record created dirs in WAL for rollback)
       tmp = root/f.gs-<id>.tmp ; platform::write_file_durable(tmp, post) ; platform::set_file_mode(tmp, original mode | default)
4  write W/<id>.json = {state:"PREPARED", files:[{path, op, tmp, backup, pre_hash, post_hash}], created_dirs, decision:{…payload for Decision node…}, created_at}
   via tmp+fsync+rename; fsync(W)
5  set state "RENAMING" (same atomic write)
6  for each file: platform::replace_file(tmp, root/f)  |  platform::remove_file(root/f) for delete_file / move source ;
   platform::fsync_dir(parent) once per distinct dir (no-op on Windows)
7  set state "RENAMED"
8  FactStore.replace(post facts); Resolver.rebuild_for(changed files ∪ their importers)
9  Graph: pipeline [Q_UPSERT_FILE_SUBGRAPH per file …, Q_DELETE_FILE per deleted file, Q_REPLACE_CALLS for affected callers, Q_RECORD_DECISION] — one pipelined round trip
10 delete W/<id>.json and W/<id>/ (backups)
11 remove from inflight_paths
```

**Recovery (startup step 3)** for each `W/*.json`:
- `PREPARED` → delete `tmp` files, delete backups, remove created dirs if empty; record Decision `REJECTED` with violation `WRITE_FAILED` ("engine stopped before commit").
- `RENAMING` → for each file: if backup exists → `platform::replace_file(backup, root/f)`; if op was create (or move target) → `platform::remove_file(root/f)`; delete stray tmps; remove created dirs if empty; record Decision `REJECTED/WRITE_FAILED`.
- `RENAMED` → roll forward: record Decision `APPLIED` from payload (MERGE by patch_id → idempotent); FactStore/graph get fixed by the normal reconcile in startup step 8.
- Then delete the WAL entry. Log each action at WARN.

`fsync` on macOS: plain `fsync` (not `F_FULLFSYNC`) — documented trade-off for latency. Windows: `FlushFileBuffers` + `MoveFileExW(…WRITE_THROUGH)` with lock retries (§12.5). All file operations in this section go through `src/platform/` — no direct POSIX/Win32 calls in `gate/`.
Line endings and final newline: preserved from `pre` (P1 already normalized).

## 6.10 Violation code catalogue

| Code | Phase | Default severity | `evidence` keys |
|---|---|---|---|
| `PATCH_INVALID` | PREFLIGHT | error | `field`, `reason` |
| `PATH_OUTSIDE_WORKSPACE` | PREFLIGHT | error | `path` |
| `PATH_PROTECTED` | PREFLIGHT | error | `path`, `pattern` |
| `PATH_INVALID` | PREFLIGHT | error | `path`, `reason` (reserved name / illegal char / trailing dot) |
| `PATH_IGNORED` | PREFLIGHT | error | `path`, `pattern` |
| `FILE_NOT_FOUND` / `FILE_EXISTS` | PREFLIGHT | error | `path` |
| `STALE_BASE` | PREFLIGHT | error | `expected`, `actual` |
| `UNSUPPORTED_OP` | PREFLIGHT | error | `op`, `language` |
| `EXTERNAL_CHANGE_SYNCED` | PREFLIGHT | info | `path` |
| `SYMBOL_NOT_FOUND` / `SYMBOL_AMBIGUOUS` | EDIT | error | `symbol`, `candidates[]` |
| `CODE_NOT_SINGLE_SYMBOL` | EDIT | error | `found` (count / kinds) |
| `TEXT_NOT_FOUND` / `TEXT_NOT_UNIQUE` | EDIT | error | `closest_line`/`lines[]` |
| `SYNTAX_ERROR` | SYNTAX | error | `kind`, `expected`, `line_text`, `pointer`, `enclosing_symbol` |
| `PREEXISTING_SYNTAX_ERRORS` | SYNTAX | info | `count` |
| `SYMBOL_RENAMED` | — | info | `from`, `to` |
| `CALLER_INCOMPATIBLE` | BLAST_RADIUS | error (exact) / warn (heuristic) | `callee`, `old_signature`, `new_signature`, `call`, `reason`, `confidence` |
| `CALLER_UNCHECKED` | BLAST_RADIUS | info | `callee`, `call` |
| `CRITICAL_SYMBOL_CHANGED` | BLAST_RADIUS | warn | `symbol`, `pr_pct`, `tier`, `transitive_callers` — signature change/removal of a `high`-criticality function (§05.14); never blocks |
| `REMOVED_SYMBOL_IN_USE` | BLAST_RADIUS | error / warn | `callee`, `call`, `confidence` |
| `BROKEN_IMPORT` | BLAST_RADIUS | error | `module`, `name`, `module_deleted` |
| `CALL_INCOMPATIBLE` | REFERENCES | error (Py/TS) / warn (JS) | `callee`, `signature`, `call`, `reason` |
| `MISSING_SYMBOL` | REFERENCES | error | `module`, `name`, `class?`, `did_you_mean[]` |
| `MISSING_MODULE` | REFERENCES | error | `import`, `tried[]` |
| `UNDEFINED_NAME` | REFERENCES | warn | `name` |
| `RULE_VIOLATION` | QUALITY | per rule | `snippet`, rule-specific (`value`, `max`) |
| `WRITE_FAILED` | WRITE | error | `errno`, `step` |
| `DB_UNAVAILABLE` | WRITE | error | `host`, `port` |
| `CHANGESET_CONFLICT` | WRITE (abandon) | error | `files[]` changed outside the changeset since its last write |

**Deferrable codes** (become obligations inside a changeset, §6.14): `CALLER_INCOMPATIBLE`, `REMOVED_SYMBOL_IN_USE`, `BROKEN_IMPORT`, `CALL_INCOMPATIBLE`, `MISSING_SYMBOL`, `MISSING_MODULE`. Everything else keeps its severity in every mode.

## 6.11 Pitfall & lesson integration

On every `REJECTED` result (except `WRITE_FAILED`, `DB_UNAVAILABLE`, `STALE_BASE` — environmental, not the agent's mistake):
- For each error violation → upsert a `Pitfall` keyed by `key = xxh3_hex(code + "|" + (symbol_id or file) + "|" + (rule_id or ""))` (§07.4). Increment `count`, set `last_seen`, keep the first `lesson` text unless the new one is longer.
- `lesson` templates (MUST be generated, not free text from the agent):

| Code | Lesson template |
|---|---|
| `CALLER_INCOMPATIBLE` | "Changing the signature of `{callee}` breaks {n} caller(s) ({files}). Update callers in the same propose_patch or keep the change backward-compatible (defaults)." |
| `REMOVED_SYMBOL_IN_USE` | "`{callee}` is still called from {files}; migrate callers before removing it." |
| `MISSING_SYMBOL` | "`{module}` has no `{name}`. Existing similar names: {did_you_mean}. Check the outline before calling helpers." |
| `CALL_INCOMPATIBLE` | "`{callee}` has signature `{signature}`; calls must match it ({reason})." |
| `SYNTAX_ERROR` | "Patches to `{file}` failed syntax at {line}:{col} ({kind} {expected}). Re-check brackets/indentation in generated code." |
| `RULE_VIOLATION` | "{rule message} (rule `{rule_id}`)." |
| `BROKEN_IMPORT` / `MISSING_MODULE` | "Import `{name}` from `{module}` is broken by this change." |
| others | message |

- `PatchResult.lessons` = up to 5 existing Pitfalls (count ≥ 1) about any symbol in the patch's targets or any file in the WorkingSet, ordered by `count DESC, last_seen DESC` — returned on **both** APPLIED and REJECTED results so the agent sees history at the moment it matters.
- On `APPLIED`: for each Pitfall about a symbol in this patch, set `last_resolved = now`, `resolved_count += 1` (the agent fixed it).

## 6.12 Gate modes (`config.gate.mode`)

| Mode | Behavior |
|---|---|
| `enforce` (default) | As specified. |
| `advisory` | All violations downgraded to warnings; everything is applied (still journaled + recorded). For gradual adoption. |

## 6.13 Stretch: P7 post-check command (only if time remains)

`config.gate.post_check = {"cmd": ["python","-m","pytest","-x","-q"], "timeout_s": 60, "on_fail": "rollback"}`. Runs after P6 step 7 with cwd = root; on non-zero exit and `on_fail=rollback`, restores from WAL backups (same as recovery `RENAMING`) and returns `REJECTED` with `POST_CHECK_FAILED` (`evidence.output_tail` = last 60 lines). Not part of MVP acceptance.

## 6.14 Changesets — incremental and greenfield work

### 6.14.1 Why

Strict mode requires every patch to leave the workspace consistent. That is right for edits to existing code, but wrong for **building**: an agent scaffolding a feature writes `api/routes.py` (calling `services.create_order`) before `services.py` exists, or changes a signature in step 1 and migrates 30 callers over steps 2–6, and wants to *run* the half-built code in between. A changeset lets the gate adapt to that workflow without giving up its guarantees: consistency checks still run on every patch, but their findings become **obligations** that must be cleared before the work is declared done.

Single-patch greenfield work needs no changeset: a `propose_patch` with many `create_file` edits whose files reference each other already passes, because P4 evaluates the complete post-state.

### 6.14.2 Lifecycle

```
changeset{action:"begin", intent} ─► OPEN ─(propose_patch …)─► OPEN ─ changeset{action:"finalize"} ─► CLOSED   (0 obligations)
                                     │                               └─ obligations remain → stays OPEN, list returned
                                     └─ changeset{action:"abandon"} ─► ABANDONED (every touched file restored to its pre-changeset bytes)
```
- At most **one OPEN changeset per workspace**. `begin` while one is open → tool error `CHANGESET_OPEN` (data: its id, intent, obligation count) — continue it instead.
- While a changeset is OPEN, **every** `propose_patch` belongs to it (implicit — the agent can't forget an id).
- Changesets outlive sessions and restarts (state in FalkorDB + snapshot on disk). The next session's briefing shows it under *Open changeset*; any session or agent can continue it — this is the cross-session **handoff** primitive (Track 2: shared state & handoffs).
- Humans can force via CLI: `graphstate changeset close --force <id>` (remaining obligations are recorded as Pitfalls with code `OBLIGATION_FORCED_OPEN`) and `graphstate changeset abandon --force <id>` (skips the conflict check). Agents have no force option.

### 6.14.3 Check behavior inside a changeset

| Check | Strict mode | Changeset mode |
|---|---|---|
| P0 preflight, P1 edit errors, `STALE_BASE` | reject | reject |
| P2 syntax | reject | reject (never write unparsable code) |
| P5 rules with severity `error` | reject | reject (local to the code being written — fix now) |
| Deferrable codes (§6.10) | reject | **APPLIED + tracked as obligations** |
| Warnings | returned | returned |

### 6.14.4 Obligations = P4 of the cumulative diff

Obligations are not accumulated patch by patch (that goes stale). They are **recomputed** with `gate::audit_workspace(base, current)` (`src/gate/audit.cpp`, shared with `graphstate audit` §13.4): run P3+P4 as if the whole changeset were *one* patch from the **base snapshot** (each touched file's content before the changeset first touched it) to the **current disk state** of every touched file. Consequences:
- An obligation disappears automatically when a later patch creates the missing symbol or fixes the caller (reported in `discharged`).
- External edits to changeset files (watcher) trigger recomputation too.
- `finalize` = recompute; empty → CLOSED.

Triggers: after each APPLIED patch, after the watcher re-indexes a changeset file, on `status`/`finalize`, on startup (OPEN changeset).
Cost: base facts are parsed once from the snapshot and cached in RAM; recomputation is P3+P4 over the touched files — ms-scale for tens of files.

### 6.14.5 Snapshot (rollback without git)

```
.graphstate/changesets/<id>/
├── manifest.json     # {id, intent, created_at, status, files: {path: {existed, base_hash, last_written_hash}}, created_dirs: []}
└── base/<path>       # byte-exact copy of each touched file as it was before the changeset first touched it
```
- First write to a path within the changeset (P6, before the WAL rename): `platform::hardlink_or_copy(root/path, base/path)` (the rename replaces the directory entry, so a hard link keeps the old bytes); record `existed` / `base_hash`. Created files: `existed=false`.
- After every write: update `last_written_hash` (manifest written via tmp + replace).
- **abandon**: for each file, if current disk hash ≠ `last_written_hash` → conflict (edited after the changeset wrote it) → abort the whole abandon with `CHANGESET_CONFLICT` listing the files (never lose someone's edits). Otherwise restore through the normal WAL-protected DiskTxn: `existed` → write base bytes back; `!existed` → delete; remove empty `created_dirs`. Re-index; status ABANDONED; record a Decision with `status:"REVERTED"`.
- CLOSED / ABANDONED → snapshot directory deleted.

### 6.14.6 Data model (`src/gate/changesets.hpp/.cpp`)

```cpp
struct Obligation {
  std::string id;          // xxh3 of code|file|symbol|evidence-key (stable across recomputes → "discharged" detection)
  Violation violation;     // code ∈ deferrable set, with location / evidence / fix_hint
};
struct ChangesetState {
  std::string id;          // "c_" + 12 hex
  std::string intent;
  std::string status;      // OPEN | CLOSED | ABANDONED
  std::vector<std::string> files;          // touched so far
  std::vector<Obligation> obligations;     // current (after recompute)
  std::vector<std::string> discharged;     // obligation ids cleared since the previous recompute
};
class Changesets {
 public:
  explicit Changesets(Engine&);
  void load_on_startup();                                   // OPEN changeset: graph + manifest → base facts → recompute
  ChangesetState begin(std::string_view intent, const SessionCtx&);
  std::optional<ChangesetState> current() const;
  void before_write(const std::vector<std::string>& paths); // snapshot first-touch files (called by DiskTxn)
  void after_write(const std::vector<std::pair<std::string, std::string>>& path_hashes);
  ChangesetState recompute();                               // §6.14.4; persists via Q_OBLIGATIONS_REPLACE
  ChangesetState finalize(const SessionCtx&);
  ChangesetState abandon(const SessionCtx&, bool force = false);
};
```
Graph: `(:Changeset)` and `(:Obligation)`, see §04.8 *Changesets*. Obligations do **not** create Pitfalls (they are planned work, not mistakes) — only `OBLIGATION_FORCED_OPEN` does.

### 6.14.7 Results while a changeset is OPEN

`propose_patch` returns `status:"APPLIED"` (or `REJECTED` for blocking checks) plus:
```json
"changeset": {"id":"c_4e5f6a7b8c9d","intent":"Build order API","status":"OPEN","files":["api/routes.py","services.py"],
              "open_obligations":3,
              "obligations":[{"id":"…","code":"MISSING_SYMBOL","message":"api/routes.py:14 calls services.create_order, which does not exist yet.",
                              "location":{"file":"api/routes.py","line":14,"col":12},"fix_hint":"Create services.create_order(...) or change the call."}],
              "discharged":["…"]}
```
`obligations` lists at most 20 (sorted by file, line); the full list comes from `changeset{action:"status"}`.

### 6.14.8 Nudging agents into the right mode

When a strict-mode patch is REJECTED **only** for deferrable codes and every violation location is in a file created or modified during the current session, the result carries
`"suggestion": "This looks like incremental building. Open a changeset (changeset {action:'begin', intent}) to apply now and track these as obligations, or include the missing pieces in this patch."`

## 6.15 Relationship to version control

- GraphState is **VCS-agnostic** and never runs `git add/commit/stash/checkout/reset` or any other write command. Git is only *read* (`git ls-files` for scanning, §05.3). `.git/**` is a protected path.
- `APPLIED` = written to the working tree. Users (or the agent through its own shell tools) commit with git as usual; GraphState neither requires nor prevents it.
- Without git, the safety nets are the gate, the WAL (no half-written patches), and changesets (`abandon` = multi-step undo).
- **VCS operations** (`git checkout`, `pull`, `stash pop`, branch switch) change many files outside the gate. The watcher treats a tick as a *bulk external change* when **more than `watch.bulk_threshold` (default 20) files** changed **or** `.git/HEAD` / `.git/index` mtime changed during the tick: it re-indexes everything changed, records **one** `Decision{actor:"external", status:"SYNCED", files}` (no per-file audit, no UNGATED findings, no Pitfalls), and pushes one notice `{"kind":"bulk_change","files":N,"likely":"vcs"}`. An OPEN changeset gets its obligations recomputed; its abandon conflict guard protects the newly checked-out content.
- Upgrade path (not MVP): `gate.on_apply_cmd` (e.g. `["git","add","--"]` + paths) run after each APPLIED patch; single-patch undo by retaining the last N WAL backups.
