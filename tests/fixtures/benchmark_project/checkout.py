from billing import Invoice, apply_discount, calculate_total
from models import Cart


def checkout(cart: Cart, rate: float, code: str = "") -> str:
    total = calculate_total(cart, tax_rate=rate)
    if code:
        total = apply_discount(total, code)
    return Invoice(cart).render() + f" (charged {total})"
