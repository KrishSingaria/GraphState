"""E-commerce shop demo application entry point."""

import sys
from shop.billing.calculator import calculate_total, apply_discount
from utils.money import format_currency
from shop.reports.summary import build_summary
from shop.orders.order import Order, OrderItem

def run_checkout_demo():
    """Runs a simulated cart checkout demo."""
    cart = [
        {"price": 49.99, "quantity": 2},
        {"price": 15.00, "quantity": 1}
    ]
    tot = calculate_total(cart, "USD")
    disc = apply_discount(tot, "VIP20")
    print(f"Subtotal: {format_currency(tot)}")
    print(f"VIP Discounted: {format_currency(disc)}")
    return disc

def run_summary_demo():
    """Runs a summary report demo."""
    order = Order("ORD-001", "CUST-99", "USD")
    order.add_item(OrderItem("SKU-1", "Widget", 25.00, 4))
    summary = build_summary([order])
    print(f"Summary total orders: {summary['total_orders']}")
    return summary

def main():
    commands = {
        "checkout": run_checkout_demo,
        "summary": run_summary_demo
    }
    cmd = sys.argv[1] if len(sys.argv) > 1 else "checkout"
    if cmd in commands:
        commands[cmd]()
    else:
        print(f"Unknown command: {cmd}")

if __name__ == "__main__":
    main()
