# GraphState Evaluation: With vs. Without GraphState

> Generated from `20261017_220749.json` on 20261017_220749. Model: `claude-sonnet-5-5`. Dry-run: `False`.

## Summary Results

| Arm | Task success | Broken refs (sum) | Hallucinated calls | Syntax errors | Tests pass | Turns (mean) | Tokens (mean) | Cost (sum) |
|-----|--------------|-------------------|--------------------|---------------|------------|--------------|---------------|------------|
| **A — baseline** | 6/10 (60%) | 0 | 0 | 0 | 60% | 9.5 | 1k | $1.22 |
| **B — GraphState** | 6/10 (60%) | 0 | 0 | 0 | 60% | 11.0 | 3k | $1.49 |

## Fairness & Methodology Notes
- **Identical Setup**: Both arms use identical model, prompts, workspace state, turn budgets, and timeouts.
- **No GraphState Hints in Prompts**: Prompt text is neutral; Arm B acquires guidance strictly through MCP instructions.
- **Independent Verification**: Evaluated via external stdlib `unittest` behavioral tests and deterministic task check scripts.
- **Circularity Caveat**: Arm B's near-zero broken references are partly by construction because the gate checks the same invariants. The independent unittest pass rate and deterministic check scripts provide the un-biased signal.

## Per-Task Breakdown

| Task ID | Category | Arm A Success | Arm A Broken Refs | Arm B Success | Arm B Broken Refs |
|---------|----------|---------------|-------------------|---------------|-------------------|
| T01 | `signature_change` | FAIL | 0 | FAIL | 0 |
| T02 | `rename` | FAIL | 0 | FAIL | 0 |
| T03 | `move` | FAIL | 0 | FAIL | 0 |
| T04 | `feature_hallucination_trap` | PASS | 0 | PASS | 0 |
| T05 | `removal_migration` | FAIL | 0 | FAIL | 0 |
| T06 | `method_chain` | PASS | 0 | PASS | 0 |
| T07 | `greenfield` | PASS | 0 | PASS | 0 |
| T08 | `typescript` | PASS | 0 | PASS | 0 |
| T09 | `refactor_quality` | PASS | 0 | PASS | 0 |
| T10 | `validation` | PASS | 0 | PASS | 0 |

## Tool Calls & Context Breakdown

| Task ID | Arm A Tools Called | Arm B Tools Called (GraphState Gated) | Arm A Turns / Out Tokens | Arm B Turns / Out Tokens |
|---------|--------------------|---------------------------------------|--------------------------|--------------------------|
| T01 | Grep (4), Read (12), Edit (10) | gs:get_briefing (2), gs:get_impact (4), gs:read_symbol (2), Grep (2), Bash (2), gs:propose_patch (2), gs:end_session (2) | 14 t / 2351 out | 9 t / 1641 out |
| T02 | Grep (2), Bash (4) | gs:get_briefing (2), gs:get_impact (2), Grep (4), Bash (2), gs:propose_patch (2), gs:end_session (2) | 4 t / 522 out | 8 t / 3678 out |
| T03 | Grep (4), Glob (2), Read (4), Write (2), Edit (10) | gs:get_briefing (2), gs:get_impact (2), Grep (6), Read (2), gs:propose_patch (4), gs:changeset (4), gs:end_session (2) | 12 t / 1802 out | 12 t / 2999 out |
| T04 | Grep (2), Glob (2), Read (4), Edit (2), Bash (2) | gs:get_briefing (2), gs:search_symbols (2), Glob (2), gs:get_file_outline (6), Read (6), gs:propose_patch (2), gs:end_session (2) | 7 t / 1133 out | 12 t / 1538 out |
| T05 | Grep (4), Glob (2), Read (14), Edit (22), Write (2), PowerShell (2) | gs:get_briefing (2), gs:search_symbols (2), gs:get_impact (2), Grep (4), gs:read_symbol (2), Glob (2), Read (14), gs:propose_patch (4), gs:end_session (2) | 24 t / 3868 out | 18 t / 4747 out |
| T06 | Grep (4), Read (8), Edit (8) | gs:get_briefing (2), gs:search_symbols (4), gs:read_symbol (4), Read (6), Grep (4) | 11 t / 2564 out | 11 t / 2555 out |
| T07 | Bash (6) | gs:get_briefing (2), Glob (2), Read (16), gs:propose_patch (4), gs:changeset (4), Bash (2), gs:end_session (2) | 4 t / 2774 out | 17 t / 6814 out |
| T08 | Grep (2), Read (4), Edit (4) | gs:get_briefing (2), gs:get_impact (2), gs:read_symbol (2), Grep (2), Read (2), gs:propose_patch (2), gs:end_session (2) | 6 t / 889 out | 8 t / 1321 out |
| T09 | Grep (2), Glob (4), Read (2), Edit (2), Bash (2) | gs:get_briefing (2), gs:search_symbols (2), gs:read_symbol (2), Read (2), gs:propose_patch (4), gs:end_session (2) | 7 t / 2735 out | 8 t / 4860 out |
| T10 | Glob (2), Grep (4), Read (2), Edit (2) | gs:get_briefing (2), gs:search_symbols (2), gs:read_symbol (4), gs:propose_patch (2), gs:end_session (2) | 6 t / 1090 out | 7 t / 1457 out |
