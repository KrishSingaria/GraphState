from billing import calculate_total
from utils import format_currency


def daily_report(carts) -> str:
    lines = []
    for cart in carts:
        lines.append(f"{cart.user_id}: {format_currency(calculate_total(cart))}")
    return "\n".join(lines)
