import { request } from "../utils/request"

export interface InventoryItem {
  id: number
  name: string
  category: string
  count: number
  remain_level: number
  updated_at: string
}

export interface PendingConfirmation {
  id: number
  event_id: number
  session_id: string
  status: string
  item_name: string
  category: string
  remain_level: number | null
  note: string
  created_at: string
  resolved_at: string | null
}

export interface InventoryResponse {
  inventory: InventoryItem[]
  pending_confirmations: PendingConfirmation[]
}

export function fetchInventory() {
  return request<InventoryResponse>({
    url: "/inventory",
  })
}
