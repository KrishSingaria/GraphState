#!/usr/bin/env python3
"""Check for T08: TypeScript totalLabel currency required parameter."""

import os
import re
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t08.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    format_ts = os.path.join(run_dir, "web", "format.ts")

    if not os.path.isfile(format_ts):
        print(f"File web/format.ts not found: {format_ts}")
        sys.exit(1)

    with open(format_ts, "r", encoding="utf-8") as fh:
        format_content = fh.read()

    # Match: totalLabel(..., currency: string) without default or ?
    sig_match = re.search(r"function\s+totalLabel\s*\(([^)]+)\)", format_content)
    if not sig_match:
        print("Function totalLabel signature not found in web/format.ts")
        sys.exit(1)

    params_str = sig_match.group(1)
    # Check if currency has default '= "..."' or '?:'
    if re.search(r"currency\s*\?:\s*string", params_str) or re.search(r"currency\s*:\s*string\s*=", params_str):
        print(f"totalLabel currency parameter is not required: {params_str}")
        sys.exit(1)

    # Check callers in web/
    call_single_arg = re.compile(r"\btotalLabel\s*\(\s*[^,()]+\s*\)")
    web_dir = os.path.join(run_dir, "web")
    found_single_arg = []

    for f in os.listdir(web_dir):
        if f.endswith(".ts") and f != "format.ts":
            path = os.path.join(web_dir, f)
            with open(path, "r", encoding="utf-8") as fh:
                for line_no, line in enumerate(fh, 1):
                    if call_single_arg.search(line):
                        found_single_arg.append(f"{f}:{line_no}: {line.strip()}")

    if found_single_arg:
        print("Found callers passing only 1 argument to totalLabel:")
        for call in found_single_arg:
            print(f"  {call}")
        sys.exit(1)

    print("T08 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
