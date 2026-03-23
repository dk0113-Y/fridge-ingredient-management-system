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
      data,
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

        reject(
          new Error(
            `请求失败：HTTP ${statusCode}${
              typeof responseData === "string" ? ` - ${responseData}` : ""
            }`,
          ),
        )
      },
      fail: () => {
        reject(
          new Error(
            `无法连接到后端：${resolvedBaseUrl}。请确认 Flask 服务已启动，并且已勾选开发者工具里的“不校验合法域名”选项。`,
          ),
        )
      },
    })
  })
}
