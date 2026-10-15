export interface CartDto { userId: string; total: number }

export function totalLabel(amount: number, currency?: string): string {
  return `${currency ?? "USD"} ${amount.toFixed(2)}`;
}

export async function fetchCart(id: string): Promise<CartDto> {
  const res = await fetch(`/api/carts/${id}`);
  return (await res.json()) as CartDto;
}
