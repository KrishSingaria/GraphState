/**
 * Web checkout view controller.
 */

import { totalLabel } from "./format";

export interface CheckoutSummary {
  orderId: string;
  amount: number;
  currency: string;
}

export function formatCheckoutButton(summary: CheckoutSummary): string {
  const label = totalLabel(summary.amount, "USD");
  return `Pay Now: ${label}`;
}
