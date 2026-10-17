/**
 * Sales report presenter.
 */

import { totalLabel } from "./format";

export interface SalesBucket {
  period: string;
  total: number;
}

export function formatReportBucket(bucket: SalesBucket): string {
  const label = totalLabel(bucket.total, "EUR");
  return `${bucket.period}: ${label}`;
}
