"""Invoice generation with class hierarchy and polymorphism."""

from utils.money import round_money, format_currency
from shop.billing.calculator import calculate_total, apply_discount

class BaseInvoice:
    """Base class for invoices."""

    def __init__(self, invoice_id: str, customer_name: str, currency: str = "USD"):
        self.invoice_id = invoice_id
        self.customer_name = customer_name
        self.currency = currency
        self.items = []

    def add_item(self, description: str, price: float, quantity: int = 1):
        self.items.append({"description": description, "price": price, "quantity": quantity})

    def calculate_subtotal(self) -> float:
        """Calculates raw subtotal for invoice items."""
        total = 0.0
        for item in self.items:
            total += item["price"] * item["quantity"]
        return round_money(total)

    def render(self) -> str:
        """Renders invoice header."""
        return f"INVOICE #{self.invoice_id} FOR {self.customer_name}"

class Invoice(BaseInvoice):
    """Concrete invoice supporting discounts, tax rates, and full rendering."""

    def __init__(self, invoice_id: str, customer_name: str, currency: str = "USD", discount_code: str = ""):
        super().__init__(invoice_id, customer_name, currency)
        self.discount_code = discount_code

    def total(self, tax_rate: float = 0.05) -> float:
        """Calculates final invoice total with tax and discounts."""
        sub = self.calculate_subtotal()
        base_total = calculate_total(self.items, currency=self.currency)
        discounted = apply_discount(base_total, self.discount_code)
        return round_money(discounted)

    def render(self) -> str:
        """Renders complete invoice text including base header and line items."""
        header = super().render()
        tot = self.total()
        formatted_tot = format_currency(tot, self.currency)
        lines = [header, f"Total: {formatted_tot}", f"Items: {len(self.items)}"]
        return "\n".join(lines)
