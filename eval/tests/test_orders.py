"""Tests for orders, checkout flow, shipping, and order history."""

import unittest
from shop.orders.order import Order, OrderItem
from shop.orders.checkout import calculate_shipping, checkout
from shop.orders.history import OrderHistory

class TestOrders(unittest.TestCase):

    def test_order_item_subtotal(self):
        item = OrderItem("SKU-1", "USB Cable", 9.99, 3)
        self.assertEqual(item.subtotal(), 29.97)

    def test_order_creation_and_slug(self):
        order = Order("ORD-123", "CUST-456", "USD")
        self.assertIn("cust-456-ord-123", order.tracking_slug)
        self.assertEqual(order.status, "pending")

    def test_calculate_shipping_standard(self):
        order = Order("ORD-1", "CUST-1", "USD")
        order.add_item(OrderItem("SKU-1", "Small Item", 20.0, 1))
        # Total < 100 -> standard shipping = 5.0
        self.assertEqual(calculate_shipping(order, "standard"), 5.0)

    def test_calculate_shipping_express(self):
        order = Order("ORD-2", "CUST-1", "USD")
        order.add_item(OrderItem("SKU-1", "Small Item", 20.0, 1))
        self.assertEqual(calculate_shipping(order, "express"), 15.0)

    def test_calculate_shipping_free(self):
        order = Order("ORD-3", "CUST-1", "USD")
        order.add_item(OrderItem("SKU-X", "Big Item", 150.0, 1))
        # Total > 100 -> free shipping = 0.0
        self.assertEqual(calculate_shipping(order), 0.0)

    def test_checkout_confirmed(self):
        order = Order("ORD-4", "CUST-2", "USD")
        order.add_item(OrderItem("SKU-1", "Gadget", 50.0, 1))
        res = checkout(order, discount_code="")
        self.assertEqual(order.status, "confirmed")
        self.assertEqual(res["order_id"], "ORD-4")
        self.assertGreater(res["total"], 50.0)

    def test_checkout_with_discount(self):
        order = Order("ORD-5", "CUST-3", "USD")
        order.add_item(OrderItem("SKU-1", "Gadget", 100.0, 1))
        res_normal = checkout(order, discount_code="")

        order_disc = Order("ORD-6", "CUST-3", "USD")
        order_disc.add_item(OrderItem("SKU-1", "Gadget", 100.0, 1))
        res_disc = checkout(order_disc, discount_code="VIP20")

        self.assertLess(res_disc["total"], res_normal["total"])

    def test_order_history_tracking(self):
        hist = OrderHistory()
        o1 = Order("O1", "CUST-1", "USD")
        o1.add_item(OrderItem("SKU-1", "Item", 50.0, 1))
        o2 = Order("O2", "CUST-1", "USD")
        o2.add_item(OrderItem("SKU-2", "Item", 50.0, 1))
        hist.record_order(o1)
        hist.record_order(o2)

        self.assertGreater(hist.total_spent("CUST-1"), 100.0)
        self.assertGreater(hist.reorder("O1"), 0.0)
        self.assertEqual(hist.reorder("NONEXISTENT"), 0.0)

if __name__ == "__main__":
    unittest.main()
