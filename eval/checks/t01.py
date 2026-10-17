#!/usr/bin/env python3
"""Check for T01: Required currency parameter in calculate_total."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t01.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    try:
        from shop.billing import calculator
    except ImportError as e:
        print(f"Failed to import shop.billing.calculator: {e}")
        sys.exit(1)

    sig = inspect.signature(calculator.calculate_total)
    params = list(sig.parameters.values())

    if len(params) < 2:
        print(f"calculate_total has {len(params)} parameters, expected at least 2")
        sys.exit(1)

    curr_param = None
    for p in params:
        if p.name == "currency":
            curr_param = p
            break

    if not curr_param:
        print("Parameter 'currency' not found in calculate_total")
        sys.exit(1)

    if curr_param.default != inspect.Parameter.empty:
        print(f"Parameter 'currency' must be required (found default: {curr_param.default})")
        sys.exit(1)

    # Test call with required positional/keyword arguments
    try:
        res = calculator.calculate_total([{"price": 10.0, "quantity": 1}], "USD")
        if res <= 0:
            print("calculate_total returned non-positive total")
            sys.exit(1)
    except Exception as e:
        print(f"Calling calculate_total failed: {e}")
        sys.exit(1)

    print("T01 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
