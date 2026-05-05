import { request } from "../utils/request"

export type CppRemainLevel = "full" | "half" | "low" | "empty" | number

export interface CppInventoryItem {
  item_id: number
  item_name: string
  category: string
  count: number
  remain_level: CppRemainLevel
  expire_date?: string
  last_update_time?: string
}

export interface CppInventoryResponse {
  items?: CppInventoryItem[]
  pending_review_count?: number
}

export interface InventoryItem {
  id: number
  name: string
  category: string
  count: number
  remain_level: number
  expire_date: string
  updated_at: string
}

export interface InventoryResponse {
  inventory: InventoryItem[]
  pending_review_count: number
}

export function normalizeRemainLevel(remainLevel: CppRemainLevel | null | undefined) {
  if (typeof remainLevel === "number") {
    return Math.max(0, Math.min(1, remainLevel))
  }

  if (remainLevel === "full") {
    return 1
  }
  if (remainLevel === "half") {
    return 0.5
  }
  if (remainLevel === "low") {
    return 0.25
  }
  if (remainLevel === "empty") {
    return 0
  }

  return 0
}

function normalizeInventoryItem(item: CppInventoryItem): InventoryItem {
  return {
    id: item.item_id,
    name: item.item_name || "unknown",
    category: item.category || "unknown",
    count: typeof item.count === "number" ? item.count : 0,
    remain_level: normalizeRemainLevel(item.remain_level),
    expire_date: item.expire_date || "",
    updated_at: item.last_update_time || "",
  }
}

export async function fetchInventory() {
  const raw = await request<CppInventoryResponse>({
    url: "/inventory",
  })

  return {
    inventory: (raw.items || []).map(normalizeInventoryItem),
    pending_review_count:
      typeof raw.pending_review_count === "number" ? raw.pending_review_count : 0,
  }
}
