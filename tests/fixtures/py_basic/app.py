"""Module docstring for test app."""
import os
from typing import Optional, List

class BaseService:
    def __init__(self, name: str):
        self.name = name

    def ping(self) -> bool:
        return True

class Calculator(BaseService):
    def __init__(self, name: str, precision: int = 2):
        super().__init__(name)
        self.precision = precision

    def add(self, a: float, b: float) -> float:
        """Add two numbers."""
        return a + b

    @staticmethod
    def identity(x: int) -> int:
        return x

def calculate_total(cart: List[float], tax_rate: float = 0.05, *, currency: str = "USD") -> float:
    """Calculate total cart value with tax."""
    subtotal = sum(cart)
    if tax_rate < 0:
        raise ValueError("Invalid tax")
    total = subtotal * (1.0 + tax_rate)
    return total

def run_app():
    calc = Calculator("MainCalc")
    calc.ping()
    res = calc.add(10.5, 2.5)
    total = calculate_total([10.0, 20.0], 0.1, currency="EUR")
    return total
