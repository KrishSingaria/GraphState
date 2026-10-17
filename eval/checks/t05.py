#!/usr/bin/env python3
"""Check for T05: Removal & migration of apply_discount to Discounts.apply."""

import os
import re
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t05.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    try:
        from shop.billing import discounts
    except ImportError as e:
        print(f"Failed to import shop.billing.discounts: {e}")
        sys.exit(1)

    if not hasattr(discounts, "Discounts"):
        print("Class 'Discounts' not found in shop.billing.discounts")
        sys.exit(1)

    disc_cls = getattr(discounts, "Discounts")
    try:
        disc_obj = disc_cls()
        if not hasattr(disc_obj, "apply"):
            print("Method 'apply' not found on Discounts instance")
            sys.exit(1)
        res = disc_obj.apply("VIP20", 100.0)
        if res != 80.0:
            print(f"Discounts().apply('VIP20', 100.0) returned {res}, expected 80.0")
            sys.exit(1)
    except Exception as e:
        print(f"Instantiating/calling Discounts failed: {e}")
        sys.exit(1)

    # Check that apply_discount is removed or replaced across callers
    bad_call = re.compile(r"\bapply_discount\s*\(")
    callers = [
        os.path.join(run_dir, "shop", "orders", "checkout.py"),
        os.path.join(run_dir, "shop", "billing", "invoice.py"),
        os.path.join(run_dir, "shop", "reports", "summary.py"),
        os.path.join(run_dir, "app.py")
    ]

    for path in callers:
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as fh:
                content = fh.read()
            if bad_call.search(content):
                rel = os.path.relpath(path, run_dir)
                print(f"Found unmigrated call to apply_discount in {rel}")
                sys.exit(1)

    print("T05 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
