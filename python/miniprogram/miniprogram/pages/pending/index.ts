import { applyPartialConfirmation } from "../../api/confirm"
import { fetchInventory, PendingConfirmation } from "../../api/inventory"
import {
  formatUpdatedAt,
  getCategoryLabel,
  getNameLabel,
  getRemainMeta,
} from "../../utils/inventory-display"

interface PendingViewItem {
  id: number
  sessionId: string
  itemName: string
  category: string
  remainLevel: number
  displayName: string
  displayCategory: string
  remainText: string
  noteText: string
  createdText: string
}

interface PendingPageData {
  loading: boolean
  errorText: string
  items: PendingViewItem[]
  hasItems: boolean
  showEmptyState: boolean
  confirmingId: number
}

function mapPendingItem(item: PendingConfirmation): PendingViewItem {
  const remainLevel = item.remain_level ?? 0.5
  const remainMeta = getRemainMeta(remainLevel)

  return {
    id: item.id,
    sessionId: item.session_id,
    itemName: item.item_name,
    category: item.category,
    remainLevel,
    displayName: getNameLabel(item.item_name),
    displayCategory: getCategoryLabel(item.category),
    remainText: remainMeta.label,
    noteText: item.note || "等待人工确认当前识别结果。",
    createdText: formatUpdatedAt(item.created_at),
  }
}

Page<PendingPageData>({
  data: {
    loading: true,
    errorText: "",
    items: [],
    hasItems: false,
    showEmptyState: false,
    confirmingId: 0,
  },

  onShow() {
    void this.loadPendingItems()
  },

  async loadPendingItems() {
    this.setData({
      loading: true,
      errorText: "",
    })

    try {
      const response = await fetchInventory()
      const items = response.pending_confirmations.map(mapPendingItem)

      this.setData({
        loading: false,
        items,
        hasItems: items.length > 0,
        showEmptyState: items.length === 0,
      })
    } catch (error) {
      this.setData({
        loading: false,
        errorText: error instanceof Error ? error.message : "加载待确认列表失败。",
        items: [],
        hasItems: false,
        showEmptyState: false,
      })
    }
  },

  handleGoHome() {
    wx.redirectTo({
      url: "/pages/home/index",
    })
  },

  async handleConfirm(event: WechatMiniprogram.BaseEvent) {
    const { id } = event.currentTarget.dataset as { id: number | string }
    const currentItem = this.data.items.find((item) => item.id === Number(id))

    if (!currentItem) {
      return
    }

    this.setData({
      confirmingId: currentItem.id,
    })

    try {
      await applyPartialConfirmation({
        session_id: currentItem.sessionId,
        item_name: currentItem.itemName,
        category: currentItem.category,
        remain_level: currentItem.remainLevel,
        note: "小程序确认通过。",
      })

      wx.showToast({
        title: "已确认",
        icon: "success",
      })
      void this.loadPendingItems()
    } catch (error) {
      wx.showToast({
        title: error instanceof Error ? error.message : "确认失败",
        icon: "none",
      })
    } finally {
      this.setData({
        confirmingId: 0,
      })
    }
  },

  handleAdjust(event: WechatMiniprogram.BaseEvent) {
    const { id } = event.currentTarget.dataset as { id: number | string }
    const currentItem = this.data.items.find((item) => item.id === Number(id))

    if (!currentItem) {
      return
    }

    wx.setStorageSync("pending_confirm_item", currentItem)
    wx.navigateTo({
      url: `/pages/pending-confirm/index?id=${currentItem.id}`,
    })
  },
})
