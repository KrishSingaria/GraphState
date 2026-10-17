#!/usr/bin/env python3
"""Demo reset utility for GraphState.

Resets benchmark_project to pristine state for live demonstration and automated
walkthrough tests.
"""

import argparse
import os
import shutil
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser(description="Reset benchmark_project to pristine state")
    parser.add_argument("--target", default="tests/fixtures/benchmark_project", help="Target project to reset")
    parser.add_argument("--pristine", default="tests/fixtures/benchmark_project_pristine", help="Pristine source directory")
    parser.add_argument("--wipe-memory", action="store_true", help="Also wipe memory and pitfalls")
    parser.add_argument("--reindex", action="store_true", default=True, help="Re-index workspace after reset")
    args = parser.parse_args()

    target_dir = os.path.abspath(args.target)
    pristine_dir = os.path.abspath(args.pristine)

    if not os.path.isdir(pristine_dir):
        print(f"Error: Pristine fixture directory not found: {pristine_dir}")
        sys.exit(1)

    print(f"Resetting {target_dir} from {pristine_dir}...")

    # Copy files over
    shutil.copytree(pristine_dir, target_dir, dirs_exist_ok=True)

    # Locate graphstate binary
    candidates = [
        "build/windows-msvc-release/graphstate.exe",
        "build/windows-msvc-release/graphstate",
        "build/graphstate.exe",
        "build/graphstate"
    ]
    gs_bin = None
    for c in candidates:
        if os.path.isfile(c):
            gs_bin = os.path.abspath(c)
            break

    if gs_bin and args.wipe_memory:
        print("Wiping memory and pitfalls...")
        subprocess.run([gs_bin, "forget", "--root", target_dir, "--all-memory", "--pitfalls", "--yes"], check=False)

    if gs_bin and args.reindex:
        print("Re-indexing workspace...")
        subprocess.run([gs_bin, "index", "--root", target_dir], check=False)

    print("Demo reset complete.")

if __name__ == "__main__":
    main()
