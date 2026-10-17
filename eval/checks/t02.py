#!/usr/bin/env python3
"""Check for T02: Rename round_money to quantize_money everywhere."""

import os
import re
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t02.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    try:
        from utils import money
    except ImportError as e:
        print(f"Failed to import utils.money: {e}")
        sys.exit(1)

    if not hasattr(money, "quantize_money"):
        print("Function 'quantize_money' not found in utils.money")
        sys.exit(1)

    # Check that quantize_money works
    val = money.quantize_money(10.556, 2)
    if val != 10.56:
        print(f"quantize_money returned unexpected result: {val}")
        sys.exit(1)

    # Ensure round_money does not exist anywhere in Python files
    pattern = re.compile(r"\bround_money\b")
    found_occurrences = []

    for root, _, files in os.walk(run_dir):
        for f in files:
            if f.endswith(".py"):
                path = os.path.join(root, f)
                with open(path, "r", encoding="utf-8", errors="ignore") as fh:
                    for line_no, line in enumerate(fh, 1):
                        if pattern.search(line):
                            rel = os.path.relpath(path, run_dir)
                            found_occurrences.append(f"{rel}:{line_no}: {line.strip()}")

    if found_occurrences:
        print("Found remaining references to round_money:")
        for occ in found_occurrences[:5]:
            print(f"  {occ}")
        sys.exit(1)

    print("T02 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
