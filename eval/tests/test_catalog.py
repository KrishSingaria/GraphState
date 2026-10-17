"""Tests for product catalog, categories, and inventory."""

import unittest
from shop.catalog.categories import Category, CategoryTree
from shop.catalog.products import Product, ProductCatalog
from shop.catalog.inventory import InventoryItem, InventoryManager

class TestCatalog(unittest.TestCase):

    def test_category_slug(self):
        cat = Category("cat-1", "Home & Kitchen")
        self.assertEqual(cat.slug, "home-kitchen")

    def test_category_tree_path(self):
        tree = CategoryTree()
        c1 = Category("c1", "Electronics")
        c2 = Category("c2", "Audio", parent_id="c1")
        c3 = Category("c3", "Headphones", parent_id="c2")
        tree.add_category(c1)
        tree.add_category(c2)
        tree.add_category(c3)

        self.assertEqual(tree.get_slug_path("c3"), "electronics/audio/headphones")

    def test_product_creation(self):
        p = Product("SKU-100", "Wireless Mouse", 29.99)
        self.assertEqual(p.price, 29.99)
        self.assertEqual(p.slug, "wireless-mouse")

    def test_product_discounted_price(self):
        p = Product("SKU-101", "Mechanical Keyboard", 100.0)
        self.assertEqual(p.discounted_price(25.0), 75.0)

    def test_product_catalog(self):
        catalog = ProductCatalog()
        p1 = Product("SKU-1", "Item A", 10.0)
        p2 = Product("SKU-2", "Item B", 20.0)
        catalog.add_product(p1)
        catalog.add_product(p2)

        self.assertEqual(catalog.get_product("SKU-1").name, "Item A")
        self.assertIsNone(catalog.get_product("SKU-999"))
        self.assertEqual(len(catalog.list_all()), 2)

    def test_inventory_item_available(self):
        item = InventoryItem("SKU-1", 10, 3)
        self.assertEqual(item.available, 7)

    def test_inventory_manager_reserve_release(self):
        mgr = InventoryManager()
        mgr.set_stock("SKU-1", 10)
        self.assertTrue(mgr.reserve_stock("SKU-1", 4))
        self.assertFalse(mgr.check_stock("SKU-1", 7))
        self.assertTrue(mgr.check_stock("SKU-1", 6))

        mgr.release_stock("SKU-1", 2)
        self.assertTrue(mgr.check_stock("SKU-1", 8))

    def test_inventory_manager_deduct(self):
        mgr = InventoryManager()
        mgr.set_stock("SKU-1", 10)
        mgr.reserve_stock("SKU-1", 5)
        mgr.deduct_stock("SKU-1", 5)
        self.assertEqual(mgr.check_stock("SKU-1", 5), True)
        self.assertEqual(mgr.check_stock("SKU-1", 6), False)

if __name__ == "__main__":
    unittest.main()
