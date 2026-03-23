import { fetchHealth } from "../../api/system"
import {
  buildBaseUrl,
  getCurrentBaseUrl,
  getRecentBaseUrls,
  parseBaseUrl,
  saveCurrentBaseUrl,
} from "../../utils/backend"

interface ConnectBackendPageData {
  currentBaseUrl: string
  ip: string
  port: string
  previewBaseUrl: string
  previewText: string
  previewFieldClass: string
  recentBaseUrls: string[]
  showEmptyHistory: boolean
  testing: boolean
  hasTestResult: boolean
  testResultText: string
  testResultClass: string
  heroStateText: string
  heroHintText: string
  heroCenterText: string
  heroAddressText: string
}

Page<ConnectBackendPageData>({
  data: {
    currentBaseUrl: "",
    ip: "",
    port: "5000",
    previewBaseUrl: "",
    previewText: "请先输入 IP 与端口",
    previewFieldClass: "field-picker field-placeholder",
    recentBaseUrls: [],
    showEmptyHistory: true,
    testing: false,
    hasTestResult: false,
    testResultText: "",
    testResultClass: "test-result test-error",
    heroStateText: "当前地址",
    heroHintText: "推荐先测试连接，再保存并使用。",
    heroCenterText: "待连接",
    heroAddressText: "请先输入 IP 与端口",
  },

  onShow() {
    this.syncFromCurrentBaseUrl()
  },

  syncFromCurrentBaseUrl() {
    const currentBaseUrl = getCurrentBaseUrl()
    const parsed = parseBaseUrl(currentBaseUrl)
    const recentBaseUrls = getRecentBaseUrls()

    this.setData({
      currentBaseUrl,
      ip: parsed.ip,
      port: parsed.port,
      previewBaseUrl: currentBaseUrl,
      previewText: currentBaseUrl,
      previewFieldClass: "field-picker",
      recentBaseUrls,
      showEmptyHistory: recentBaseUrls.length === 0,
      hasTestResult: false,
      testResultText: "",
      testResultClass: "test-result test-error",
      heroStateText: "当前地址",
      heroHintText: "推荐先测试连接，再保存并使用。",
      heroCenterText: "后端",
      heroAddressText: currentBaseUrl,
    })
  },

  updatePreview(ip: string, port: string) {
    if (!ip.trim()) {
      this.setData({
        previewBaseUrl: "",
        previewText: "请先输入 IP 与端口",
        previewFieldClass: "field-picker field-placeholder",
        heroStateText: "等待输入",
        heroHintText: "请输入电脑本地或局域网中的后端地址。",
        heroCenterText: "未配置",
        heroAddressText: "请先输入 IP 与端口",
      })
      return
    }

    const previewBaseUrl = buildBaseUrl(ip, port)
    this.setData({
      previewBaseUrl,
      previewText: previewBaseUrl,
      previewFieldClass: "field-picker",
      heroStateText: "待测试",
      heroHintText: "当前为待测试地址，点击“测试连接”验证可用性。",
      heroCenterText: "待测试",
      heroAddressText: previewBaseUrl,
    })
  },

  handleIpInput(event: WechatMiniprogram.CustomEvent<{ value: string }>) {
    const ip = event.detail.value
    this.setData({
      ip,
    })
    this.updatePreview(ip, this.data.port)
  },

  handlePortInput(event: WechatMiniprogram.CustomEvent<{ value: string }>) {
    const port = event.detail.value
    this.setData({
      port,
    })
    this.updatePreview(this.data.ip, port)
  },

  handleUseRecent(event: WechatMiniprogram.BaseEvent) {
    const { url } = event.currentTarget.dataset as { url: string }
    const parsed = parseBaseUrl(url)

    this.setData({
      ip: parsed.ip,
      port: parsed.port,
      previewBaseUrl: url,
      previewText: url,
      previewFieldClass: "field-picker",
      hasTestResult: false,
      testResultText: "",
      testResultClass: "test-result test-error",
      heroStateText: "待测试",
      heroHintText: "已载入历史地址，请测试后再决定是否保存。",
      heroCenterText: "待测试",
      heroAddressText: url,
    })
  },

  async handleTestConnection() {
    if (!this.data.previewBaseUrl) {
      wx.showToast({
        title: "请先输入 IP 地址",
        icon: "none",
      })
      return
    }

    this.setData({
      testing: true,
      hasTestResult: false,
      testResultText: "",
      heroStateText: "测试中",
      heroHintText: "正在向后端发送健康检查请求。",
      heroCenterText: "检测中",
      heroAddressText: this.data.previewBaseUrl,
    })

    try {
      const health = await fetchHealth(this.data.previewBaseUrl)
      this.setData({
        testing: false,
        hasTestResult: true,
        testResultClass: "test-result test-success",
        testResultText: `连接成功：库存 ${health.inventory_items} 项，事件 ${health.events} 条，待确认 ${health.pending_confirmations} 条。`,
        heroStateText: "连接成功",
        heroHintText: "该地址可用，点击“保存并使用”即可切换当前后端。",
        heroCenterText: "已连接",
        heroAddressText: this.data.previewBaseUrl,
      })
    } catch (error) {
      this.setData({
        testing: false,
        hasTestResult: true,
        testResultClass: "test-result test-error",
        testResultText: error instanceof Error ? error.message : "连接失败。",
        heroStateText: "连接失败",
        heroHintText: "请检查 IP、端口或 Flask 服务是否正常运行。",
        heroCenterText: "不可达",
        heroAddressText: this.data.previewBaseUrl,
      })
    }
  },

  handleSave() {
    if (!this.data.previewBaseUrl) {
      wx.showToast({
        title: "请先输入 IP 地址",
        icon: "none",
      })
      return
    }

    const savedUrl = saveCurrentBaseUrl(this.data.previewBaseUrl)
    const recentBaseUrls = getRecentBaseUrls()

    wx.showToast({
      title: "已保存",
      icon: "success",
    })

    this.setData({
      currentBaseUrl: savedUrl,
      recentBaseUrls,
      showEmptyHistory: recentBaseUrls.length === 0,
      heroStateText: "当前地址",
      heroHintText: "新的 baseURL 已保存，后续请求会自动使用该地址。",
      heroCenterText: "已保存",
      heroAddressText: savedUrl,
    })

    setTimeout(() => {
      wx.navigateBack()
    }, 250)
  },
})
