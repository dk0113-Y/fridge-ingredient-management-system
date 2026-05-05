import { env } from "../config/env"

const BACKEND_URL_KEY = "backend_base_url"
const BACKEND_RECENT_KEY = "backend_recent_urls"
const MAX_RECENT_URLS = 5

export function normalizeBaseUrl(url: string) {
  return url.trim().replace(/\/+$/, "")
}

export function getCurrentBaseUrl() {
  const stored = wx.getStorageSync(BACKEND_URL_KEY)
  if (typeof stored === "string" && stored.trim()) {
    return normalizeBaseUrl(stored)
  }

  return env.defaultBaseUrl
}

export function getRecentBaseUrls() {
  const stored = wx.getStorageSync(BACKEND_RECENT_KEY)
  if (!Array.isArray(stored)) {
    return []
  }

  return stored.filter((item): item is string => typeof item === "string" && item.trim().length > 0)
}

export function saveCurrentBaseUrl(url: string) {
  const normalized = normalizeBaseUrl(url)
  wx.setStorageSync(BACKEND_URL_KEY, normalized)

  const nextRecent = [normalized, ...getRecentBaseUrls().filter((item) => item !== normalized)].slice(
    0,
    MAX_RECENT_URLS,
  )
  wx.setStorageSync(BACKEND_RECENT_KEY, nextRecent)

  return normalized
}

export function buildBaseUrl(ip: string, port: string) {
  const cleanIp = ip.trim().replace(/^https?:\/\//, "").replace(/\/+$/, "")
  const cleanPort = port.trim() || "8080"
  return normalizeBaseUrl(`http://${cleanIp}:${cleanPort}`)
}

export function parseBaseUrl(url: string) {
  const normalized = normalizeBaseUrl(url)
  const matched = normalized.match(/^https?:\/\/([^/:]+)(?::(\d+))?$/)

  return {
    ip: matched?.[1] || "",
    port: matched?.[2] || "8080",
  }
}
