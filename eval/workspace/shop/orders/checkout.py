"""Checkout and payment processing service."""

from shop.billing.calculator import calculate_total, apply_discount
from utils.money import round_money

def calculate_shipping(order, method: str = "standard") -> float:
    """Calculates shipping fees based on order size and method."""
    order_total = calculate_total(order.items, currency=order.currency)
    if order_total > 100.0:
        return 0.0
    if method == "express":
        return 15.0
    return 5.0

def checkout(order, discount_code: str = "") -> dict:
    """Executes order checkout, calculating totals and applying discounts."""
    subtotal = calculate_total(order.items)
    discounted = apply_discount(subtotal, discount_code)
    shipping = calculate_shipping(order, "standard")
    final_amount = round_money(discounted + shipping)
    order.status = "confirmed"

    return {
        "order_id": order.order_id,
        "subtotal": subtotal,
        "discounted": discounted,
        "shipping": shipping,
        "total": final_amount,
        "currency": order.currency
    }
