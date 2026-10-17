# GraphState Evaluation: With vs. Without GraphState

> Generated from `20261017_224205.json` on 20261017_224205. Model: `haiku`. Dry-run: `False`.

## Summary Results

| Arm | Task success | Broken refs (sum) | Hallucinated calls | Syntax errors | Tests pass | Turns (mean) | Tokens (mean) | Cost (sum) |
|-----|--------------|-------------------|--------------------|---------------|------------|--------------|---------------|------------|
| **A — baseline** | 10/10 (100%) | 0 | 0 | 0 | 100% | 12.9 | 4k | $0.10 |
| **B — GraphState** | 10/10 (100%) | 0 | 0 | 0 | 100% | 13.3 | 6k | $0.14 |

## Fairness & Methodology Notes
- **Identical Setup**: Both arms use identical model, prompts, workspace state, turn budgets, and timeouts.
- **No GraphState Hints in Prompts**: Prompt text is neutral; Arm B acquires guidance strictly through MCP instructions.
- **Independent Verification**: Evaluated via external stdlib `unittest` behavioral tests and deterministic task check scripts.
- **Circularity Caveat**: Arm B's near-zero broken references are partly by construction because the gate checks the same invariants. The independent unittest pass rate and deterministic check scripts provide the un-biased signal.

## Per-Task Breakdown

| Task ID | Category | Arm A Success | Arm A Broken Refs | Arm B Success | Arm B Broken Refs |
|---------|----------|---------------|-------------------|---------------|-------------------|
| T01 | `signature_change` | PASS | 0 | PASS | 0 |
| T02 | `rename` | PASS | 0 | PASS | 0 |
| T03 | `move` | PASS | 0 | PASS | 0 |
| T04 | `feature_hallucination_trap` | PASS | 0 | PASS | 0 |
| T05 | `removal_migration` | PASS | 0 | PASS | 0 |
| T06 | `method_chain` | PASS | 0 | PASS | 0 |
| T07 | `greenfield` | PASS | 0 | PASS | 0 |
| T08 | `typescript` | PASS | 0 | PASS | 0 |
| T09 | `refactor_quality` | PASS | 0 | PASS | 0 |
| T10 | `validation` | PASS | 0 | PASS | 0 |

## Tool Calls & Context Breakdown

| Task ID | Arm A Tools Called | Arm B Tools Called (GraphState Gated) | Arm A Turns / Out Tokens | Arm B Turns / Out Tokens |
|---------|--------------------|---------------------------------------|--------------------------|--------------------------|
| T01 | Grep (6), Glob (2), Read (16), Edit (10), Bash (2) | gs:get_briefing (2), gs:search_symbols (2), gs:read_symbol (2), gs:get_impact (2), Grep (4), gs:propose_patch (2), gs:end_session (2) | 19 t / 3811 out | 9 t / 3621 out |
| T02 | Grep (2), Bash (10) | gs:get_briefing (2), Grep (4), Read (18), gs:propose_patch (2), Bash (2), gs:end_session (2) | 7 t / 2222 out | 16 t / 6327 out |
| T03 | Grep (4), Glob (2), Read (2), Write (2), Edit (10), Bash (2) | gs:get_briefing (2), Grep (4), gs:read_symbol (2), Read (2), gs:propose_patch (4), gs:changeset (4), gs:end_session (2) | 12 t / 2378 out | 11 t / 4955 out |
| T04 | Grep (4), Glob (2), Read (10), Edit (2), Bash (2) | gs:get_briefing (2), gs:search_symbols (4), Grep (2), Glob (2), Read (8), gs:propose_patch (2), Bash (2), gs:end_session (2) | 11 t / 2467 out | 13 t / 3488 out |
| T05 | Grep (2), Glob (2), Read (14), Edit (22), Bash (4) | gs:get_briefing (2), gs:search_symbols (2), gs:get_impact (2), gs:read_symbol (2), Grep (4), gs:get_file_outline (4), Glob (2), Read (14), gs:changeset (4), gs:propose_patch (4), Bash (4), gs:end_session (2) | 23 t / 5427 out | 24 t / 9284 out |
| T06 | Grep (4), Glob (4), Read (12), Edit (10), Bash (8) | gs:get_briefing (2), gs:search_symbols (2), Grep (4), Read (8), gs:read_symbol (4), gs:get_impact (2), gs:propose_patch (6), Bash (2), gs:end_session (2) | 20 t / 6836 out | 17 t / 8143 out |
| T07 | Bash (8), Write (8), Edit (4) | gs:get_briefing (2), Glob (2), Bash (6), gs:changeset (4), gs:propose_patch (4), gs:end_session (2) | 11 t / 9524 out | 11 t / 13316 out |
| T08 | Grep (2), Read (4), Edit (4), Bash (2) | gs:get_briefing (2), Grep (4), Read (4), gs:get_impact (2), gs:read_symbol (2), gs:propose_patch (2), gs:end_session (2) | 7 t / 1491 out | 10 t / 2358 out |
| T09 | Grep (2), Glob (2), Read (2), Bash (10), Write (2) | gs:get_briefing (2), gs:search_symbols (2), gs:read_symbol (2), gs:get_file_outline (2), gs:propose_patch (4), Bash (8), gs:end_session (2) | 10 t / 6936 out | 12 t / 10789 out |
| T10 | Grep (4), Read (6), Glob (2), Edit (2), Bash (2) | gs:get_briefing (2), gs:search_symbols (4), gs:read_symbol (4), gs:get_file_outline (2), gs:propose_patch (2), Bash (2), gs:end_session (2) | 9 t / 2063 out | 10 t / 2719 out |

---

## Next Milestone: Real-World OSS Blast-Radius Evaluation

To stress-test GraphState beyond synthetic micro-benchmarks and prove immunity to "Localized Tunnel Vision" on production open-source codebases, see the full execution plan in [docs/oss_eval_plan.md](oss_eval_plan.md).

