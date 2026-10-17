"""Customer order history tracking."""

from shop.billing.calculator import calculate_total
from shop.orders.order import Order

class OrderHistory:
    """Historical archive of completed orders."""

    def __init__(self):
        self._orders = {}

    def record_order(self, order: Order):
        self._orders[order.order_id] = order

    def get_order(self, order_id: str) -> Order:
        return self._orders.get(order_id)

    def total_spent(self, customer_id: str) -> float:
        """Computes aggregate amount spent by customer across all orders."""
        spent = 0.0
        for order in self._orders.values():
            if order.customer_id == customer_id:
                spent += calculate_total(order.items, "USD")
        return round(spent, 2)

    def reorder(self, order_id: str) -> float:
        """Computes expected total for reordering past purchase."""
        order = self._orders.get(order_id)
        if not order:
            return 0.0
        return calculate_total(order.items, currency=order.currency)
