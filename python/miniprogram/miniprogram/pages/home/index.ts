import { EventItem, fetchEvents } from "../../api/events"
import { fetchInventory } from "../../api/inventory"
import { formatTime } from "../../utils/util"

interface HomePageData {
  loading: boolean
  errorText: string
  greetingText: string
  currentDateText: string
  connectionText: string
  connectionClass: string
  syncText: string
  inventoryCount: number
  pendingCount: number
  recentEventCount: number
}

function getGreetingText(date: Date) {
  const hour = date.getHours()

  if (hour < 11) {
    return "早上好！"
  }

  if (hour < 18) {
    return "下午好！"
  }

  return "晚上好！"
}

function formatChineseDate(date: Date) {
  return `${date.getFullYear()}年${date.getMonth() + 1}月${date.getDate()}日`
}

Page<HomePageData, WechatMiniprogram.IAnyObject>({
  data: {
    loading: true,
    errorText: "",
    greetingText: getGreetingText(new Date()),
    currentDateText: formatChineseDate(new Date()),
    connectionText: "离线",
    connectionClass: "summary-status",
    syncText: "--",
    inventoryCount: 0,
    pendingCount: 0,
    recentEventCount: 0,
  },

  onShow() {
    void this.loadHomeData()
  },

  async loadHomeData() {
    const now = new Date()
    this.setData({
      loading: true,
      errorText: "",
      greetingText: getGreetingText(now),
      currentDateText: formatChineseDate(now),
    })

    try {
      const inventoryResponse = await fetchInventory()
      let eventsResponse = { events: [] as EventItem[] }

      try {
        eventsResponse = await fetchEvents()
      } catch (error) {
        console.warn("Failed to load events on home page", error)
      }

      const activeInventory = inventoryResponse.inventory.filter((item) => item.count > 0)

      this.setData({
        loading: false,
        connectionText: "在线",
        connectionClass: "summary-status summary-status-online",
        syncText: formatTime(new Date()),
        inventoryCount: activeInventory.length,
        pendingCount: inventoryResponse.pending_review_count,
        recentEventCount: eventsResponse.events.length,
      })
    } catch (error) {
      this.setData({
        loading: false,
        errorText: error instanceof Error ? error.message : "当前无法连接到本地服务，请检查地址配置。",
        connectionText: "离线",
        connectionClass: "summary-status",
        syncText: "--",
        inventoryCount: 0,
        pendingCount: 0,
        recentEventCount: 0,
      })
    }
  },

  handleOpenSettings() {
    wx.navigateTo({
      url: "/pages/settings/index",
    })
  },

  handleOpenInventory() {
    wx.redirectTo({
      url: "/pages/inventory/index",
    })
  },

  handleOpenPending() {
    wx.redirectTo({
      url: "/pages/pending/index",
    })
  },

  handleOpenEvents() {
    wx.redirectTo({
      url: "/pages/events/index",
    })
  },

  handleGoConnect() {
    wx.navigateTo({
      url: "/pages/connect-backend/index",
    })
  },
})
