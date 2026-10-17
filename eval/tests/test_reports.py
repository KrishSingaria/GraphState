"""Tests for sales and summary reporting."""

import unittest
from shop.orders.order import Order, OrderItem
from shop.reports.sales import daily_sales_report, channel_revenue
from shop.reports.summary import build_summary
from shop.reports.invoicing import generate_invoice_batch

class TestReports(unittest.TestCase):

    def setUp(self):
        self.orders = []
        for i in range(5):
            o = Order(f"ORD-{i}", f"CUST-{i}", "USD")
            o.add_item(OrderItem("SKU-1", "Product", 20.0, 2))
            self.orders.append(o)

    def test_daily_sales_report(self):
        rep = daily_sales_report(self.orders, "2026-10-17")
        self.assertEqual(rep["order_count"], 5)
        self.assertEqual(rep["day"], "2026-10-17")
        self.assertGreater(rep["gross_revenue"], 200.0)

    def test_channel_revenue(self):
        rev = channel_revenue(self.orders, "web")
        self.assertGreater(rev, 200.0)

    def test_generate_invoice_batch(self):
        invoices = generate_invoice_batch(self.orders)
        self.assertEqual(len(invoices), 5)
        self.assertTrue(invoices[0]["formatted_total"].startswith("$"))

    def test_build_summary_basic(self):
        summary = build_summary(self.orders, period="daily", channel="web")
        self.assertEqual(summary["total_orders"], 5)
        self.assertEqual(summary["channel"], "web")
        self.assertEqual(summary["period"], "daily")
        self.assertGreater(summary["gross_revenue"], 0.0)

    def test_build_summary_periods(self):
        for p in ["daily", "weekly", "monthly", "annual"]:
            res = build_summary(self.orders, period=p)
            self.assertEqual(res["period"], p)
            self.assertGreater(res["projected_annual"], 0.0)

    def test_build_summary_status_filter(self):
        cancelled_order = Order("CANCELLED", "CUST-X", "USD")
        cancelled_order.status = "cancelled"
        cancelled_order.add_item(OrderItem("SKU-99", "Bad", 100.0, 1))

        orders_with_cancelled = self.orders + [cancelled_order]
        res = build_summary(orders_with_cancelled)
        self.assertEqual(res["total_orders"], 5)

    def test_build_summary_discounts(self):
        o = Order("DISC", "CUST-D", "USD")
        o.discount_code = "VIP20"
        o.add_item(OrderItem("SKU-1", "Prod", 100.0, 1))

        summary = build_summary([o], include_discounts=True)
        self.assertGreater(summary["discounts_given"], 0.0)

if __name__ == "__main__":
    unittest.main()
