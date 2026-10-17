"""Text normalization and slugification utilities."""

import re

def slugify(text: str) -> str:
    """Converts a string to a URL-friendly slug."""
    text = text.strip().lower()
    text = re.sub(r"[^\w\s-]", "", text)
    text = re.sub(r"[\s_-]+", "-", text)
    return text.strip("-")

def truncate(text: str, max_len: int = 50) -> str:
    """Truncates text with ellipsis if exceeding max length."""
    if len(text) <= max_len:
        return text
    return text[: max_len - 3] + "..."

def title_case(text: str) -> str:
    """Capitalizes the first character of each word."""
    return text.title()
