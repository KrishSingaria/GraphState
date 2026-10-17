"""Product category tree management."""

from utils.text import slugify

class Category:
    """Product category representation."""

    def __init__(self, category_id: str, name: str, parent_id: str = ""):
        self.category_id = category_id
        self.name = name
        self.slug = slugify(name)
        self.parent_id = parent_id

class CategoryTree:
    """Manages hierarchical category structure."""

    def __init__(self):
        self.categories = {}

    def add_category(self, cat: Category):
        self.categories[cat.category_id] = cat

    def get_category(self, category_id: str) -> Category:
        return self.categories.get(category_id)

    def get_slug_path(self, category_id: str) -> str:
        parts = []
        cur_id = category_id
        while cur_id and cur_id in self.categories:
            cat = self.categories[cur_id]
            parts.append(cat.slug)
            cur_id = cat.parent_id
        return "/".join(reversed(parts))
