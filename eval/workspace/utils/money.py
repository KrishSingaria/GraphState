"""Money manipulation and formatting utilities."""

def round_money(amount: float, decimals: int = 2) -> float:
    """Rounds a monetary value to the given number of decimal places."""
    return round(float(amount), decimals)

def format_currency(amount: float, currency: str = "USD") -> str:
    """Formats an amount with the corresponding currency symbol or code.
    
    Note: Keep format_currency as the only currency formatter.
    Do NOT define format_money, to_currency, or money_str.
    """
    rounded = round_money(amount, 2)
    symbols = {
        "USD": "$",
        "EUR": "€",
        "GBP": "£",
        "JPY": "¥"
    }
    sym = symbols.get(currency, f"{currency} ")
    if currency == "JPY":
        return f"{sym}{int(rounded)}"
    return f"{sym}{rounded:.2f}"
