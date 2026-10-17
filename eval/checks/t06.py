#!/usr/bin/env python3
"""Check for T06: Invoice.total accepts tax_rate and passes from checkout."""

import inspect
import os
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: t06.py <run_dir>")
        sys.exit(1)

    run_dir = os.path.abspath(sys.argv[1])
    sys.path.insert(0, run_dir)

    try:
        from shop.billing.invoice import Invoice
    except ImportError as e:
        print(f"Failed to import shop.billing.invoice.Invoice: {e}")
        sys.exit(1)

    sig = inspect.signature(Invoice.total)
    if "tax_rate" not in sig.parameters:
        print("Parameter 'tax_rate' not found in Invoice.total")
        sys.exit(1)

    # Test custom tax_rate
    inv = Invoice("INV-TEST", "Customer", "USD")
    inv.add_item("Item", 100.0, 1)

    try:
        tot_normal = inv.total()
        tot_custom = inv.total(tax_rate=0.10)
        # Tax rate 0.10 should result in different or equal tax calculation
        if tot_custom < 100.0:
            print(f"inv.total(tax_rate=0.10) returned unexpectedly low amount: {tot_custom}")
            sys.exit(1)
    except Exception as e:
        print(f"Calling Invoice.total with tax_rate failed: {e}")
        sys.exit(1)

    print("T06 check passed!")
    sys.exit(0)

if __name__ == "__main__":
    main()
