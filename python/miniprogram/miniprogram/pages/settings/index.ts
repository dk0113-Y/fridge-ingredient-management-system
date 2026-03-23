import { fetchHealth } from "../../api/system"
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

Page<SettingsPageData>({
  data: {
    baseUrl: "",
    checking: true,
    isOnline: false,
    statusText: "检测中",
    statusTagClass: "tag-neutral",
    statusHintText: "正在读取当前后端状态...",
    summaryText: "正在读取当前后端状态...",
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
      statusHintText: "正在读取当前后端状态...",
      summaryText: "正在读取当前后端状态...",
    })

    try {
      const health = await fetchHealth()
      this.setData({
        checking: false,
        isOnline: true,
        statusText: "在线",
        statusTagClass: "tag-online",
        statusHintText: "当前地址已可访问，适合继续比赛演示。",
        summaryText: `库存 ${health.inventory_items} 项，事件 ${health.events} 条，待确认 ${health.pending_confirmations} 条。`,
      })
    } catch (error) {
      this.setData({
        checking: false,
        isOnline: false,
        statusText: "离线",
        statusTagClass: "tag-offline",
        statusHintText: "当前地址不可达，请检查 IP、端口或 Flask 服务。",
        summaryText: error instanceof Error ? error.message : "无法连接到当前后端。",
      })
    }
  },

  handleOpenConnectPage() {
    wx.navigateTo({
      url: "/pages/connect-backend/index",
    })
  },
})
