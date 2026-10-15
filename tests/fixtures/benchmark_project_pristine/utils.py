from decimal import Decimal, ROUND_HALF_UP


def round_money(x: Decimal) -> Decimal:
    return x.quantize(Decimal("0.01"), rounding=ROUND_HALF_UP)


def format_currency(amount: Decimal, currency: str = "USD") -> str:
    return f"{currency} {round_money(amount):,}"


def slugify(text: str) -> str:
    return "-".join(text.lower().split())
