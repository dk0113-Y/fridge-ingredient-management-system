import { request } from "../utils/request"

export interface CppEventItem {
  session_id: string
  event_type: string
  coarse_class?: string
  fine_name?: string
  quantity_delta?: number
  yolo_confidence?: number
  llm_confidence?: number
  review_required?: boolean
  timestamp?: string
}

export interface CppEventsResponse {
  events?: CppEventItem[]
}

export interface EventItem {
  id: number
  session_id: string
  timestamp: string
  event_type: string
  coarse_class: string
  fine_name: string
  quantity_delta: number
  yolo_confidence: number
  llm_confidence: number
  review_required: boolean
}

export interface EventsResponse {
  events: EventItem[]
}

function stableEventId(sessionId: string, timestamp: string, index: number) {
  const key = `${sessionId}:${timestamp}:${index}`
  let hash = 0
  for (let charIndex = 0; charIndex < key.length; charIndex += 1) {
    hash = (hash * 31 + key.charCodeAt(charIndex)) % 2147483647
  }
  return hash || index + 1
}

function normalizeEventItem(item: CppEventItem, index: number): EventItem {
  const timestamp = item.timestamp || ""
  const sessionId = item.session_id || ""

  return {
    id: stableEventId(sessionId, timestamp, index),
    session_id: sessionId,
    timestamp,
    event_type: item.event_type || "unknown",
    coarse_class: item.coarse_class || "unknown",
    fine_name: item.fine_name || "",
    quantity_delta: typeof item.quantity_delta === "number" ? item.quantity_delta : 0,
    yolo_confidence: typeof item.yolo_confidence === "number" ? item.yolo_confidence : 0,
    llm_confidence: typeof item.llm_confidence === "number" ? item.llm_confidence : 0,
    review_required: item.review_required === true,
  }
}

export async function fetchEvents() {
  const raw = await request<CppEventsResponse>({
    url: "/events",
  })

  return {
    events: (raw.events || []).map(normalizeEventItem),
  }
}
