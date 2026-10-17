"""Order and order item data models."""

from utils.text import slugify
from utils.money import round_money

class OrderItem:
    """Individual line item within an order."""

    def __init__(self, sku: str, title: str, price: float, quantity: int = 1):
        self.sku = sku
        self.title = title
        self.price = round_money(price)
        self.quantity = quantity

    def subtotal(self) -> float:
        return round_money(self.price * self.quantity)

class Order:
    """Customer purchase order."""

    def __init__(self, order_id: str, customer_id: str, currency: str = "USD"):
        self.order_id = order_id
        self.customer_id = customer_id
        self.currency = currency
        self.status = "pending"
        self.items = []
        self.discount_code = ""
        self.tax_rate = 0.05
        self.tracking_slug = slugify(f"{customer_id}-{order_id}")

    def add_item(self, item: OrderItem):
        self.items.append(item)

    def total_items(self) -> int:
        return sum(item.quantity for item in self.items)
