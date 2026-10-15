# 10 — Testing, Fixtures, Demo Project & Benchmarks

> Index: [README.md](README.md). Framework: doctest v2.5.3. Labels: `unit` (no DB), `db` (needs FalkorDB at `GRAPHSTATE_FALKOR_HOST:PORT`, default 127.0.0.1:6390).

## 10.1 Principles

- Every module in §03.1 with logic has a unit test file. Tests are table-driven where possible.
- DB tests use a throwaway graph `gs:test-<12 hex>` and `GRAPH.DELETE` it in a fixture destructor (also delete `gs:test-*` older than 1 h at test start).
- No mocks of FalkorDB: integration tests hit the real thing (it's one `docker compose up`).
- Test-only crash injection: env `GRAPHSTATE_CRASH_AT=<point>` (`wal_prepared`, `wal_renaming_mid`, `wal_renamed`, `after_graph`) makes the disk transaction call `_exit(137)` at that point. Compiled in all builds (cheap `getenv` once at startup).

## 10.2 Unit tests (must exist and pass)

| File | Cases (minimum) |
|---|---|
| `test_cypher_params.cpp` | null/bool/int/negative/float/NaN-throws; strings with `'` `\` `\n` `\r` `\t` `"` emoji `$x` `}`; nested `[{a:1,b:['x']}]`; empty list/map; invalid key `a-b` throws; full prefix `CYPHER a=1 b='x'` ordering = insertion order |
| `test_result_parse.cpp` | fabricated `redisReply` trees: 3-element reply with `j` column; 1-element stats-only; stats line parsing incl. `Cached execution: 1`; error reply → throws `DB_QUERY` |
| `test_text.cpp` | dedent (tabs vs spaces, blank lines), reindent to method indentation, CRLF conversion, line index (byte→line/col incl. UTF-8 multi-byte), Damerau-Levenshtein, identifier splitting (`calculateTotal`, `calc_total`, `HTTPServer` → `http server`), `goal_norm` |
| `test_paths.cpp` | glob `**/x/**`, `*.py`, `{a,b}`, `?`; containment rejects `../`, absolute, symlink escape (create temp symlink) |
| `test_binding.cpp` | **Python**: `f(a, b=1)` with 1/2/3 positional; `f(a, *, k)` called `f(1, k=2)` ok / `f(1, 2)` too many / `f(1)` missing k; `f(a, /, b)` with `a=1` kw → unexpected; `f(*args)` any count ok; `f(**kw)` any kw ok; multiple values `f(1, a=1)`; instance method drops self; staticmethod doesn't; classmethod via class drops cls; call with `*x` → Unknown. **TS**: `f(a, b?)` 1/2/3 args; `f(a = 1, b)` min=2; rest `f(a, ...r)` 10 args ok; `this` param ignored; spread → Unknown; overloads → Unknown. **JS**: mismatch → Incompatible (severity downgrade tested in gate) |
| `test_python_extractor.cpp` | golden: fixture `py_basic/` → expected facts JSON (symbols, params kinds, decorators, async, docstring, bases, calls with shapes/receivers/arg counts, imports incl. relative/aliased/wildcard, module_names, `<module>` calls, nested def attribution, locals) |
| `test_ts_extractor.cpp` | golden: `ts_basic/` (function decl, overloads, class with constructor/static/private `#m`, arrow exports, `export default function`, interface, named/default/namespace/type-only imports, re-exports, `new X()`, `this.m()`, optional chaining, spread) |
| `test_js_extractor.cpp` | golden: `js_basic/` (assignment_pattern defaults, rest, destructuring, CommonJS require/exports) |
| `test_resolver.cpp` | fixture `resolve/`: Python absolute/relative/`__init__`/`src` root/submodule-from-import; TS `./x`, `./x.js`→`x.ts`, `index.ts`; call table rows of §05.7.3 each covered; heuristic uniqueness + COMMON_NAMES exclusion; constructor → `__init__` in base class; `super().m()` |
| `test_syntax.cpp` | missing `)` → kind missing + expected `")"` at right line/col; stray token → unexpected; pre-existing error outside edited range not reported as introduced; cap at 10 |
| `test_apply.cpp` | replace_symbol method re-indent (4-space and tab files); decorated function range includes decorators; TS `export function` range includes `export`; insert after/before/into_class/at_start/end with correct blank lines; delete last method inserts `pass`; replace_text not found (closest line) / not unique (line list); CRLF file stays CRLF; create/delete file |
| `test_rules.cpp` | each built-in rule positive + negative; custom rule with `#eq?`, `#match?`, `#any-of?`, `unless_query` suppression, `paths` filter; invalid query disables rule with error offset; rules only applied inside changed ranges |
| `test_metrics.cpp` | complexity of known snippets (if/elif/else = 3, `a and b or c` +2, comprehension with if +2); nesting depth; nested def excluded |
| `test_disk_txn.cpp` (temp dir, no DB) | happy path; recovery from each `GRAPHSTATE_CRASH_AT` point by **re-launching the test binary as a child** (no fork — Windows, §12.11): child crashes, parent runs recovery and asserts disk state (pre for PREPARED/RENAMING, post for RENAMED); file mode preserved; created dirs removed on rollback; Windows-only locked-target retry test |
| `test_changesets.cpp` (DB) | begin → patch referencing a missing module → APPLIED with 1 obligation → patch creating it → 0 obligations + discharged id → finalize CLOSED; begin while open → CHANGESET_OPEN; abandon restores created/modified/deleted files byte-exact; abandon with an external edit → CHANGESET_CONFLICT and nothing restored; restart with OPEN changeset → obligations recomputed identically; syntax error inside changeset still REJECTED |
| `test_analytics.cpp` (DB) | PageRank tiers on a fixture call graph (a hub called by many → `high`); < 20 functions → all `low`; betweenness puts the bridge file first; WCC reports an isolated file; fallback path (fan-in percentile) when `algo.*` errors |
| `test_audit.cpp` | `audit_workspace` on fixture pairs: introduced vs pre-existing findings; line-independent keys; each finding code covered; no DB |
| `test_platform.cpp` | path normalization (`a\\b` → `a/b`, reject `C:/x`, `C:x`, `\\\\srv\\x`, `..`); reserved names (`con.py`, `NUL`, `aux.txt`), trailing dot/space; UTF-8 round-trip of non-ASCII paths; case-insensitive resolution when the FS probe says so; `run_capture` with spaces/quotes in args; `executable_path` exists |

## 10.3 Fixtures layout

```
tests/fixtures/
  py_basic/      src files + expected.json  (FileFacts JSON, generated by `graphstate debug-facts <file>` then hand-reviewed)
  ts_basic/  js_basic/  resolve/
  gate/          before/ + patches/*.json + expected/*.json   (gate e2e: request → expected status, violation codes, locations)
```
Add a hidden CLI `graphstate debug-facts <file>` (prints FileFacts JSON; not listed in `--help`) to create/refresh goldens. Golden comparison ignores `body_hash`/`hash` fields.

## 10.4 Integration & e2e tests

| Test | What |
|---|---|
| `test_graph_client.cpp` | ping; query with params round-trip (`RETURN toJSON({s:$s}) AS j` for tricky strings); pipeline of 100; MULTI/EXEC; reconnect after `CLIENT KILL` |
| `test_graph_writer.cpp` | index `benchmark_project` copy → counts: Files, Functions (incl. `<module>`), CALLS edges match expected numbers in `expected_counts.json`; re-sync unchanged → zero writes (stats); rename a function → old node gone, callers' edges re-resolved |
| `test_gate_e2e.cpp` | for each `fixtures/gate/patches/*.json`: copy `before/` to temp, index, `propose`, compare result to `expected/*.json` (status, failed_phase, violation codes+files+lines); verify disk unchanged on REJECTED and changed on APPLIED; verify Decision/Pitfall nodes |
| `test_memory.cpp` | session create/heartbeat/interrupted marking (fake `now`); intent dedupe; remember dedupe/supersede/secret guard; recall ranking (anchor first); briefing sections + budget truncation |
| `test_restart.cpp` | start engine on temp workspace, apply a patch, record memory, destroy Engine (simulated kill: no shutdown), modify a file externally, start new Engine → reconcile re-indexes only that file; previous session INTERRUPTED; memory present; ungated Decision recorded |
| `tests/e2e/run.py` | spawns `build/graphstate serve --root <tmp copy of benchmark_project>`, feeds each `*.jsonl` line, asserts response subsets. Python 3 stdlib only. Run via `ctest -L e2e` |

`tests/e2e/*.jsonl` format — alternating `>` (send) and `<` (expect subset; `"*"` = any value) lines:
```
> {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"e2e","version":"0"}}}
< {"id":1,"result":{"protocolVersion":"2025-06-18","serverInfo":{"name":"graphstate"}}}
> {"jsonrpc":"2.0","method":"notifications/initialized"}
> {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"propose_patch","arguments":{"intent":"x","reasoning":"y","edits":[{"op":"replace_symbol","file":"billing.py","symbol":"calculate_total","code":"def calculate_total(cart, currency, tax_rate=0.1):\n    return 0\n"}]}}}
< {"id":2,"result":{"isError":false,"structuredContent":{"status":"REJECTED","failed_phase":"BLAST_RADIUS"}}}
```
Required scripts: `handshake_legacy.jsonl`, `handshake_modern.jsonl` (server/discover + `_meta` + bad version → -32022), `read_tools.jsonl`, `gate_reject_apply.jsonl` (demo steps 2–3), `changeset_flow.jsonl` (greenfield build with obligations → finalize), `memory_flow.jsonl`, `errors.jsonl` (bad JSON, unknown method, unknown tool, invalid args, batch array, CRLF-terminated request line).

## 10.5 `benchmark_project/` (demo workspace — exact contents)

`models.py`
```python
from dataclasses import dataclass, field
from decimal import Decimal


@dataclass
class LineItem:
    sku: str
    price: Decimal
    qty: int = 1


@dataclass
class Cart:
    user_id: str
    items: list[LineItem] = field(default_factory=list)
```
`utils.py`
```python
from decimal import Decimal, ROUND_HALF_UP


def round_money(x: Decimal) -> Decimal:
    return x.quantize(Decimal("0.01"), rounding=ROUND_HALF_UP)


def format_currency(amount: Decimal, currency: str = "USD") -> str:
    return f"{currency} {round_money(amount):,}"


def slugify(text: str) -> str:
    return "-".join(text.lower().split())
```
`billing.py`
```python
from decimal import Decimal

from models import Cart
from utils import round_money


def calculate_total(cart: Cart, tax_rate: float = 0.1) -> Decimal:
    """Total including tax."""
    subtotal = sum((i.price * i.qty for i in cart.items), Decimal("0"))
    return round_money(subtotal * (1 + Decimal(str(tax_rate))))


def apply_discount(total: Decimal, code: str) -> Decimal:
    if code == "WELCOME10":
        return round_money(total * Decimal("0.9"))
    return total


class Invoice:
    def __init__(self, cart: Cart):
        self.cart = cart

    def total(self) -> Decimal:
        return calculate_total(self.cart)

    def render(self) -> str:
        return f"Invoice for {self.cart.user_id}: {self.total()}"
```
`checkout.py`
```python
from billing import Invoice, apply_discount, calculate_total
from models import Cart


def checkout(cart: Cart, rate: float, code: str = "") -> str:
    total = calculate_total(cart, tax_rate=rate)
    if code:
        total = apply_discount(total, code)
    return Invoice(cart).render() + f" (charged {total})"
```
`reports.py`
```python
from billing import calculate_total
from utils import format_currency


def daily_report(carts) -> str:
    lines = []
    for cart in carts:
        lines.append(f"{cart.user_id}: {format_currency(calculate_total(cart))}")
    return "\n".join(lines)
```
`app.py`
```python
from decimal import Decimal

from billing import calculate_total
from models import Cart, LineItem
from utils import format_currency

cart = Cart("u1", [LineItem("book", Decimal("12.50"), 2)])
total = calculate_total(cart)
print(format_currency(total))
```
`web/api.ts`
```ts
export interface CartDto { userId: string; total: number }

export function totalLabel(amount: number, currency?: string): string {
  return `${currency ?? "USD"} ${amount.toFixed(2)}`;
}

export async function fetchCart(id: string): Promise<CartDto> {
  const res = await fetch(`/api/carts/${id}`);
  return (await res.json()) as CartDto;
}
```
`web/cart.ts`
```ts
import { fetchCart, totalLabel } from "./api";

export class CartView {
  constructor(private readonly id: string) {}

  async render(): Promise<string> {
    const cart = await fetchCart(this.id);
    return totalLabel(cart.total, "USD");
  }
}
```
`.graphstate/rules.json`
```json
{ "version": 1, "rules": { "py-mutable-default-arg": { "severity": "error" }, "py-bare-except": { "severity": "error" } } }
```

Expected graph facts (assert in `test_graph_writer.cpp`; update if fixture changes):
- Files: 8 (`.graphstate/**` is ignored) → `models.py utils.py billing.py checkout.py reports.py app.py web/api.ts web/cart.ts`.
- Callers of `billing.py::calculate_total` (exact): `billing.py::Invoice.total`, `checkout.py::checkout`, `reports.py::daily_report`, `app.py::<module>` = **4**.
- Demo step 2 ("add required `currency`") therefore rejects with **4** `CALLER_INCOMPATIBLE` violations. `Invoice.total` comes from post-state facts because billing.py is in the WorkingSet; the other 3 come from `Q_CALLERS_OF_IDS`.
- `utils.format_money` in new code → `MISSING_SYMBOL` with `did_you_mean: ["format_currency"]` (shared 6-char prefix `format`).
- `web/cart.ts::CartView.render` calls `web/api.ts::fetchCart` and `totalLabel` (exact).

`tools/demo_reset.sh`: `git checkout -- benchmark_project` is unavailable (no git) → copy from `tests/fixtures/benchmark_project_pristine/` (identical copy) over `benchmark_project/`, then `graphstate index --full --root benchmark_project`; does **not** wipe memory unless `--wipe-memory` (`graphstate forget --all-memory --yes`).

## 10.6 Benchmarks (`graphstate bench`)

Runs against the current workspace (use a generated large workspace for headline numbers: `tools/gen_synthetic.py --files 1000 --funcs-per-file 20` creates Python+TS files with cross-file imports/calls — dev script, Python stdlib only).

| Workload | Method | Report |
|---|---|---|
| `cold_index` | `full_rebuild()` once (after `GRAPH.DELETE` of a bench graph `gs:bench-…`) | ms, files/s |
| `reindex_one` | touch+modify one mid-size file body, `reindex([f])` × N | p50/p95/p99 |
| `read_symbol` | random symbol, in-process handler call incl. JSON render × N | p50/p95/p99 |
| `get_impact_d3` | random high-fan-in symbol × N | p50/p95/p99 |
| `patch_reject` | `propose_patch` that breaks callers × N (no disk writes) | p50/p95/p99 |
| `patch_apply` | `propose_patch` body-only change applied then reverted × N | p50/p95/p99 (incl. fsync) |
| `restart` | destroy Engine, construct + `startup_reconcile()` on unchanged workspace | ms |
| `rss` | `getrusage` max RSS after cold index | MB |

Output table (also `--json`):
```
workload        n     p50(ms)  p95(ms)  p99(ms)
read_symbol     500   0.62     1.10     1.90
patch_reject    200   1.40     2.20     3.00
...
```
Targets in §01.9. Save the bench output to `docs/bench.md` for the README/blog.
