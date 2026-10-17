#!/usr/bin/env python3
"""Aggregates eval run results and generates Markdown tables for docs/eval.md."""

import argparse
import glob
import json
import os
import sys

def load_latest_results(results_dir):
    files = glob.glob(os.path.join(results_dir, "*.json"))
    if not files:
        return None
    files.sort(key=os.path.getmtime, reverse=True)
    with open(files[0], "r", encoding="utf-8") as fh:
        return json.load(fh), files[0]

def generate_markdown_report(data, source_file):
    results = data.get("results", [])
    model = data.get("model", "unknown")
    timestamp = data.get("timestamp", "")
    dry_run = data.get("dry_run", False)

    # Group by arm
    by_arm = {"A": [], "B": []}
    for r in results:
        arm = r.get("arm", "A")
        if arm in by_arm:
            by_arm[arm].append(r)

    def stats_for_arm(arm_runs):
        if not arm_runs:
            return None
        n = len(arm_runs)
        successes = sum(1 for r in arm_runs if r.get("task_success", False))
        tests_passed = sum(1 for r in arm_runs if r.get("all_tests_passed", False))
        broken_refs = sum(r.get("broken_refs", 0) for r in arm_runs)
        hallucinated = sum(r.get("hallucinated_calls", 0) for r in arm_runs)
        syntax_errs = sum(r.get("syntax_errors", 0) for r in arm_runs)
        turns_mean = sum(r.get("agent", {}).get("turns", 0) for r in arm_runs) / n
        tokens_mean = sum(r.get("agent", {}).get("input_tokens", 0) + r.get("agent", {}).get("output_tokens", 0) for r in arm_runs) / n
        cost_sum = sum(r.get("agent", {}).get("cost_usd", 0.0) for r in arm_runs)

        return {
            "n": n,
            "success_rate": f"{successes}/{n} ({int(successes * 100 / n)}%)",
            "broken_refs": broken_refs,
            "hallucinated": hallucinated,
            "syntax_errs": syntax_errs,
            "tests_pass_rate": f"{int(tests_passed * 100 / n)}%",
            "turns_mean": f"{turns_mean:.1f}",
            "tokens_mean": f"{int(tokens_mean / 1000)}k" if tokens_mean >= 1000 else f"{int(tokens_mean)}",
            "cost_sum": f"${cost_sum:.2f}"
        }

    stats_a = stats_for_arm(by_arm["A"])
    stats_b = stats_for_arm(by_arm["B"])

    lines = [
        "# GraphState Evaluation: With vs. Without GraphState",
        "",
        f"> Generated from `{os.path.basename(source_file)}` on {timestamp}. Model: `{model}`. Dry-run: `{dry_run}`.",
        "",
        "## Summary Results",
        "",
        "| Arm | Task success | Broken refs (sum) | Hallucinated calls | Syntax errors | Tests pass | Turns (mean) | Tokens (mean) | Cost (sum) |",
        "|-----|--------------|-------------------|--------------------|---------------|------------|--------------|---------------|------------|"
    ]

    if stats_a:
        lines.append(f"| **A — baseline** | {stats_a['success_rate']} | {stats_a['broken_refs']} | {stats_a['hallucinated']} | {stats_a['syntax_errs']} | {stats_a['tests_pass_rate']} | {stats_a['turns_mean']} | {stats_a['tokens_mean']} | {stats_a['cost_sum']} |")
    else:
        lines.append("| **A — baseline** | N/A | - | - | - | - | - | - | - |")

    if stats_b:
        lines.append(f"| **B — GraphState** | {stats_b['success_rate']} | {stats_b['broken_refs']} | {stats_b['hallucinated']} | {stats_b['syntax_errs']} | {stats_b['tests_pass_rate']} | {stats_b['turns_mean']} | {stats_b['tokens_mean']} | {stats_b['cost_sum']} |")
    else:
        lines.append("| **B — GraphState** | N/A | - | - | - | - | - | - | - |")

    lines.extend([
        "",
        "## Fairness & Methodology Notes",
        "- **Identical Setup**: Both arms use identical model, prompts, workspace state, turn budgets, and timeouts.",
        "- **No GraphState Hints in Prompts**: Prompt text is neutral; Arm B acquires guidance strictly through MCP instructions.",
        "- **Independent Verification**: Evaluated via external stdlib `unittest` behavioral tests and deterministic task check scripts.",
        "- **Circularity Caveat**: Arm B's near-zero broken references are partly by construction because the gate checks the same invariants. The independent unittest pass rate and deterministic check scripts provide the un-biased signal.",
        "",
        "## Per-Task Breakdown",
        "",
        "| Task ID | Category | Arm A Success | Arm A Broken Refs | Arm B Success | Arm B Broken Refs |",
        "|---------|----------|---------------|-------------------|---------------|-------------------|"
    ])

    task_ids = sorted(list({r.get("task_id") for r in results}))
    for tid in task_ids:
        runs_a = [r for r in by_arm["A"] if r.get("task_id") == tid]
        runs_b = [r for r in by_arm["B"] if r.get("task_id") == tid]

        succ_a = "PASS" if any(r.get("task_success") for r in runs_a) else "FAIL"
        bref_a = sum(r.get("broken_refs", 0) for r in runs_a)
        succ_b = "PASS" if any(r.get("task_success") for r in runs_b) else "FAIL"
        bref_b = sum(r.get("broken_refs", 0) for r in runs_b)

        lines.append(f"| {tid} | - | {succ_a} | {bref_a} | {succ_b} | {bref_b} |")

    lines.append("")
    return "\n".join(lines)

def main():
    parser = argparse.ArgumentParser(description="Generate Eval Markdown Report")
    parser.add_argument("--results-dir", default="eval/results", help="Directory containing eval JSON results")
    parser.add_argument("--out", default="docs/eval.md", help="Output markdown path")
    args = parser.parse_args()

    loaded = load_latest_results(args.results_dir)
    if not loaded:
        print(f"No result JSON files found in {args.results_dir}")
        sys.exit(0)

    data, src_path = loaded
    md = generate_markdown_report(data, src_path)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write(md)

    print(f"Wrote evaluation report to: {args.out}")

if __name__ == "__main__":
    main()
