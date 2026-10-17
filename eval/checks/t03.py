#!/usr/bin/env python3
"""Check for T03: Move slugify from utils/text.py to utils/slugs.py."""

import os
import re
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t03.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    slugs_path = os.path.join(run_dir, "utils", "slugs.py")
    if not os.path.isfile(slugs_path):
        print(f"File utils/slugs.py does not exist at: {slugs_path}")
        sys.exit(1)

    try:
        from utils import slugs
    except ImportError as e:
        print(f"Failed to import utils.slugs: {e}")
        sys.exit(1)

    if not hasattr(slugs, "slugify"):
        print("Function 'slugify' not found in utils.slugs")
        sys.exit(1)

    res = slugs.slugify("Hello World")
    if res != "hello-world":
        print(f"slugs.slugify returned unexpected result: {res}")
        sys.exit(1)

    # Check utils/text.py does NOT define slugify
    text_path = os.path.join(run_dir, "utils", "text.py")
    if os.path.isfile(text_path):
        with open(text_path, "r", encoding="utf-8") as fh:
            text_content = fh.read()
        if re.search(r"def\s+slugify\b", text_content):
            print("utils/text.py still contains 'def slugify'")
            sys.exit(1)

    # Check that no file imports slugify from utils.text
    bad_import = re.compile(r"from\s+utils\.text\s+import\s+.*\bslugify\b")
    for root, _, files in os.walk(run_dir):
        for f in files:
            if f.endswith(".py"):
                path = os.path.join(root, f)
                with open(path, "r", encoding="utf-8", errors="ignore") as fh:
                    for line_no, line in enumerate(fh, 1):
                        if bad_import.search(line):
                            rel = os.path.relpath(path, run_dir)
                            print(f"Found stale import at {rel}:{line_no}: {line.strip()}")
                            sys.exit(1)

    print("T03 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
