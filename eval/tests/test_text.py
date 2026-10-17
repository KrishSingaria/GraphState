"""Tests for text normalization and slugification."""

import unittest
from utils.text import slugify, truncate, title_case

class TestTextUtils(unittest.TestCase):

    def test_slugify_basic(self):
        self.assertEqual(slugify("Hello World"), "hello-world")
        self.assertEqual(slugify("  Spaces  Everywhere  "), "spaces-everywhere")

    def test_slugify_special_chars(self):
        self.assertEqual(slugify("Product #10: Super & Great!"), "product-10-super-great")

    def test_slugify_dashes(self):
        self.assertEqual(slugify("already-slugified-text"), "already-slugified-text")
        self.assertEqual(slugify("multiple---dashes___here"), "multiple-dashes-here")

    def test_truncate_short(self):
        self.assertEqual(truncate("Short string", 20), "Short string")

    def test_truncate_long(self):
        self.assertEqual(truncate("This is a rather long string for display", 15), "This is a ra...")

    def test_title_case(self):
        self.assertEqual(title_case("hello world of python"), "Hello World Of Python")

if __name__ == "__main__":
    unittest.main()
