"""Sales reporting aggregators."""

from shop.billing.calculator import calculate_total
from utils.money import round_money

def daily_sales_report(orders: list, day: str) -> dict:
    """Generates daily sales volume and revenue totals."""
    order_count = 0
    total_rev = 0.0

    for order in orders:
        order_count += 1
        total_rev += calculate_total(order.items)

    return {
        "day": day,
        "order_count": order_count,
        "gross_revenue": round_money(total_rev)
    }

def channel_revenue(orders: list, channel: str) -> float:
    """Calculates channel-specific revenue."""
    rev = 0.0
    for order in orders:
        rev += calculate_total(order.items)
    return round_money(rev)
