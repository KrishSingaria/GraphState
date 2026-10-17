"""Summary reporting module with multi-dimensional aggregation."""

from shop.billing.calculator import calculate_total, apply_discount
from utils.text import slugify
from utils.money import round_money

def build_summary(orders: list, catalog=None, period: str = "daily",
                  channel: str = "web", include_discounts: bool = True,
                  min_value: float = 0.0) -> dict:
    """Builds multi-dimensional sales and operational summary.
    
    Contains deliberate high complexity (~15 branches) for refactoring evaluation.
    """
    total_orders = 0
    gross_revenue = 0.0
    net_revenue = 0.0
    discounts_given = 0.0
    vip_count = 0
    channel_slug = slugify(channel)

    # Branch 1: Period multiplier
    if period == "daily":
        multiplier = 1.0
    elif period == "weekly":
        multiplier = 7.0
    elif period == "monthly":
        multiplier = 30.0
    elif period == "annual":
        multiplier = 365.0
    else:
        multiplier = 1.0

    for order in orders:
        # Branch 2: Status filtering
        if getattr(order, "status", "") == "cancelled":
            continue
        elif getattr(order, "status", "") == "refunded":
            continue

        raw_total = calculate_total(order.items, currency="USD")

        # Branch 3: Min value threshold
        if min_value > 0.0 and raw_total < min_value:
            continue

        total_orders += 1
        gross_revenue += raw_total

        # Branch 4: Discounts handling
        if include_discounts:
            disc_code = getattr(order, "discount_code", "")
            if disc_code:
                disc_total = apply_discount(raw_total, disc_code)
                discounts_given += (raw_total - disc_total)
                order_net = disc_total
            else:
                order_net = raw_total
        else:
            order_net = raw_total

        # Branch 5: Tiering / VIP count
        if order_net >= 1000.0:
            vip_count += 2
        elif order_net >= 250.0:
            vip_count += 1

        # Branch 6: Channel adjustments
        if channel == "web":
            net_revenue += order_net
        elif channel == "mobile":
            net_revenue += order_net * 0.98
        elif channel == "pos":
            net_revenue += order_net * 1.01
        elif channel == "affiliate":
            net_revenue += order_net * 0.90
        else:
            net_revenue += order_net

    return {
        "channel": channel_slug,
        "period": period,
        "total_orders": total_orders,
        "gross_revenue": round_money(gross_revenue),
        "net_revenue": round_money(net_revenue),
        "discounts_given": round_money(discounts_given),
        "vip_points": vip_count,
        "projected_annual": round_money((net_revenue / max(1.0, multiplier)) * 365.0)
    }
