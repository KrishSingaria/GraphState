#!/usr/bin/env python3
"""E2E Test Runner for GraphState MCP Server.

Spawns `graphstate serve --root <tmp_dir>`, feeds JSON-RPC messages from *.jsonl scripts,
and verifies responses against expected JSON subsets.
Python 3 standard library only.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time


def subset_matches(expected, actual):
    """Recursively checks if actual matches expected.
    If expected is "*", any non-null value matches.
    If expected is a dict, all keys in expected must exist in actual and match recursively.
    If expected is a list, actual must be a list of at least the same length and elements match.
    """
    if expected == "*":
        return True

    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return False
        for k, v in expected.items():
            if k not in actual:
                return False
            if not subset_matches(v, actual[k]):
                return False
        return True

    if isinstance(expected, list):
        if not isinstance(actual, list):
            return False
        if len(actual) < len(expected):
            return False
        for exp_item, act_item in zip(expected, actual):
            if not subset_matches(exp_item, act_item):
                return False
        return True

    return expected == actual


def run_script(binary_path, workspace_path, script_path):
    print(f"\n--- Running scenario: {os.path.basename(script_path)} ---")
    with open(script_path, "r", encoding="utf-8") as f:
        lines = [line.rstrip("\r\n") for line in f if line.strip() and not line.startswith("#")]

    cmd = [binary_path, "serve", "--root", workspace_path]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        encoding="utf-8"
    )

    step = 0
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("> "):
            send_payload = line[2:]
            step += 1
            # Send message
            proc.stdin.write(send_payload + "\n")
            proc.stdin.flush()
            i += 1
        elif line.startswith("< "):
            expect_payload = json.loads(line[2:])
            # Read one response line from server
            resp_line = proc.stdout.readline()
            if not resp_line:
                err = proc.stderr.read()
                print(f"[FAIL] Unexpected EOF from server at step {step}. Stderr: {err}")
                proc.kill()
                return False

            try:
                actual = json.loads(resp_line.strip())
            except json.JSONDecodeError as e:
                print(f"[FAIL] Server emitted non-JSON line: {resp_line.strip()} ({e})")
                proc.kill()
                return False

            if not subset_matches(expect_payload, actual):
                print(f"[FAIL] Mismatch at step {step} in {os.path.basename(script_path)}!")
                print(f"  Expected subset: {json.dumps(expect_payload, indent=2)}")
                print(f"  Actual response: {json.dumps(actual, indent=2)}")
                proc.kill()
                return False
            else:
                print(f"  Step {step}: OK")
            i += 1
        else:
            i += 1

    proc.stdin.close()
    proc.stdout.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()

    print(f"[PASS] Scenario {os.path.basename(script_path)} passed.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Run GraphState MCP E2E tests")
    parser.add_argument("--bin", required=True, help="Path to graphstate executable")
    parser.add_argument("--fixtures", required=True, help="Path to benchmark_project fixture")
    parser.add_argument("--scripts", default="", help="Path to scripts directory or specific jsonl file")
    args = parser.parse_args()

    bin_path = os.path.abspath(args.bin)
    if not os.path.isfile(bin_path):
        print(f"Error: binary not found at {bin_path}", file=sys.stderr)
        return 1

    fixtures_path = os.path.abspath(args.fixtures)
    if not os.path.isdir(fixtures_path):
        print(f"Error: fixtures directory not found at {fixtures_path}", file=sys.stderr)
        return 1

    scripts_dir = os.path.abspath(args.scripts) if args.scripts else os.path.dirname(__file__)
    script_files = []
    if os.path.isfile(scripts_dir) and scripts_dir.endswith(".jsonl"):
        script_files.append(scripts_dir)
    elif os.path.isdir(scripts_dir):
        for f in sorted(os.listdir(scripts_dir)):
            if f.endswith(".jsonl"):
                script_files.append(os.path.join(scripts_dir, f))

    if not script_files:
        print(f"Error: no .jsonl test scripts found in {scripts_dir}", file=sys.stderr)
        return 1

    passed = 0
    failed = 0
    for s in script_files:
        tmp_dir = tempfile.mkdtemp(prefix="gs_e2e_")
        try:
            workspace = os.path.join(tmp_dir, "benchmark_project")
            shutil.copytree(fixtures_path, workspace)
            if run_script(bin_path, workspace, s):
                passed += 1
            else:
                failed += 1
        finally:
            shutil.rmtree(tmp_dir, ignore_errors=True)

    print(f"\n==========================================")
    print(f"E2E Summary: {passed} passed, {failed} failed")
    print(f"==========================================")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
