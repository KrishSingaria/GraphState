/**
 * Formatting helpers for web UI.
 */

export function totalLabel(amount: number, currency: string = "USD"): string {
  const formatted = amount.toFixed(2);
  switch (currency) {
    case "EUR":
      return `€${formatted}`;
    case "GBP":
      return `£${formatted}`;
    case "USD":
    default:
      return `$${formatted}`;
  }
}
