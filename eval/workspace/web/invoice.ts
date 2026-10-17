/**
 * Web invoice renderer.
 */

import { totalLabel } from "./format";

export interface WebInvoiceData {
  id: string;
  total: number;
  currency: string;
}

export function renderInvoiceSummary(data: WebInvoiceData): string {
  const formatted = totalLabel(data.total, data.currency);
  return `Invoice #${data.id} - Total Due: ${formatted}`;
}
