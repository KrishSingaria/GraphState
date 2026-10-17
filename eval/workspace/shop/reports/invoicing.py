"""Invoice batch and reporting utilities."""

from shop.billing.calculator import calculate_total
from utils.money import format_currency

def generate_invoice_batch(orders: list) -> list:
    """Generates summarized invoice receipts for a batch of orders."""
    invoices = []
    for order in orders:
        tot = calculate_total(order.items, currency=order.currency)
        formatted = format_currency(tot, order.currency)
        invoices.append({
            "order_id": order.order_id,
            "customer_id": order.customer_id,
            "currency": order.currency,
            "total": tot,
            "formatted_total": formatted
        })
    return invoices
