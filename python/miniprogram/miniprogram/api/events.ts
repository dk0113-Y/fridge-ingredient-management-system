import { request } from "../utils/request"

export interface EventItem {
  id: number
  session_id: string
  timestamp: string
  event_type: string
  roi_id: string
  confidence: number
  before_frame: string
  after_frame: string
  need_user_confirm: boolean
  source_file: string
  created_at: string
}

export interface EventsResponse {
  events: EventItem[]
}

export function fetchEvents() {
  return request<EventsResponse>({
    url: "/events",
  })
}
