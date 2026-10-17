/**
 * API client stubs for frontend store.
 */

export interface ApiResponse<T> {
  success: boolean;
  data: T;
}

export async function fetchCatalog(): Promise<ApiResponse<string[]>> {
  return {
    success: true,
    data: ["SKU-1", "SKU-2", "SKU-3"]
  };
}

export async function submitOrder(orderId: string): Promise<ApiResponse<{ confirmed: boolean }>> {
  return {
    success: true,
    data: { confirmed: true }
  };
}
