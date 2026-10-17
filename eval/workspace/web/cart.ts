/**
 * Cart view presentation model.
 */

import { totalLabel } from "./format";

export interface CartLineItem {
  sku: string;
  price: number;
  quantity: number;
}

export class CartView {
  private items: CartLineItem[] = [];

  addItem(sku: string, price: number, quantity: number = 1): void {
    this.items.push({ sku, price, quantity });
  }

  get subtotal(): number {
    return this.items.reduce((sum, item) => sum + item.price * item.quantity, 0);
  }

  renderHeader(): string {
    const label = totalLabel(this.subtotal);
    return `Cart Total: ${label} (${this.items.length} items)`;
  }
}
