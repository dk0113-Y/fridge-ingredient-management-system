import { HealthResponse, fetchHealth } from "../../api/system"
import { env } from "../../config/env"
import { getCurrentBaseUrl } from "../../utils/backend"

interface SettingsPageData {
  baseUrl: string
  checking: boolean
  isOnline: boolean
  statusText: string
  statusTagClass: string
  statusHintText: string
  summaryText: string
  versionText: string
}

function buildSummaryText(health: HealthResponse) {
  const dbText = health.db_ready ? "数据库可用" : "数据库未就绪"
  const bindText = health.bind_host ? `${health.bind_host}:${health.port || 8080}` : `:${health.port || 8080}`
  const eventText = health.last_event_time ? `最近事件 ${health.last_event_time}` : "暂无事件时间"
  return `${health.service}，${dbText}，监听 ${bindText}，${eventText}。`
}

Page<SettingsPageData, WechatMiniprogram.IAnyObject>({
  data: {
    baseUrl: "",
    checking: true,
    isOnline: false,
    statusText: "检测中",
    statusTagClass: "tag-neutral",
    statusHintText: "正在读取当前本地服务状态...",
    summaryText: "正在读取当前本地服务状态...",
    versionText: env.appVersion,
  },

  onShow() {
    void this.loadConnectionState()
  },

  async loadConnectionState() {
    const baseUrl = getCurrentBaseUrl()
    this.setData({
      baseUrl,
      checking: true,
      statusText: "检测中",
      statusTagClass: "tag-neutral",
      statusHintText: "正在读取当前本地服务状态...",
      summaryText: "正在读取当前本地服务状态...",
    })

    try {
      const health = await fetchHealth()
      this.setData({
        checking: false,
        isOnline: true,
        statusText: "在线",
        statusTagClass: "tag-online",
        statusHintText: "当前 C++ 本地服务地址可访问，适合继续本地联调。",
        summaryText: buildSummaryText(health),
      })
    } catch (error) {
      this.setData({
        checking: false,
        isOnline: false,
        statusText: "离线",
        statusTagClass: "tag-offline",
        statusHintText: "当前地址不可达，请检查 IP、端口或 C++ local HTTP server。",
        summaryText: error instanceof Error ? error.message : "无法连接到当前本地服务。",
      })
    }
  },

  handleOpenConnectPage() {
    wx.navigateTo({
      url: "/pages/connect-backend/index",
    })
  },
})
