#!/usr/bin/env python3
"""GraphState Evaluation Runner: Arm A (baseline) vs Arm B (GraphState).

Executes benchmark tasks across arms, measures audit findings, tests, and task checks,
and generates structured JSON output.
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import time

def find_graphstate_bin(custom_path=None):
    if custom_path and os.path.isfile(custom_path):
        return os.path.abspath(custom_path)
    candidates = [
        "build/windows-msvc-release/graphstate.exe",
        "build/windows-msvc-release/graphstate",
        "build/graphstate.exe",
        "build/graphstate",
        "graphstate.exe",
        "graphstate"
    ]
    for c in candidates:
        if os.path.isfile(c):
            return os.path.abspath(c)
    # Check PATH
    bin_on_path = shutil.which("graphstate")
    if bin_on_path:
        return os.path.abspath(bin_on_path)
    return "graphstate"

def load_tasks(tasks_json_path):
    with open(tasks_json_path, "r", encoding="utf-8") as fh:
        return json.load(fh)

def run_cmd(cmd, cwd=None, env=None, timeout=None, shell=None):
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    t0 = time.time()
    use_shell = shell if shell is not None else ((sys.platform == "win32") or isinstance(cmd, str))
    try:
        proc = subprocess.run(
            cmd,
            cwd=cwd,
            env=merged_env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            shell=use_shell
        )
        return proc.returncode, proc.stdout, proc.stderr, time.time() - t0, False
    except subprocess.TimeoutExpired as e:
        return -1, e.stdout or "", e.stderr or "", time.time() - t0, True
    except Exception as e:
        return -1, "", str(e), time.time() - t0, False

def run_eval_task(task, arm, run_idx, args, ts, base_ws_dir, graphstate_bin):
    task_id = task["id"]
    run_ws = os.path.abspath(os.path.join("eval", "runs", ts, task_id, arm, str(run_idx), "ws"))
    os.makedirs(run_ws, exist_ok=True)

    # 1. Copy pristine workspace
    shutil.copytree(base_ws_dir, run_ws, dirs_exist_ok=True)

    # 2. Host setup
    if arm == "B":
        # Initialize GraphState host kit for Claude Code
        code, out, err, _, _ = run_cmd([graphstate_bin, "init", "--host", "claude-code", "--root", run_ws])
        # Run initial index if DB is available (skip error if DB offline)
        run_cmd([graphstate_bin, "index", "--root", run_ws])
    else:
        # Arm A: Native tools only
        claude_dir = os.path.join(run_ws, ".claude")
        os.makedirs(claude_dir, exist_ok=True)
        settings_path = os.path.join(claude_dir, "settings.json")
        with open(settings_path, "w", encoding="utf-8") as fh:
            json.dump({
                "permissions": {
                    "allow": ["Read", "Grep", "Glob", "Edit", "Write", "Bash"]
                }
            }, fh, indent=2)

    # 3. Agent execution
    agent_output = {
        "turns": 0,
        "input_tokens": 0,
        "cache_read_tokens": 0,
        "cache_creation_tokens": 0,
        "output_tokens": 0,
        "cost_usd": 0.0,
        "duration_s": 0.0,
        "session_id": None,
        "tools_called": [],
        "tool_counts": {}
    }

    if args.dry_run:
        # Simulated execution for testing harness pipeline
        time.sleep(0.05)
        agent_output["turns"] = 12 if arm == "A" else 15
        agent_output["input_tokens"] = 15000 if arm == "A" else 18000
        agent_output["output_tokens"] = 1200 if arm == "A" else 1400
        agent_output["cost_usd"] = 0.08 if arm == "A" else 0.10
        agent_output["duration_s"] = 0.5
    else:
        # Real Claude Code headless run
        claude_bin = shutil.which("claude") or "claude"
        mcp_cfg = os.path.join(run_ws, ".mcp.json")
        if arm == "B":
            cmd = [
                claude_bin, "-p", task["prompt"],
                "--output-format", "json",
                "--max-turns", str(args.max_turns),
                "--mcp-config", mcp_cfg,
                "--strict-mcp-config",
                "--allowedTools", "mcp__graphstate__*,Read,Grep,Glob,Bash",
                "--disallowedTools", "Edit,Write,NotebookEdit"
            ]
        else:
            cmd = [
                claude_bin, "-p", task["prompt"],
                "--output-format", "json",
                "--max-turns", str(args.max_turns),
                "--permission-mode", "acceptEdits",
                "--allowedTools", "Read,Grep,Glob,Edit,Write,Bash"
            ]

        if args.model:
            cmd.extend(["--model", args.model])

        if getattr(args, "effort", None):
            cmd.extend(["--effort", args.effort])

        timeout_sec = args.timeout_min * 60
        code, out, err, dur, timed_out = run_cmd(cmd, cwd=run_ws, timeout=timeout_sec)
        agent_output["duration_s"] = dur

        if out:
            try:
                data = json.loads(out)
                agent_output["turns"] = data.get("turns", 0) or data.get("num_turns", 0)
                usage = data.get("usage", {})
                agent_output["input_tokens"] = usage.get("input_tokens", 0)
                agent_output["cache_read_tokens"] = usage.get("cache_read_input_tokens", 0)
                agent_output["cache_creation_tokens"] = usage.get("cache_creation_input_tokens", 0)
                agent_output["output_tokens"] = usage.get("output_tokens", 0)
                agent_output["cost_usd"] = data.get("cost_usd", 0.0) or data.get("total_cost_usd", 0.0)
                session_id = data.get("session_id")
                agent_output["session_id"] = session_id

                tool_calls = []
                if session_id:
                    user_home = os.path.expanduser("~")
                    claude_projects_dir = os.path.join(user_home, ".claude", "projects")
                    if os.path.isdir(claude_projects_dir):
                        for root_p, _, files_p in os.walk(claude_projects_dir):
                            for f_p in files_p:
                                if session_id in f_p:
                                    jsonl_path = os.path.join(root_p, f_p)
                                    try:
                                        with open(jsonl_path, "r", encoding="utf-8", errors="replace") as jfh:
                                            for line in jfh:
                                                if not line.strip():
                                                    continue
                                                entry = json.loads(line)
                                                msg = entry.get("message", {})
                                                content = msg.get("content", [])
                                                if isinstance(content, list):
                                                    for blk in content:
                                                        if isinstance(blk, dict) and blk.get("type") == "tool_use":
                                                            tname = blk.get("name")
                                                            if tname:
                                                                tool_calls.append(tname)
                                    except Exception:
                                        pass
                                    break
                tool_counts = {}
                for t in tool_calls:
                    tool_counts[t] = tool_counts.get(t, 0) + 1
                agent_output["tools_called"] = tool_calls
                agent_output["tool_counts"] = tool_counts
            except Exception:
                pass

    # 4. Measurements
    # Audit: graphstate audit --root ws --base eval/workspace --json
    audit_cmd = [graphstate_bin, "audit", "--root", run_ws, "--base", base_ws_dir, "--json"]
    code, audit_out, audit_err, _, _ = run_cmd(audit_cmd)

    audit_findings = []
    audit_counts = {}
    if audit_out:
        try:
            audit_data = json.loads(audit_out)
            audit_findings = audit_data.get("findings", [])
            audit_counts = audit_data.get("counts", {})
        except Exception:
            pass

    syntax_errors = audit_counts.get("SYNTAX_ERROR", 0)
    broken_refs = (
        audit_counts.get("CALL_INCOMPATIBLE", 0) +
        audit_counts.get("BROKEN_IMPORT", 0) +
        audit_counts.get("MISSING_MODULE", 0) +
        audit_counts.get("REMOVED_SYMBOL_IN_USE", 0)
    )
    hallucinated_calls = audit_counts.get("MISSING_SYMBOL", 0)

    # Unit behavioral tests: python -m unittest discover -s eval/tests with PYTHONPATH=ws
    test_env = {"PYTHONPATH": run_ws}
    t_code, t_out, t_err, _, _ = run_cmd(
        [sys.executable, "-m", "unittest", "discover", "-s", "eval/tests"],
        cwd=os.getcwd(),
        env=test_env
    )
    all_tests_passed = (t_code == 0)
    tests_summary = t_err if t_err else t_out

    # Task check script: python eval/checks/tNN.py ws
    check_script = task.get("check")
    check_passed = False
    check_message = ""
    if check_script and os.path.isfile(check_script):
        c_code, c_out, c_err, _, _ = run_cmd([sys.executable, check_script, run_ws])
        check_passed = (c_code == 0)
        check_message = (c_out + "\n" + c_err).strip()

    # TypeScript type check: npx tsc --noEmit -p ws/web
    ts_check = None
    if shutil.which("npx"):
        ts_code, _, _, _, _ = run_cmd(["npx", "tsc", "--noEmit", "-p", os.path.join(run_ws, "web")])
        ts_check = (ts_code == 0)

    # Overall task success per §13.6
    task_success = (check_passed and all_tests_passed and syntax_errors == 0)

    run_record = {
        "timestamp": datetime.datetime.now().isoformat(),
        "task_id": task_id,
        "arm": arm,
        "run_idx": run_idx,
        "task_success": task_success,
        "check_passed": check_passed,
        "check_message": check_message,
        "all_tests_passed": all_tests_passed,
        "broken_refs": broken_refs,
        "hallucinated_calls": hallucinated_calls,
        "syntax_errors": syntax_errors,
        "audit_counts": audit_counts,
        "audit_findings_count": len(audit_findings),
        "ts_check": ts_check,
        "agent": agent_output
    }

    # Write per-run run.json
    run_json_path = os.path.join(os.path.dirname(run_ws), "run.json")
    with open(run_json_path, "w", encoding="utf-8") as fh:
        json.dump(run_record, fh, indent=2)

    return run_record

def main():
    parser = argparse.ArgumentParser(description="GraphState Evaluation Runner")
    parser.add_argument("--tasks", default="all", help="Comma-separated task IDs or 'all'")
    parser.add_argument("--arms", default="A,B", help="Comma-separated arms: A, B, or A,B")
    parser.add_argument("--runs", type=int, default=1, help="Number of runs per (task, arm)")
    parser.add_argument("--model", default="claude-sonnet-5-5", help="Model name")
    parser.add_argument("--effort", default="medium", help="Effort level (low, medium, high)")
    parser.add_argument("--max-turns", type=int, default=40, help="Max turns per run")
    parser.add_argument("--timeout-min", type=int, default=15, help="Timeout in minutes per run")
    parser.add_argument("--budget-usd", type=float, default=60.0, help="Maximum budget in USD")
    parser.add_argument("--out", default="eval/results", help="Output directory for aggregated results")
    parser.add_argument("--graphstate-bin", default=None, help="Path to graphstate binary")
    parser.add_argument("--dry-run", action="store_true", help="Dry run without invoking LLM")

    args = parser.parse_args()

    graphstate_bin = find_graphstate_bin(args.graphstate_bin)
    base_ws_dir = os.path.abspath("eval/workspace")

    all_tasks = load_tasks("eval/tasks.json")
    if args.tasks != "all":
        selected_ids = {t.strip().upper() for t in args.tasks.split(",")}
        tasks_to_run = [t for t in all_tasks if t["id"].upper() in selected_ids]
    else:
        tasks_to_run = all_tasks

    arms = [a.strip().upper() for a in args.arms.split(",")]

    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    os.makedirs(args.out, exist_ok=True)

    print(f"GraphState Evaluation Runner [{ts}]")
    print(f"Tasks: {[t['id'] for t in tasks_to_run]} ({len(tasks_to_run)})")
    print(f"Arms: {arms}, Runs per task/arm: {args.runs}")
    print(f"Dry Run: {args.dry_run}")
    print(f"Binary: {graphstate_bin}")
    print("-" * 60)

    total_cost = 0.0
    results = []

    for task in tasks_to_run:
        for arm in arms:
            for r in range(1, args.runs + 1):
                if total_cost >= args.budget_usd:
                    print(f"Budget reached (${total_cost:.2f} >= ${args.budget_usd:.2f}). Stopping.")
                    break

                print(f"Running Task {task['id']} | Arm {arm} | Run {r}/{args.runs} ...", end=" ", flush=True)
                res = run_eval_task(task, arm, r, args, ts, base_ws_dir, graphstate_bin)
                cost = res["agent"]["cost_usd"]
                total_cost += cost
                results.append(res)

                status = "PASS" if res["task_success"] else "FAIL"
                print(f"{status} (broken_refs={res['broken_refs']}, syntax_errs={res['syntax_errors']}, cost=${cost:.3f})")

    # Save aggregated results
    summary_path = os.path.join(args.out, f"{ts}.json")
    with open(summary_path, "w", encoding="utf-8") as fh:
        json.dump({
            "timestamp": ts,
            "model": args.model,
            "dry_run": args.dry_run,
            "budget_usd": args.budget_usd,
            "total_cost_usd": total_cost,
            "runs_count": len(results),
            "results": results
        }, fh, indent=2)

    print("-" * 60)
    print(f"Completed {len(results)} runs. Total cost: ${total_cost:.2f}")
    print(f"Saved aggregated results to: {summary_path}")

if __name__ == "__main__":
    main()
