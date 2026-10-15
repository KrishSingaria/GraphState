from dataclasses import dataclass, field
from decimal import Decimal


@dataclass
class LineItem:
    sku: str
    price: Decimal
    qty: int = 1


@dataclass
class Cart:
    user_id: str
    items: list[LineItem] = field(default_factory=list)
