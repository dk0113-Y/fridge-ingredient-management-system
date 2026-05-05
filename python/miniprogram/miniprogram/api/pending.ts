import { request } from "../utils/request"
import { CppRemainLevel, normalizeRemainLevel } from "./inventory"

export interface CppPendingItem {
  session_id: string
  event_type: string
  category: string
  item_name: string
  reason?: string
  timestamp?: string
  remain_level?: CppRemainLevel
}

export interface CppPendingResponse {
  pending?: CppPendingItem[]
}

export interface PendingConfirmation {
  id: number
  session_id: string
  event_type: string
  item_name: string
  category: string
  remain_level: number
  reason: string
  created_at: string
}

export interface PendingResponse {
  pending: PendingConfirmation[]
}

function stablePendingId(sessionId: string, index: number) {
  const key = `${sessionId}:${index}`
  let hash = 0
  for (let charIndex = 0; charIndex < key.length; charIndex += 1) {
    hash = (hash * 31 + key.charCodeAt(charIndex)) % 2147483647
  }
  return hash || index + 1
}

function normalizePendingItem(item: CppPendingItem, index: number): PendingConfirmation {
  return {
    id: stablePendingId(item.session_id || "pending", index),
    session_id: item.session_id || "",
    event_type: item.event_type || "uncertain",
    item_name: item.item_name || "unknown",
    category: item.category || "unknown",
    remain_level: normalizeRemainLevel(item.remain_level ?? 0.5),
    reason: item.reason || "",
    created_at: item.timestamp || "",
  }
}

export async function fetchPending() {
  const raw = await request<CppPendingResponse>({
    url: "/pending",
  })

  return {
    pending: (raw.pending || []).map(normalizePendingItem),
  }
}
