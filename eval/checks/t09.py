#!/usr/bin/env python3
"""Check for T09: Refactoring reports.build_summary into helpers."""

import ast
import os
import sys

def count_decision_points(node):
    points = 0
    for child in ast.walk(node):
        if isinstance(child, (ast.If, ast.While, ast.For, ast.AsyncFor, ast.ExceptHandler, ast.With, ast.AsyncWith)):
            points += 1
        elif isinstance(child, ast.BoolOp):
            points += len(child.values) - 1
    return points

def main():
    if len(sys.argv) < 2:
        print("Usage: t09.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    summary_py = os.path.join(run_dir, "shop", "reports", "summary.py")
    if not os.path.isfile(summary_py):
        print(f"File shop/reports/summary.py not found: {summary_py}")
        sys.exit(1)

    with open(summary_py, "r", encoding="utf-8") as fh:
        code = fh.read()

    tree = ast.parse(code)
    build_summary_node = None
    function_count = 0

    for item in tree.body:
        if isinstance(item, ast.FunctionDef):
            function_count += 1
            if item.name == "build_summary":
                build_summary_node = item

    if not build_summary_node:
        print("Function 'build_summary' not found in shop/reports/summary.py")
        sys.exit(1)

    # In baseline, build_summary has complexity ~15 and is the only function.
    # Refactoring requires splitting into smaller helpers (at least 2 functions in the module).
    if function_count < 2:
        print("shop/reports/summary.py has not been split into helper functions")
        sys.exit(1)

    # Verify behavioral correctness
    try:
        from shop.reports.summary import build_summary
        from shop.orders.order import Order, OrderItem

        order = Order("ORD-1", "CUST-1", "USD")
        order.add_item(OrderItem("SKU-1", "Test", 50.0, 2))
        res = build_summary([order], period="daily", channel="web")
        if res.get("total_orders") != 1:
            print(f"build_summary returned invalid total_orders: {res}")
            sys.exit(1)
    except Exception as e:
        print(f"Calling refactored build_summary failed: {e}")
        sys.exit(1)

    print("T09 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
