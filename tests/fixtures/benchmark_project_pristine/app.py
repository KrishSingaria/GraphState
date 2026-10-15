from decimal import Decimal

from billing import calculate_total
from models import Cart, LineItem
from utils import format_currency

cart = Cart("u1", [LineItem("book", Decimal("12.50"), 2)])
total = calculate_total(cart)
print(format_currency(total))
