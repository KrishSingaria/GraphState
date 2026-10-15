# 05 — Parsing, Extraction, Resolution & Indexing

> Index: [README.md](README.md). Code: `src/lang/*`, `src/index/*`. Tree-sitter core **v0.27.1**; grammar node/field names verified against `src/node-types.json` of the pinned grammar tags.

## 5.1 Pipeline overview

```
Scanner ──files──► Parser pool (N threads) ──trees──► Extractor ──FileFacts──► FactStore
                                                                                 │
                                                       Resolver ◄────────────────┘
                                                          │ (imports → File edges, calls → Function edges)
                                                          ▼
                                                     GraphWriter ──MULTI/EXEC batches──► FalkorDB
```

Facts are pure data (no tree-sitter handles), so trees are freed right after extraction.

## 5.2 Languages (`lang/language.hpp`)

```cpp
namespace gs::lang {
enum class Language { Python, TypeScript, Tsx, JavaScript, Go, Rust, Java, C, Cpp, Json, Unknown };

struct LanguageInfo {
  Language lang; std::string_view name;          // "python"
  const TSLanguage* ts = nullptr;                // nullptr for Unknown
  bool semantic = false;                         // has an Extractor
  std::vector<std::string_view> extensions;
};

class LanguageRegistry {
 public:
  static const LanguageRegistry& get();          // built once (function-local static)
  const LanguageInfo& for_path(std::string_view rel_path) const;  // by extension, lowercase
  const LanguageInfo& info(Language) const;
  const TSQuery* rule_query(Language, std::string_view rule_id) const; // compiled built-in rule queries
};
}
extern "C" {
const TSLanguage* tree_sitter_python(void);  const TSLanguage* tree_sitter_typescript(void);
const TSLanguage* tree_sitter_tsx(void);     const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_go(void);      const TSLanguage* tree_sitter_rust(void);
const TSLanguage* tree_sitter_java(void);    const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_json(void);
#ifdef GS_WITH_CPP_GRAMMAR
const TSLanguage* tree_sitter_cpp(void);
#endif
}
```

| Language | Extensions | Semantic | Grammar ABI |
|---|---|---|---|
| Python | `.py` `.pyi` | ✔ | 15 |
| TypeScript | `.ts` `.mts` `.cts` (`.d.ts` indexed, read-only: gate refuses symbol ops) | ✔ | 14 |
| TSX | `.tsx` | ✔ | 14 |
| JavaScript | `.js` `.mjs` `.cjs` `.jsx` | ✔ | 15 |
| Go | `.go` | syntax | 15 |
| Rust | `.rs` | syntax | 15 |
| Java | `.java` | syntax | 14 |
| C | `.c` `.h` | syntax | 15 |
| C++ | `.cc` `.cpp` `.cxx` `.hpp` `.hh` `.hxx` | syntax | 14 |
| JSON | `.json` | syntax | — |

Startup check: for each grammar, `ts_language_abi_version(lang)` MUST be in `[13, 15]`; else log ERROR and disable that language (`doctor` shows ✖).

## 5.3 Scanner (`index/scanner.cpp`)

```cpp
struct ScannedFile { std::string rel; uint64_t size; int64_t mtime_ns; };
std::vector<ScannedFile> scan(const Config&, const std::filesystem::path& root);
```
1. If `index.use_git_ls_files` and `root/.git` exists and `git` is on PATH: run `git -C <root> ls-files -co --exclude-standard -z` (via `platform::run_capture`, timeout 10 s; read stdout, split on `\0`; git prints `/` paths on every OS). This honours `.gitignore` correctly at zero implementation cost.
2. Else: recursive `std::filesystem::recursive_directory_iterator` (no symlink following), skipping directories that match `index.ignore`.
3. In both cases apply `index.ignore` globs and drop: files > `max_file_bytes` (still recorded as `File{skipped_reason:"too_large"}`), files whose first 8 KiB contain a NUL byte (`binary`), non-regular files.
4. Only files whose language is not `Unknown` are parsed; `Unknown` files are recorded as `File` nodes (so the gate knows about them) but get no symbols.

Paths are converted with `platform::to_utf8`; ids use `/` and on-disk casing; on case-insensitive file systems FactStore also keeps a lowercase-id map (§12.6).

Glob semantics (`core/paths.cpp`, `glob_match(pattern, rel_path, case_insensitive)`): `**` matches any number of path segments (including zero), `*` any chars except `/`, `?` one char except `/`, `{a,b}` alternation. Patterns are matched against the `/`-separated relative path. Unit-tested.

## 5.4 Facts (`lang/facts.hpp`)

```cpp
namespace gs::lang {
struct ByteRange { uint32_t start = 0, end = 0; };
struct LineRange { uint32_t start = 0, end = 0; };          // 1-based inclusive

enum class ParamKind { PositionalOnly, PositionalOrKeyword, VarPositional, KeywordOnly, VarKeyword };
struct Param {
  std::string name;                   // "<destructured>" for TS/JS patterns
  ParamKind kind = ParamKind::PositionalOrKeyword;
  bool has_default = false;
  bool optional = false;              // TS `?`
  std::string type;                   // annotation text, whitespace-collapsed, "" if none
};
struct Signature {                    // see §06.7.4 for check semantics
  std::vector<Param> params;
  bool is_async = false;
  std::string return_type;
  bool drop_first_for_instance_call = false;
  bool has_overloads = false;
  std::string canonical() const;      // §5.6
  nlohmann::json to_json() const;     // params_json
  int required_count() const;         // excludes dropped first param
  int max_count() const;              // -1 if VarPositional
};

enum class SymbolKind { Function, Method, Constructor, Arrow, Module, Class, Interface };
struct SymbolFact {
  std::string id, name, qualname;     // id = file + "::" + qualname (+ "#n" for duplicates)
  SymbolKind kind;
  std::string class_qualname;         // owner for methods
  Signature sig;                      // functions only
  std::vector<std::string> bases;     // classes only (as written)
  std::vector<std::string> decorators;
  bool is_exported = false, is_static = false;
  ByteRange def_range;                // definition range (§06.4.1)
  ByteRange body_range;
  LineRange lines;
  uint32_t indent_col = 0;            // byte column of def_range.start
  std::string body_hash;              // xxh3, whitespace-collapsed body
  std::string doc;                    // first docstring line ≤160
  int complexity = 1, max_nesting = 0, loc = 0;
  std::vector<std::string> locals;    // Python: names bound inside (params, assignments, nested defs, imports, for/with/except targets, comprehension vars, global/nonlocal)
};

enum class CalleeShape { Name, Attr, SelfAttr, ClsAttr, SuperAttr, New, Other };
struct CallFact {
  std::string caller_id;              // enclosing indexed symbol id (or <module>)
  CalleeShape shape;
  std::string name;                   // final identifier: foo in foo(), x.foo(), self.foo()
  std::string receiver;               // "x" for x.foo(); dotted for a.b.foo() → "a.b"; "" otherwise
  uint32_t line, col;                 // 1-based
  uint32_t start_byte;
  int pos_args = 0;
  std::vector<std::string> kw_names;
  bool has_star = false, has_dstar = false;   // *x / **x in Python; spread in TS/JS → has_star
  std::string text;                   // call text, single-line, ≤120 chars ("…" if cut)
};

struct ImportFact {
  enum Kind { Module, FromNames, TsNamed, TsDefault, TsNamespace, TsSideEffect, ReExport } kind;
  std::string spec;                   // python: "pkg.billing" / ".billing" / "..utils" ; ts: "./billing"
  std::vector<std::pair<std::string,std::string>> names;  // (imported name, local alias) ; Module: (module, alias)
  uint32_t line;
  bool is_type_only = false;          // TS `import type`
};

struct SyntaxError { uint32_t line, col, start_byte; bool missing; std::string expected; };

struct FileFacts {
  std::string file, hash;
  Language lang;
  uint64_t size = 0; int64_t mtime_ns = 0; uint32_t line_count = 0;
  bool parse_ok = true;
  std::vector<SyntaxError> syntax_errors;      // capped at 50
  std::vector<SymbolFact> symbols;             // includes <module> pseudo-symbol for semantic langs
  std::vector<CallFact> calls;
  std::vector<ImportFact> imports;
  std::vector<std::string> module_names;       // top-level names bound in the module (defs, classes, assignments, imports)
  std::vector<std::string> exports;            // TS/JS exported names (incl. re-exports); Python = public top-level names
  std::string skipped_reason;
  const SymbolFact* find(std::string_view qualname_or_id) const;
};

class Extractor {
 public:
  virtual ~Extractor() = default;
  virtual FileFacts extract(const std::string& file, const std::string& src, TSTree* tree) const = 0;
};
std::unique_ptr<Extractor> make_extractor(Language);   // nullptr for syntax-only
FileFacts extract_any(const std::string& file, const std::string& src, Language); // parse + extract or syntax-only
}
```

Syntax-only languages get `FileFacts` with only `file, hash, lang, size, line_count, parse_ok, syntax_errors` (no symbols).

## 5.5 Extractors — normative AST walks

General rules:
- Walk with `ts_node_child`/`ts_node_named_child` over the node types listed; use `ts_node_child_by_field_name(node, "name", 4)` for fields.
- Node text = `src.substr(start_byte, end_byte - start_byte)`.
- Every semantic file gets one `<module>` symbol: `kind=Module`, `def_range = [0, size)`, owns calls that occur outside any indexed function/class method.
- Nested functions (a `def` inside a `def`) are **not** separate symbols: their calls are attributed to the nearest indexed ancestor, and their names go into that ancestor's `locals`.
- Nested classes ARE symbols (`Outer.Inner`), their methods `Outer.Inner.m`.
- Duplicate qualnames: see §04.5.

### 5.5.1 Python (`python_extractor.cpp`, grammar v0.25.0)

**Module level** — iterate named children of `module`:

| Node | Action |
|---|---|
| `function_definition` | function symbol (§ below) |
| `decorated_definition` | read `decorator` children → `decorators` (text after `@`, up to first `(`, whitespace-stripped); field `definition` → function or class; `def_range` = the decorated_definition node |
| `class_definition` | class symbol; field `superclasses` (`argument_list`) → each positional child text → `bases` (keyword args like `metaclass=` ignored); field `body` (`block`) → members |
| `import_statement` | each `name` field child: `dotted_name` → `ImportFact{Module, spec=text, names=[(text, first segment)]}`; `aliased_import` → `names=[(name, alias)]` |
| `import_from_statement` | field `module_name` (`dotted_name` or `relative_import` text, e.g. `..pkg`) → spec; each `name` field child (`dotted_name` / `aliased_import`) → names; a `wildcard_import` child → `names=[("*","*")]` |
| `expression_statement` → `assignment` (field `left` is `identifier` / `pattern_list`) | add identifiers to `module_names`; scan the right side for calls → `<module>` |
| `if_statement`, `try_statement`, `with_statement` at top level | recurse into their blocks with the same module-level rules (handles `if TYPE_CHECKING:` and try/except imports) |
| anything else | collect calls → `<module>` |

**Class body** (`block` of a class): `function_definition`/`decorated_definition` → method (qualname `Class.name`); `class_definition` → nested class; assignments → ignored (class attributes); calls in other class-body statements are attributed to `<module>` (class bodies execute at import time).

**Function symbol**:
- `name` field → name. `kind`: in class → `Method`, `__init__` → `Constructor`, else `Function`.
- `is_async`: the `function_definition` node has an `async` child token (check first child type == `"async"`).
- `return_type` field → text.
- `body` field → `body_range`; docstring = first statement if `expression_statement` → `string`; first line, quotes stripped.
- `is_static`: decorators contains `staticmethod`. `drop_first_for_instance_call = (kind ∈ {Method, Constructor}) && !is_static`.
- `is_exported = !name.starts_with("_") || (name.starts_with("__") && name.ends_with("__"))`.

**Parameters** (`parameters` field) — iterate children in order, state `after_star = false`:

| Child | Param |
|---|---|
| `identifier` | `{name, PositionalOrKeyword (KeywordOnly if after_star)}` |
| `typed_parameter` | name = its first named child that is `identifier` (if that child is `list_splat_pattern` → VarPositional; `dictionary_splat_pattern` → VarKeyword, name = inner identifier); `type` field |
| `default_parameter` | `name` field, `value` field → `has_default=true`; kind as identifier rule |
| `typed_default_parameter` | `name`, `type`, `value` |
| `list_splat_pattern` | `VarPositional`, name = inner identifier; set `after_star = true` |
| `dictionary_splat_pattern` | `VarKeyword` |
| `keyword_separator` (bare `*`) | `after_star = true` (no param) |
| `positional_separator` (`/`) | mark all params so far as `PositionalOnly` |
| `tuple_pattern` | skip (py2) |

**Locals** (for UNDEFINED_NAME, §06.7.3): walk the function body (including nested defs/lambdas/comprehensions) and collect: params; `assignment`/`augmented_assignment` left identifiers (recurse into `pattern_list`/`tuple_pattern`/`list_pattern`); `for_statement` left; `with_item` → `as_pattern` alias; `except_clause` alias (`as` identifier); nested `function_definition`/`class_definition` names; `import_*` names inside the function; `for_in_clause` left in comprehensions; `named_expression` (walrus) name; `global_statement`/`nonlocal_statement` identifiers; `lambda_parameters`.

**Calls** — every `call` node in the symbol's subtree (excluding subtrees of nested *class* definitions, which are their own symbols):
- `function` field:
  - `identifier` → `Name`, name = text.
  - `attribute` → name = `attribute` field text; object = `object` field:
    - `identifier` `self` → `SelfAttr`; `cls` → `ClsAttr`;
    - `call` whose function is `identifier` `super` → `SuperAttr`;
    - `identifier`/`attribute` (dotted) → `Attr`, receiver = object text (dotted, whitespace stripped);
    - else → `Other`.
  - else → `Other` (still recorded; never resolved).
- `arguments` field: `argument_list` → for each named child: `keyword_argument` → `kw_names += name`; `list_splat` → `has_star`; `dictionary_splat` → `has_dstar`; anything else → `pos_args++`. If the arguments field is a `generator_expression` → `pos_args = 1`.

### 5.5.2 TypeScript / TSX (`ts_extractor.cpp`, grammar v0.23.2)

**Program level** — iterate named children of `program`; unwrap `export_statement` (field `declaration` → process it with `is_exported=true`; `default` keyword child → name `default` if the declaration has no name; `export_clause` → `exports += specifier names`; `source` field present → `ImportFact{ReExport}`):

| Node | Action |
|---|---|
| `function_declaration` / `generator_function_declaration` | function symbol |
| `function_signature` (no body) | record overload: the next `function_declaration` with the same name gets `has_overloads=true`; not a symbol itself |
| `class_declaration` / `abstract_class_declaration` | class symbol; heritage: `class_heritage` → `extends_clause` (field `value`) texts → `bases`; `implements_clause` ignored |
| `interface_declaration` | `Interface` symbol (no methods indexed) |
| `lexical_declaration` / `variable_declaration` → `variable_declarator` with field `value` of type `arrow_function` / `function_expression` | function symbol `kind=Arrow`, name = declarator `name`, def_range = the whole declaration (or export_statement) |
| other `variable_declarator` | name → `module_names` |
| `import_statement` | `source` field (string; strip quotes) → spec. Children: `import_clause` → `identifier` (default import → `TsDefault`), `named_imports` → each `import_specifier` (`name`, `alias`) → `TsNamed`, `namespace_import` → identifier → `TsNamespace`. No clause → `TsSideEffect`. A `type` keyword child → `is_type_only` |
| anything else | calls → `<module>` |

**Class body** (`class_body`): `method_definition` (name field: `property_identifier` → text; `private_property_identifier` → text incl. `#`; computed/string names → text) → method; name `constructor` → `Constructor`; `static` child → `is_static`; `method_signature`/`abstract_method_signature` → overload marker for the following implementation; `public_field_definition` whose `value` is `arrow_function` → method `kind=Arrow`.
`drop_first_for_instance_call = false` for all TS/JS (no explicit self).

**Parameters** (`formal_parameters`):

| Child | Param |
|---|---|
| `required_parameter` | `pattern` field: `identifier` → name; `rest_pattern` → `VarPositional` (name = inner identifier); `this` → skip param entirely; object/array pattern → name `<destructured>`. `value` field → `has_default`. `type` field → type (strip leading `:`) |
| `optional_parameter` | same + `optional=true` |

Arrow functions: field `parameter` (single bare identifier) → one param; else field `parameters`.

**Calls**: `call_expression`: field `function`: `identifier` → `Name`; `member_expression` → name = `property` field text, `object` field: `this` → `SelfAttr`; `super` → `SuperAttr`; identifier/member chain → `Attr`, receiver text; else `Other`. Field `arguments` (`arguments` node): named children: `spread_element` → `has_star`; others → `pos_args++`.
`new_expression`: field `constructor`: identifier → `New`, name; member_expression → `New`, receiver+name. Field `arguments` as above (absent → 0 args).
Optional chaining `a?.b()` parses as member_expression with `?.` — treat the same.

### 5.5.3 JavaScript (`js_extractor.cpp`, grammar v0.25.0)

Same structure as TS (shared helpers in `ts_common.cpp`) with these differences:
- No `function_signature`, interfaces, or type annotations.
- `formal_parameters` children: `identifier` → param; `assignment_pattern` (fields `left`, `right`) → `has_default`, name from `left`; `rest_pattern` → `VarPositional`; `object_pattern`/`array_pattern` → `<destructured>`.
- CommonJS: `const x = require('./m')` → `ImportFact{TsNamespace, spec='./m', names=[("*", "x")]}`; `const {a, b} = require('./m')` → `TsNamed`. `module.exports = {…}`/`exports.f = …` → add names to `exports` (best effort).

## 5.6 Signature canonical form

`Signature::canonical()` — used for P3 diffing (exact compare) and display:
```
<name>(<p1>, <p2>, …) [-> <return_type>]
param  := [*|**]name[?][: type][ = …]
python separators: "/" after positional-only params, "*" before keyword-only when no *args
async prefix: "async "
```
Examples: `calculate_total(cart: Cart, tax_rate: float = …, *, currency: str) -> Decimal`; `fetchUser(id: string, opts?: Options) -> Promise<User>`.
Default **values** are not part of the canonical form (only presence), so changing `= 0.1` to `= 0.2` is a body change, not a signature change. Whitespace inside types is collapsed to single spaces.

## 5.7 Resolver (`index/resolver.cpp`)

### 5.7.1 Tables (rebuilt incrementally)

```cpp
class Resolver {
 public:
  explicit Resolver(const FactStore&, const Config&);
  // Module resolution
  std::optional<std::string> resolve_module(const std::string& from_file, const ImportFact&, std::string_view spec) const;
  // Call resolution for one file; uses current FactStore (or an overlay for the gate's post-state)
  std::vector<ResolvedCall> resolve_calls(const FileFacts&, const FactOverlay* overlay = nullptr) const;
  std::vector<ResolvedImport> resolve_imports(const FileFacts&, const FactOverlay* overlay = nullptr) const;
  std::vector<std::string> dependents_of(const std::string& file) const;   // files importing `file` (reverse import map)
  void on_files_changed(const std::vector<std::string>& files);           // refresh reverse maps & name index
};
struct ResolvedCall { const CallFact* call; std::string callee_id; std::string confidence; std::string call_kind; };
struct ResolvedImport { std::string target_file; std::vector<std::string> names; bool whole_module; std::string alias; uint32_t line; };
struct FactOverlay { std::map<std::string, const FileFacts*> files; std::set<std::string> deleted; }; // gate post-state
```
Name index: `method_name → [symbol ids]` and `top_level_name → [symbol ids]` across the workspace (for heuristics).

### 5.7.2 Module resolution

**Python** `resolve_module(from, spec)`:
- Absolute `a.b.c`: for each `root_dir ∈ python.source_roots` (default `""`, `"src"`): try `root_dir/a/b/c.py`, then `root_dir/a/b/c/__init__.py`. First existing (in FactStore/overlay) wins.
- Relative (`.`×k + rest): base = directory of `from`, go up `k-1` directories; then `rest` as path (`.billing` from `pkg/app.py` → `pkg/billing.py` or `pkg/billing/__init__.py`; `.` alone → `pkg/__init__.py`).
- `from pkg import name`: first try module `pkg.name` (submodule import → `whole_module=true`, alias = local name); else target `pkg` with `names=[name]`.
- Not found → external (no edge, never an error **unless** relative → `MISSING_MODULE` in gate).

**TS/JS** spec starting with `./` or `../` (or `/` relative to root):
- base = normalize(dir(from) / spec). Try in order: `base`, `base.ts`, `base.tsx`, `base.d.ts`, `base.js`, `base.jsx`, `base.mjs`, `base.cjs`, `base/index.ts`, `base/index.tsx`, `base/index.js`. If spec ends with `.js`/`.jsx`/`.mjs`, also try replacing that extension with `.ts`/`.tsx`/`.mts` (ESM-in-TS convention).
- Bare specifiers (`react`, `@scope/x`) → external.

### 5.7.3 Call resolution (per CallFact in file F, caller symbol S)

Build per-file context: `imports_by_local_name` (local alias → (target file, imported name | whole module)), F's `module_names`, F's symbols by qualname.

| Shape | Rule (first match wins) | confidence / call_kind |
|---|---|---|
| `Name` (Python) | name ∈ S.locals and not a module-level def → **skip** (local closure/variable) | — |
| | name is a top-level function in F → that id | exact / plain |
| | name is a top-level class in F → class `__init__` (search class, then resolved bases depth-first) → id | exact / constructor (no `__init__` → no edge) |
| | name is imported `from M import name [as alias]` and M is a workspace file → symbol `M::name` (function → plain; class → constructor rule) | exact |
| | builtin / unknown → skip | — |
| `Name` (TS/JS) | top-level function/arrow in F → exact; imported named/default from workspace module → that module's export (default → symbol with `is_exported && name=="default"` or the `export default` target) → exact | plain |
| `New` (TS/JS) | class resolved as above → its `constructor` method (search class, then bases) | exact / constructor |
| `Attr` with receiver = a whole-module import alias (`import billing`, `import pkg.billing as b`, TS namespace import, CommonJS require) | `M::name` | exact / plain |
| `Attr` with receiver = a class name (local or imported) | method `C.name`: decorators has `staticmethod` → plain; `classmethod` → classmethod (drop first); else unbound | exact |
| `SelfAttr` / `ClsAttr` (Python), `SelfAttr` (`this`, TS/JS) inside class C | method `name` in C, else in resolved bases (depth-first, left-to-right) | exact / instance (ClsAttr → classmethod) |
| `SuperAttr` | method `name` in C's first resolved base chain | exact / instance |
| `Attr`/`Other` with unknown receiver | **heuristic**: if exactly one workspace *method or function* is named `name`, `len(name) ≥ 4`, and `name ∉ COMMON_NAMES` → that id | heuristic / instance |
| otherwise | unresolved (no edge) | — |

`COMMON_NAMES` = `get set add run update close open read write save load append extend pop items keys values copy clear find format join split strip replace send start stop init main call apply map filter reduce push then catch next emit log info warn error debug render handle process execute parse build create delete remove insert reset test`.

Class base resolution: each `bases` entry resolves like a `Name`/`Attr` expression to a workspace `Class` id (`INHERITS` edge confidence `exact`), else ignored.

### 5.7.4 Incremental re-resolution

When files `C` change (gate write, watcher, reconcile):
1. Update FactStore for `C`; `Resolver::on_files_changed(C)`.
2. `affected = C ∪ dependents_of(C) ∪ {files with heuristic edges to symbols whose name was added/removed in C}` (the last set via the name index; cap 200 files, else full re-resolve).
3. For each file in `affected`: `resolve_imports` + `resolve_calls` → rows.
4. GraphWriter: transaction 1 (file sync for `C`), transaction 2 (`Q_CALLS_CLEAR_FROM_FILES(affected)` + `Q_CALLS_CREATE(rows)` + `Q_STRUCT_EDGES` for affected imports) + relinks.

## 5.8 Metrics (`lang/metrics.cpp`)

Computed over the symbol's `body_range` subtree, **excluding** nested function/class definitions (they are not separate symbols in Python but their complexity should not inflate the parent — ponytail: exclude nested `def` bodies entirely).

| Metric | Python decision nodes | TS/JS decision nodes |
|---|---|---|
| Cyclomatic = 1 + count | `if_statement`, `elif_clause`, `for_statement`, `while_statement`, `except_clause`, `conditional_expression`, `boolean_operator` (each), `case_clause`, `for_in_clause`, `if_clause` (comprehension) | `if_statement`, `for_statement`, `for_in_statement`, `while_statement`, `do_statement`, `switch_case`, `catch_clause`, `ternary_expression`, `binary_expression` with operator `&&`, `||`, `??` |
| Nesting depth (max) | depth increments entering: `if_statement`, `for_statement`, `while_statement`, `try_statement`, `with_statement`, `match_statement` | `if_statement`, `for_statement`, `for_in_statement`, `while_statement`, `do_statement`, `try_statement`, `switch_statement` |
| `loc` | `lines.end - lines.start + 1` | same |

`elif_clause`/`else if` chains count +1 each for complexity but do NOT increase nesting.

## 5.9 Python builtins (`lang/py_builtins.inc`)

Generated once by `tools/gen_py_builtins.py` (runs `python3 -c "import builtins; print(sorted(dir(builtins)))"` with Python 3.13, writes a sorted `constexpr std::array<std::string_view, N>`) and committed. Also treat as defined: `__name__ __file__ __doc__ __package__ __spec__ __builtins__ __debug__ reveal_type`.

## 5.10 FactStore (`index/fact_store.hpp`)

```cpp
class FactStore {
 public:
  const FileFacts* get(std::string_view file) const;
  void put(FileFacts f);                     // replace
  void erase(std::string_view file);
  std::vector<std::string> files() const;
  // symbol lookup
  const SymbolFact* symbol(std::string_view id) const;
  std::vector<const SymbolFact*> find_symbols(std::string_view query, size_t limit) const; // id | qualname | name, exact first
  bool is_stale(std::string_view file) const;   // stat() differs from stored (size, mtime_ns) → caller re-hashes
 private:
  std::unordered_map<std::string, FileFacts> files_;
  std::unordered_map<std::string, std::pair<std::string,size_t>> symbol_index_;  // id → (file, idx)
  std::unordered_multimap<std::string, std::string> by_name_, by_qualname_;
};
```
Freshness rule ("lazy check"): every tool that touches a file calls `Indexer::ensure_fresh(file)`: if `is_stale` → read + hash; if hash differs → reindex that file synchronously (audit as external change, §2.5.3).

## 5.11 Indexer (`index/indexer.hpp`)

```cpp
struct IndexReport { size_t scanned=0, parsed=0, changed=0, deleted=0, skipped=0; double ms=0; };
class Indexer {
 public:
  Indexer(Engine&);
  IndexReport startup_reconcile();                            // §2.3 steps 6–8
  IndexReport full_rebuild();                                 // Q_CODE_WIPE + startup_reconcile
  IndexReport reindex(const std::vector<std::string>& files, std::string_view actor); // watcher / reindex tool / ensure_fresh
  void apply_written(const std::vector<FileFacts>& post, const std::vector<std::string>& deleted); // gate P6
  bool ensure_fresh(const std::string& file);                 // returns true if it re-indexed
};
```
`startup_reconcile` algorithm:
1. `scan()` → list. Parse all semantic + syntax-only files in parallel: split list into N chunks; each worker has a `thread_local TSParser`; outputs `FileFacts` into a pre-sized vector (no locks).
2. `graph_hashes = Q_FILE_HASHES`. `changed = {f | f ∉ graph_hashes or hash differs}`; `deleted = graph_hashes.keys − scanned`.
3. FactStore ← all facts; Resolver rebuild (full).
4. GraphWriter: delete `deleted`; sync `changed` in batches of 50 files/transaction; then calls for `changed ∪ dependents(changed ∪ deleted)` (on first-ever index that's everything).
5. `Q_META_TOUCH_INDEXED`.

## 5.12 GraphWriter (`index/graph_writer.hpp`)

```cpp
class GraphWriter {
 public:
  GraphWriter(db::GraphClient&, std::string graph);
  void sync_files(const std::vector<const FileFacts*>& files, int64_t now);   // transaction 1 per ≤50 files
  void delete_files(const std::vector<std::string>& files);
  void replace_calls(const std::vector<std::string>& affected_files, const std::vector<CallRow>& rows); // transaction 2 (rows chunked by 2000)
  void replace_imports(const std::vector<std::string>& files, const std::vector<ImportRow>& rows);
  void relink_memory(const std::vector<std::string>& added_symbol_ids);
};
```
Row builders convert `SymbolFact` → the maps in §04.7 (e.g. `params_json = sig.to_json().dump()`, `required_params = sig.required_count()`).

## 5.13 Watcher (`index/watcher.cpp`)

```cpp
class Watcher {
 public:
  Watcher(Engine&); ~Watcher();               // joins
  void start(); void stop();
 private:
  void loop();                                // sleeps interval_ms using condition_variable wait_for (stop wakes it)
};
```
Tick:
1. Every tick: for each FactStore file `platform::stat()`; collect changed (size/mtime) and vanished. Skip `inflight_paths`. Racy-clean rule (§12.8): files hashed < 2 s after their mtime are re-hashed once more on the next tick.
   **Bulk rule (§06.15):** > `watch.bulk_threshold` changed files, or `.git/HEAD`/`.git/index` mtime changed → bulk external change: re-index all, one `SYNCED` Decision, one notice, skip steps 4's per-file audit.
2. Every `rescan_interval_ms`: `scan()` to detect new files.
3. Also check `.graphstate/rules.json` and `config.json` mtimes → reload.
4. If any changes: `lock(state_mu)`; for each changed file: re-hash; if hash unchanged → update stat only; else `audit` (gate P2–P4 in audit mode against pre facts) → reindex → if audit has error findings or the change happened while a session is ACTIVE → record `Decision{actor:"external", status:"UNGATED", violation_codes}` and push a notice to `NoticeQueue`.
5. Notices are drained by the MCP layer into the next tool result (`_notices` field in structuredContent, §08.6).

`// ponytail: portable polling stat() loop (same code on Windows/macOS/Linux); switch to ReadDirectoryChangesW/FSEvents/inotify (e.g. efsw) if repos > 50k files make the 500 ms tick expensive.`

## 5.14 Graph analytics (`index/analytics.hpp/.cpp`)

Track 1 asks for *graph algorithms as agent tools*. GraphState runs three FalkorDB algorithms over the code subgraph and feeds the results into tools and the gate.

| Algorithm | Graph | Output | Used by |
|---|---|---|---|
| **PageRank** (`algo.pageRank('Function','CALLS')`) | functions, call edges | `Function.pagerank`, `pr_pct`, `criticality` tier | `read_symbol`, `get_impact` (risk), gate warning `CRITICAL_SYMBOL_CHANGED`, briefing *Critical symbols*, `get_module_map` hotspots |
| **Betweenness** (`algo.betweenness`, sampled) | files, import edges | top bridge modules | `get_module_map.bridges`, briefing workspace section |
| **WCC** (`algo.WCC`) | files, import edges | connected components | `get_module_map.islands` (files in single-file components = no imports in or out) |

```cpp
namespace gs::index {
struct Criticality { double pagerank = 0, pct = 0; std::string tier; };   // tier: high|medium|low|""
class Analytics {
 public:
  explicit Analytics(Engine&);
  void mark_dirty();                        // called after every code-graph sync
  void maybe_recompute(bool force = false); // recompute if dirty and ≥ 5 s since last run (or force)
  Criticality of(std::string_view function_id) const;           // RAM cache, O(1)
  std::vector<nlohmann::json> bridges(size_t limit);           // cached until dirty
  std::vector<nlohmann::json> islands();                       // cached until dirty
 private:
  std::unordered_map<std::string, Criticality> cache_;
};
}
```
- Tiers: sort functions by score (exclude `kind == "module"`); `pct = 100 * rank_from_bottom / n`; `high` if pct ≥ 95, `medium` if ≥ 80, else `low`. Graphs with < 20 functions: everything `low` (percentiles meaningless).
- Recompute: watcher thread calls `maybe_recompute()` each tick; `startup_reconcile` forces one run; read tools call `maybe_recompute(force=cache empty)`. PageRank on 10 k functions is single-digit ms in FalkorDB (GraphBLAS); the write-back (`Q_SET_CRITICALITY`) only sends changed rows.
- Persisted tiers make criticality visible in FalkorDB Browser (`MATCH (f:Function {criticality:'high'}) RETURN f`) — good demo shot.
- `// ponytail: global recompute on every change (debounced 5 s); incremental PageRank if call graphs reach 1M edges.`
