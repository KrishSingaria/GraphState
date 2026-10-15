import { fetchCart, totalLabel } from "./api";

export class CartView {
  constructor(private readonly id: string) {}

  async render(): Promise<string> {
    const cart = await fetchCart(this.id);
    return totalLabel(cart.total, "USD");
  }
}
