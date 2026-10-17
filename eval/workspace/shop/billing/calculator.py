"""Core price and tax calculation functions."""

from utils.money import round_money
from shop.billing.discounts import compute_discount_amount

def calculate_tax(subtotal: float, tax_rate: float = 0.05) -> float:
    """Calculates tax on a subtotal."""
    return round_money(subtotal * tax_rate)

def apply_discount(total: float, discount_code: str = "") -> float:
    """Applies a discount code to a total amount."""
    if not discount_code:
        return round_money(total)
    discount = compute_discount_amount(total, discount_code)
    return round_money(max(0.0, total - discount))

def calculate_total(cart: list, currency: str = "USD") -> float:
    """Calculates total for a list of cart/order items."""
    raw_subtotal = 0.0
    for item in cart:
        if isinstance(item, dict):
            p = float(item.get("price", 0.0))
            q = int(item.get("quantity", 1))
        else:
            p = float(getattr(item, "price", 0.0))
            q = int(getattr(item, "quantity", 1))
        raw_subtotal += p * q

    subtotal = round_money(raw_subtotal)
    tax = calculate_tax(subtotal)
    final_total = round_money(subtotal + tax)
    return final_total
