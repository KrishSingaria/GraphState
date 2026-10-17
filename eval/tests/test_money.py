"""Tests for money manipulation and formatting utilities."""

import unittest
from utils.money import round_money, format_currency

class TestMoneyUtils(unittest.TestCase):

    def test_round_money_basic(self):
        self.assertEqual(round_money(10.556), 10.56)
        self.assertEqual(round_money(10.554), 10.55)
        self.assertEqual(round_money(10), 10.0)

    def test_round_money_decimals(self):
        self.assertEqual(round_money(10.12345, 3), 10.123)
        self.assertEqual(round_money(10.12345, 0), 10.0)

    def test_format_currency_usd(self):
        self.assertEqual(format_currency(25.5), "$25.50")
        self.assertEqual(format_currency(100), "$100.00")

    def test_format_currency_eur(self):
        self.assertEqual(format_currency(42.99, "EUR"), "€42.99")

    def test_format_currency_gbp(self):
        self.assertEqual(format_currency(15.0, "GBP"), "£15.00")

    def test_format_currency_jpy(self):
        self.assertEqual(format_currency(1500.5, "JPY"), "¥1500")

    def test_format_currency_other(self):
        self.assertEqual(format_currency(50.0, "CAD"), "CAD 50.00")

if __name__ == "__main__":
    unittest.main()
