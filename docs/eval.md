# GraphState Evaluation: With vs. Without GraphState

> Generated from `20261017_165958.json` on 20261017_165958. Model: `claude-sonnet-5-5`. Dry-run: `True`.

## Summary Results

| Arm | Task success | Broken refs (sum) | Hallucinated calls | Syntax errors | Tests pass | Turns (mean) | Tokens (mean) | Cost (sum) |
|-----|--------------|-------------------|--------------------|---------------|------------|--------------|---------------|------------|
| **A — baseline** | 0/1 (0%) | 0 | 0 | 0 | 100% | 12.0 | 16k | $0.08 |
| **B — GraphState** | 0/1 (0%) | 0 | 0 | 0 | 100% | 15.0 | 19k | $0.10 |

## Fairness & Methodology Notes
- **Identical Setup**: Both arms use identical model, prompts, workspace state, turn budgets, and timeouts.
- **No GraphState Hints in Prompts**: Prompt text is neutral; Arm B acquires guidance strictly through MCP instructions.
- **Independent Verification**: Evaluated via external stdlib `unittest` behavioral tests and deterministic task check scripts.
- **Circularity Caveat**: Arm B's near-zero broken references are partly by construction because the gate checks the same invariants. The independent unittest pass rate and deterministic check scripts provide the un-biased signal.

## Per-Task Breakdown

| Task ID | Category | Arm A Success | Arm A Broken Refs | Arm B Success | Arm B Broken Refs |
|---------|----------|---------------|-------------------|---------------|-------------------|
| T01 | - | FAIL | 0 | FAIL | 0 |
