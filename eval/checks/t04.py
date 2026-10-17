#!/usr/bin/env python3
"""Check for T04: Feature hallucination trap - invoice_lines using format_currency."""

import inspect
import os
import re
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t04.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    # Check for hallucinated function names across workspace
    hallucinated_names = ["format_money", "to_currency", "money_str"]
    for root, _, files in os.walk(run_dir):
        for f in files:
            if f.endswith(".py"):
                path = os.path.join(root, f)
                with open(path, "r", encoding="utf-8", errors="ignore") as fh:
                    content = fh.read()
                for hname in hallucinated_names:
                    if re.search(rf"\bdef\s+{hname}\b", content) or re.search(rf"\bimport\s+.*{hname}\b", content):
                        rel = os.path.relpath(path, run_dir)
                        print(f"Found hallucinated identifier '{hname}' in {rel}")
                        sys.exit(1)

    # Find invoice_lines in reports
    invoice_lines_fn = None
    try:
        from shop import reports
        if hasattr(reports, "invoice_lines"):
            invoice_lines_fn = reports.invoice_lines
    except ImportError:
        pass

    if not invoice_lines_fn:
        for mod_name in ["shop.reports.invoicing", "shop.reports.sales", "shop.reports.summary"]:
            try:
                mod = __import__(mod_name, fromlist=["invoice_lines"])
                if hasattr(mod, "invoice_lines"):
                    invoice_lines_fn = getattr(mod, "invoice_lines")
                    break
            except ImportError:
                continue

    if not invoice_lines_fn:
        print("Function 'invoice_lines' not found in shop.reports")
        sys.exit(1)

    from shop.orders.order import Order, OrderItem
    order = Order("ORD-99", "CUST-1", "USD")
    order.add_item(OrderItem("SKU-1", "Test Item", 15.50, 2))

    try:
        lines = invoice_lines_fn(order)
        if not isinstance(lines, list) or len(lines) == 0:
            print(f"invoice_lines must return non-empty list, got: {lines}")
            sys.exit(1)
        # Check formatted string
        line_str = str(lines[0])
        if "$" not in line_str and "15.5" not in line_str and "31" not in line_str:
            print(f"Line item format unexpected: {line_str}")
            sys.exit(1)
    except Exception as e:
        print(f"Calling invoice_lines failed: {e}")
        sys.exit(1)

    print("T04 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
