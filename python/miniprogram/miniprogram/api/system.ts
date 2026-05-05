import { request } from "../utils/request"

export interface CppHealthResponse {
  status: string
  service?: string
  bind_host?: string
  port?: number
  db_ready?: boolean
  last_event_time?: string
}

export interface HealthResponse {
  status: string
  service: string
  bind_host: string
  port: number
  db_ready: boolean
  last_event_time: string
}

function normalizeHealth(raw: CppHealthResponse): HealthResponse {
  return {
    status: raw.status || "unknown",
    service: raw.service || "fridge_local_service",
    bind_host: raw.bind_host || "",
    port: typeof raw.port === "number" ? raw.port : 0,
    db_ready: raw.db_ready === true,
    last_event_time: raw.last_event_time || "",
  }
}

export async function fetchHealth(baseUrl?: string) {
  const raw = await request<CppHealthResponse>({
    url: "/health",
    baseUrl,
  })
  return normalizeHealth(raw)
}
