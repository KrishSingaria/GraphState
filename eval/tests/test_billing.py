"""Tests for billing, taxes, discounts, and invoices."""

import unittest
from shop.billing.calculator import calculate_tax, apply_discount, calculate_total
from shop.billing.discounts import get_discount_rate, compute_discount_amount
from shop.billing.invoice import BaseInvoice, Invoice

class TestBilling(unittest.TestCase):

    def test_calculate_tax_default(self):
        self.assertEqual(calculate_tax(100.0), 5.0)
        self.assertEqual(calculate_tax(50.0), 2.50)

    def test_calculate_tax_custom_rate(self):
        self.assertEqual(calculate_tax(100.0, 0.10), 10.0)
        self.assertEqual(calculate_tax(50.0, 0.0), 0.0)

    def test_discount_rates(self):
        self.assertEqual(get_discount_rate("VIP20"), 0.20)
        self.assertEqual(get_discount_rate("SUMMER10"), 0.10)
        self.assertEqual(get_discount_rate("UNKNOWN"), 0.0)

    def test_apply_discount(self):
        self.assertEqual(apply_discount(100.0, "VIP20"), 80.0)
        self.assertEqual(apply_discount(50.0, "SUMMER10"), 45.0)
        self.assertEqual(apply_discount(100.0, ""), 100.0)

    def test_calculate_total_empty(self):
        self.assertEqual(calculate_total([]), 0.0)

    def test_calculate_total_dicts(self):
        cart = [
            {"price": 10.0, "quantity": 2},
            {"price": 5.0, "quantity": 1}
        ]
        # subtotal: 25.0, tax 5%: 1.25 -> 26.25
        self.assertEqual(calculate_total(cart), 26.25)

    def test_calculate_total_currency_kwarg(self):
        cart = [{"price": 100.0, "quantity": 1}]
        self.assertEqual(calculate_total(cart, currency="EUR"), 105.0)

    def test_base_invoice(self):
        inv = BaseInvoice("INV-001", "Acme Corp", "USD")
        inv.add_item("Consulting", 100.0, 2)
        self.assertEqual(inv.calculate_subtotal(), 200.0)
        self.assertIn("INVOICE #INV-001 FOR Acme Corp", inv.render())

    def test_invoice_subclass_discount(self):
        inv = Invoice("INV-002", "Globex", "USD", discount_code="VIP20")
        inv.add_item("Software License", 100.0, 1)
        # subtotal: 100, tax 5%: 5 -> 105; 20% discount on 105 = 84.0
        self.assertEqual(inv.total(), 84.0)

    def test_invoice_render_includes_super(self):
        inv = Invoice("INV-003", "Stark Industries", "EUR")
        inv.add_item("Arc Reactor", 500.0, 1)
        rendered = inv.render()
        self.assertIn("INVOICE #INV-003 FOR Stark Industries", rendered)
        self.assertIn("Total: €", rendered)
        self.assertIn("Items: 1", rendered)

if __name__ == "__main__":
    unittest.main()
