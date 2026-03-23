import { request } from "../utils/request"

export interface HealthResponse {
  status: string
  inventory_items: number
  events: number
  pending_confirmations: number
}

export function fetchHealth(baseUrl?: string) {
  return request<HealthResponse>({
    url: "/health",
    baseUrl,
  })
}
