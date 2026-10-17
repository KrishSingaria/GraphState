"""Discounts and promotion helpers."""

from utils.money import round_money

DISCOUNT_CODES = {
    "SUMMER10": 0.10,
    "VIP20": 0.20,
    "WELCOME5": 0.05,
    "HALFPRICE": 0.50,
}

def get_discount_rate(code: str) -> float:
    """Returns discount multiplier for a given discount coupon."""
    if not code:
        return 0.0
    return DISCOUNT_CODES.get(code.upper(), 0.0)

def compute_discount_amount(total: float, code: str) -> float:
    """Computes exact discount amount for a code."""
    rate = get_discount_rate(code)
    return round_money(total * rate)
