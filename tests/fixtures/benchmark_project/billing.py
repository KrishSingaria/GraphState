from decimal import Decimal

from models import Cart
from utils import round_money


def calculate_total(cart: Cart, tax_rate: float = 0.1) -> Decimal:
    """Total including tax."""
    subtotal = sum((i.price * i.qty for i in cart.items), Decimal("0"))
    return round_money(subtotal * (1 + Decimal(str(tax_rate))))


def apply_discount(total: Decimal, code: str) -> Decimal:
    if code == "WELCOME10":
        return round_money(total * Decimal("0.9"))
    return total


class Invoice:
    def __init__(self, cart: Cart):
        self.cart = cart

    def total(self) -> Decimal:
        return calculate_total(self.cart)

    def render(self) -> str:
        return f"Invoice for {self.cart.user_id}: {self.total()}"
