#!/usr/bin/env python3
"""Check for T10: Input validation in orders.checkout."""

import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t10.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    try:
        from shop.orders import checkout as checkout_mod
        from shop.orders.order import Order, OrderItem
    except ImportError as e:
        print(f"Failed to import shop.orders modules: {e}")
        sys.exit(1)

    # Test 1: Empty cart should raise ValueError
    empty_order = Order("ORD-EMPTY", "CUST-1", "USD")
    raised_empty = False
    try:
        checkout_mod.checkout(empty_order)
    except ValueError:
        raised_empty = True
    except Exception as e:
        print(f"Empty order raised {type(e).__name__} instead of ValueError: {e}")
        sys.exit(1)

    if not raised_empty:
        print("checkout(empty_order) did not raise ValueError")
        sys.exit(1)

    # Test 2: Negative quantity should raise ValueError
    neg_order = Order("ORD-NEG", "CUST-1", "USD")
    neg_order.add_item(OrderItem("SKU-1", "Item", 20.0, -1))
    raised_neg = False
    try:
        checkout_mod.checkout(neg_order)
    except ValueError:
        raised_neg = True
    except Exception as e:
        print(f"Negative quantity raised {type(e).__name__} instead of ValueError: {e}")
        sys.exit(1)

    if not raised_neg:
        print("checkout(order_with_negative_qty) did not raise ValueError")
        sys.exit(1)

    # Test 3: Normal order must still succeed
    valid_order = Order("ORD-VALID", "CUST-1", "USD")
    valid_order.add_item(OrderItem("SKU-1", "Item", 20.0, 2))
    try:
        res = checkout_mod.checkout(valid_order)
        if res.get("order_id") != "ORD-VALID":
            print(f"Valid order returned invalid response: {res}")
            sys.exit(1)
    except Exception as e:
        print(f"Valid order failed with: {e}")
        sys.exit(1)

    print("T10 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
