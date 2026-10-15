# 13 — Evaluation: With vs. Without GraphState

> Index: [README.md](README.md). Code: `eval/*`, `src/cli/cmd_audit.cpp`. Output feeds the README, the blog post and the demo video.
> Goal: one honest, reproducible headline such as *"Across 10 cross-file tasks, the agent left 23 broken call sites / 6 hallucinated calls without GraphState and 0 / 0 with it, at +18 % tokens."*

## 13.1 Design

Two arms, same model, same prompts, same starting code:

| Arm | Agent setup |
|---|---|
| **A — baseline** | Claude Code headless with its native tools (Read, Grep, Glob, Edit, Write, Bash). No GraphState. |
| **B — GraphState** | Claude Code headless with the GraphState MCP server; native Edit/Write denied (host kit from §09.3); Read/Grep/Glob/Bash allowed. |

Each (task, arm) runs `runs` times (default 3; minimum 1) in its own copy of the eval workspace. Arm B copies get their own graph automatically (graph name derives from the workspace path, §03.5).

Fairness rules (MUST, and state them in `docs/eval.md`):
- Identical prompt text, model, max turns, timeout. No GraphState-specific hints in the prompt (arm B gets them from the MCP `instructions`, which is the product).
- Both arms may run the test suite through Bash.
- Report every run, including failures and arm-B runs that hit the turn limit.
- Report cost (tokens, USD, turns, wall time) next to quality — the gate is not free.

## 13.2 Eval workspace (`eval/workspace/`)

A richer sibling of `benchmark_project` (which stays small for the live demo). Python 3.11+ standard library only (no installs), plus a small TypeScript part.

Required shape (the coding agent writes the source to satisfy it; keep it realistic, not contrived):

| Requirement | Why |
|---|---|
| ~20 Python files in 4 packages (`shop/billing`, `shop/catalog`, `shop/orders`, `shop/reports`) + `app.py` | cross-module imports |
| `billing.calculate_total` called from ≥ 8 sites in ≥ 6 files, mixing positional and keyword args | signature-change task |
| `utils.money.round_money` called from ≥ 10 sites | rename task |
| `utils.text.slugify` imported via `from … import` in ≥ 4 files | move task |
| `utils.money.format_currency` exists; **no** `format_money`, `to_currency`, `money_str` | hallucination traps |
| a class hierarchy with `self.method()` calls and a `super()` call | method resolution |
| `billing.apply_discount` called from 4 sites | removal/migration task |
| `web/` with 6 TS files; `web/format.ts::totalLabel(amount, currency?)` called from ≥ 4 sites | TS task |
| one function with cyclomatic complexity ≈ 15 | quality-rule task |
| no syntax errors; `python -m compileall` clean | clean baseline |

Behavioral tests live **outside** the workspace in `eval/tests/` (stdlib `unittest`, ≈ 40 tests), run with `PYTHONPATH=<run dir>`. They encode the task outcomes (e.g. `calculate_total(cart, "EUR")` returns EUR totals) and the pre-existing behavior (regressions).

## 13.3 Tasks (`eval/tasks.json`)

```json
[
 {"id":"T01","category":"signature_change","prompt":"Add a required `currency: str` parameter to billing.calculate_total (after `cart`) and make every caller pass the correct currency (orders use order.currency, reports use 'USD').","check":"eval/checks/t01.py"},
 {"id":"T02","category":"rename","prompt":"Rename utils.money.round_money to quantize_money everywhere.","check":"eval/checks/t02.py"},
 {"id":"T03","category":"move","prompt":"Move slugify from utils/text.py into a new module utils/slugs.py and update all imports. Delete it from utils/text.py.","check":"eval/checks/t03.py"},
 {"id":"T04","category":"feature_hallucination_trap","prompt":"Add a function reports.invoice_lines(order) that returns one formatted money string per line item using the project's existing money formatting helper.","check":"eval/checks/t04.py"},
 {"id":"T05","category":"removal_migration","prompt":"apply_discount is deprecated. Create a Discounts class in billing/discounts.py with apply(self, code, total), migrate all callers, and remove apply_discount.","check":"eval/checks/t05.py"},
 {"id":"T06","category":"method_chain","prompt":"Let Invoice.total accept an optional tax_rate keyword (default unchanged) and pass the customer's tax rate from orders.checkout through to it.","check":"eval/checks/t06.py"},
 {"id":"T07","category":"greenfield","prompt":"Build a new package shop/returns with a ReturnRequest model, a returns service (create_return, approve_return) that uses existing billing and orders code, and a report function. Add it to app.py's command list.","check":"eval/checks/t07.py"},
 {"id":"T08","category":"typescript","prompt":"In web/, make totalLabel's currency parameter required and update every caller to pass it explicitly.","check":"eval/checks/t08.py"},
 {"id":"T09","category":"refactor_quality","prompt":"reports.build_summary is too complex. Split it into smaller helpers without changing behavior.","check":"eval/checks/t09.py"},
 {"id":"T10","category":"validation","prompt":"Add input validation to orders.checkout: raise ValueError for empty carts and negative quantities. Keep existing behavior otherwise.","check":"eval/checks/t10.py"}
]
```
`eval/checks/tNN.py <run_dir>` → exit 0 if the task's intent is met (e.g. T02: no `round_money` identifier left, `quantize_money` defined; T07: package exists and `eval/tests/test_returns.py` passes). Checks are static/behavioral and independent of GraphState.

## 13.4 `graphstate audit` (new CLI command)

```
graphstate audit --root <dir> [--base <pristine_dir>] [--json]
```
A whole-workspace consistency check that needs **no database** (pure in-memory: extractors + Resolver + BindingChecker + syntax collector):
1. Parse all files of `--root`; resolve imports and calls (§05.7).
2. Findings: `SYNTAX_ERROR` per file; for every **exact** call edge, `BindingChecker` → `CALL_INCOMPATIBLE`; every `from M import name` / `M.name(...)` / `self.name(...)` into a workspace module whose target doesn't exist → `MISSING_SYMBOL` / `BROKEN_IMPORT`; relative/workspace imports to missing modules → `MISSING_MODULE`.
3. With `--base`: run the same on the base and report only **introduced** findings (keyed by `code|file|symbol|callee`, line-independent).
4. `--json` → `{"findings":[{code, file, line, col, message}], "counts":{code:n}, "files":n}`; exit 0 (it's a measurement, not a gate).

It shares code with changeset obligations (§06.14.4) — build it as `gate::audit_workspace(const FactStore& base, const FactStore& current)` and use it in both places.

**Circularity caveat (state it in `docs/eval.md`):** arm B is checked by the same analyzer that gated it, so its `broken_refs` near 0 is partly by construction. That's why the independent signals — the behavioral tests and the task checks — are reported alongside, and why audit findings are broken down by code.

## 13.5 Runner (`eval/run_eval.py`, Python stdlib only, cross-platform)

```
python eval/run_eval.py --tasks all --arms A,B --runs 3 --model claude-sonnet-5-5 --parallel 4 \
                        --max-turns 40 --timeout-min 15 --budget-usd 60 --out eval/results
```
Per run:
1. Copy `eval/workspace/` → `eval/runs/<ts>/<task>/<arm>/<n>/ws/`.
2. Arm B: `graphstate init --host claude-code --root ws` and `graphstate index --root ws`. Arm A: write `ws/.claude/settings.json` allowing Edit/Write/Bash.
3. Run the agent headless **(verify flags on day 1, V17)**:
   - A: `claude -p "<prompt>" --output-format json --max-turns 40 --permission-mode acceptEdits --allowedTools "Read,Grep,Glob,Edit,Write,Bash"`
   - B: `claude -p "<prompt>" --output-format json --max-turns 40 --mcp-config ws/.mcp.json --strict-mcp-config --allowedTools "mcp__graphstate__*,Read,Grep,Glob,Bash" --disallowedTools "Edit,Write,NotebookEdit"`
   - cwd = `ws`; capture stdout JSON (turns, usage/tokens, cost, duration) and stderr; enforce timeout.
4. Measure: `graphstate audit --root ws --base eval/workspace --json`; `python -m unittest discover -s eval/tests` with `PYTHONPATH=ws` (parse `Ran N tests`, failures/errors); `python eval/checks/tNN.py ws`; TS: `npx tsc --noEmit -p ws/web` if Node is available (else `ts_check: null`).
5. Arm B extras from the graph (`graphstate query --root ws --ro`): Decisions by status, Pitfalls created, changesets opened/closed.
6. Write `run.json`.

Stop scheduling new runs when the summed cost reaches `--budget-usd`.

## 13.6 Metrics & report

Per run:

| Metric | Source |
|---|---|
| `task_success` | check exit 0 **and** all tests pass **and** 0 introduced `SYNTAX_ERROR` |
| `broken_refs` | audit: introduced `CALL_INCOMPATIBLE` + `BROKEN_IMPORT` + `MISSING_MODULE` + `REMOVED_SYMBOL_IN_USE` |
| `hallucinated_calls` | audit: introduced `MISSING_SYMBOL` |
| `syntax_errors` | audit: introduced `SYNTAX_ERROR` |
| `tests_pass_rate` | unittest |
| `turns`, `input_tokens`, `output_tokens`, `cost_usd`, `wall_s` | Claude Code JSON output |
| `gate_rejections`, `rejections_recovered`, `changesets` | arm B graph |

`eval/report.py` aggregates into `docs/eval.md`:
```
| Arm | Task success | Broken refs (sum) | Hallucinated calls | Syntax errors | Tests pass | Turns (mean) | Tokens (mean) | Cost (sum) |
| A   | 6/10 (60 %)  | 23                | 6                  | 1             | 91 %       | 14.2         | 182k          | $9.80      |
| B   | 9/10 (90 %)  | 0                 | 0                  | 0             | 99 %       | 17.9         | 214k          | $11.60     |
```
(numbers illustrative) + a per-task breakdown + mean ± stdev when `runs > 1` + the fairness and circularity notes + the exact command, model id and date.

## 13.7 Budget & schedule

- Full: 10 tasks × 2 arms × 3 runs = 60 runs. Rough cost with a Sonnet-class model: $30–90; wall time ≈ 1.5–3 h at `--parallel 4`.
- Minimum viable: 10 × 2 × 1 = 20 runs (~$10–30, < 1 h).
- Schedule: build `eval/workspace`, tests, tasks and checks on **Day 3 evening** (M10a, ~3 h with a coding agent); run on **Day 4 morning** while recording the demo; write `docs/eval.md` + README table by Day 4 noon.

## 13.8 Files

```
eval/
├── workspace/           # pristine eval project (§13.2)
├── tests/               # unittest behavioral tests (outside the workspace)
├── tasks.json           # §13.3
├── checks/t01.py … t10.py
├── run_eval.py          # §13.5
├── report.py            # §13.6 → docs/eval.md
├── runs/                # per-run copies (gitignored)
└── results/<ts>.json    # aggregated raw results (committed)
```
