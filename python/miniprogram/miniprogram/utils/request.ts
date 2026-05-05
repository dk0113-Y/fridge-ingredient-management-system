import { env } from "../config/env"
import { getCurrentBaseUrl, normalizeBaseUrl } from "./backend"

type RequestMethod = "GET" | "POST"

interface RequestOptions<TData> {
  url: string
  method?: RequestMethod
  data?: TData
  header?: Record<string, string>
  baseUrl?: string
}

function formatResponseError(statusCode: number, responseData: unknown) {
  if (
    responseData &&
    typeof responseData === "object" &&
    "error" in responseData &&
    typeof responseData.error === "string"
  ) {
    return `请求失败：HTTP ${statusCode} - ${responseData.error}`
  }

  if (typeof responseData === "string") {
    return `请求失败：HTTP ${statusCode} - ${responseData}`
  }

  return `请求失败：HTTP ${statusCode}`
}

export function request<TResponse, TData = Record<string, unknown>>(
  options: RequestOptions<TData>,
): Promise<TResponse> {
  const { url, method = "GET", data, header = {}, baseUrl } = options

  return new Promise((resolve, reject) => {
    const resolvedBaseUrl = normalizeBaseUrl(baseUrl || getCurrentBaseUrl())
    const resolvedUrl = `${resolvedBaseUrl}${url.startsWith("/") ? url : `/${url}`}`

    wx.request({
      url: resolvedUrl,
      method,
      data: data as WechatMiniprogram.IAnyObject | string | ArrayBuffer | undefined,
      timeout: env.timeout,
      header: {
        "Content-Type": "application/json",
        ...header,
      },
      success: (response) => {
        const { statusCode, data: responseData } = response
        if (statusCode >= 200 && statusCode < 300) {
          resolve(responseData as TResponse)
          return
        }

        reject(new Error(formatResponseError(statusCode, responseData)))
      },
      fail: () => {
        reject(
          new Error(
            `无法连接到本地服务：${resolvedBaseUrl}。请确认 C++ local HTTP server 已启动，并且开发者工具已勾选“不校验合法域名”。`,
          ),
        )
      },
    })
  })
}
