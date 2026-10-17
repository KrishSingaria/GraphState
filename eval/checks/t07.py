#!/usr/bin/env python3
"""Check for T07: Greenfield package shop/returns."""

import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t07.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    returns_dir = os.path.join(run_dir, "shop", "returns")
    if not os.path.isdir(returns_dir):
        print(f"Package directory shop/returns does not exist: {returns_dir}")
        sys.exit(1)

    init_file = os.path.join(returns_dir, "__init__.py")
    if not os.path.isfile(init_file):
        print(f"shop/returns/__init__.py does not exist: {init_file}")
        sys.exit(1)

    try:
        from shop import returns
    except ImportError as e:
        print(f"Failed to import shop.returns: {e}")
        sys.exit(1)

    # Check ReturnRequest model
    found_model = hasattr(returns, "ReturnRequest")
    if not found_model:
        for mod_name in ["shop.returns.models", "shop.returns.return_request"]:
            try:
                mod = __import__(mod_name, fromlist=["ReturnRequest"])
                if hasattr(mod, "ReturnRequest"):
                    found_model = True
                    break
            except ImportError:
                continue

    if not found_model:
        print("ReturnRequest model not found in shop.returns")
        sys.exit(1)

    # Check create_return and approve_return
    found_create = False
    found_approve = False
    for mod_candidate in [returns] + [getattr(returns, m, None) for m in dir(returns)]:
        if mod_candidate:
            if hasattr(mod_candidate, "create_return"):
                found_create = True
            if hasattr(mod_candidate, "approve_return"):
                found_approve = True

    if not found_create or not found_approve:
        for mod_name in ["shop.returns.service", "shop.returns.services"]:
            try:
                mod = __import__(mod_name, fromlist=["create_return", "approve_return"])
                if hasattr(mod, "create_return"):
                    found_create = True
                if hasattr(mod, "approve_return"):
                    found_approve = True
            except ImportError:
                continue

    if not found_create or not found_approve:
        print(f"Returns services missing: create_return={found_create}, approve_return={found_approve}")
        sys.exit(1)

    print("T07 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
