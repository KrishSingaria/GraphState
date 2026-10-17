"""Product models and catalog registry."""

from utils.text import slugify
from utils.money import round_money

class Product:
    """Individual product specification."""

    def __init__(self, sku: str, name: str, price: float, category_id: str = ""):
        self.sku = sku
        self.name = name
        self.price = round_money(price)
        self.slug = slugify(name)
        self.category_id = category_id

    def discounted_price(self, percent: float) -> float:
        """Returns price after applying percentage discount."""
        factor = max(0.0, 1.0 - (percent / 100.0))
        return round_money(self.price * factor)

class ProductCatalog:
    """Product catalog repository."""

    def __init__(self):
        self._products = {}

    def add_product(self, product: Product):
        self._products[product.sku] = product

    def get_product(self, sku: str) -> Product:
        return self._products.get(sku)

    def list_all(self) -> list:
        return list(self._products.values())
