"""Inventory management and stock reservations."""

class InventoryItem:
    """Stock tracking for a specific product SKU."""

    def __init__(self, sku: str, quantity: int, reserved: int = 0):
        self.sku = sku
        self.quantity = quantity
        self.reserved = reserved

    @property
    def available(self) -> int:
        return max(0, self.quantity - self.reserved)

class InventoryManager:
    """Maintains real-time stock levels across products."""

    def __init__(self):
        self._items = {}

    def set_stock(self, sku: str, qty: int):
        self._items[sku] = InventoryItem(sku, qty, 0)

    def check_stock(self, sku: str, required_qty: int) -> bool:
        item = self._items.get(sku)
        if not item:
            return False
        return item.available >= required_qty

    def reserve_stock(self, sku: str, qty: int) -> bool:
        if not self.check_stock(sku, qty):
            return False
        self._items[sku].reserved += qty
        return True

    def release_stock(self, sku: str, qty: int):
        if sku in self._items:
            self._items[sku].reserved = max(0, self._items[sku].reserved - qty)

    def deduct_stock(self, sku: str, qty: int):
        if sku in self._items:
            self._items[sku].reserved = max(0, self._items[sku].reserved - qty)
            self._items[sku].quantity = max(0, self._items[sku].quantity - qty)
