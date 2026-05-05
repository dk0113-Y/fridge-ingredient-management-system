import { applyPartialConfirmation } from "../../api/confirm"
import {
  getCategoryLabel,
  getRemainMeta,
} from "../../utils/inventory-display"

const REMAIN_LEVEL_OPTIONS = [
  { label: "充足", value: 1 },
  { label: "正常", value: 0.75 },
  { label: "正常偏低", value: 0.5 },
  { label: "偏低", value: 0.25 },
  { label: "不足", value: 0 },
]

interface PendingConfirmPageData {
  sessionId: string
  name: string
  category: string
  remainLevelIndex: number
  remainLevelLabels: string[]
  remainPreviewText: string
  saving: boolean
}

function getRemainIndex(remainLevel: number) {
  const target = REMAIN_LEVEL_OPTIONS.findIndex((item) => item.value === remainLevel)
  return target >= 0 ? target : 2
}

Page<PendingConfirmPageData, WechatMiniprogram.IAnyObject>({
  data: {
    sessionId: "",
    name: "",
    category: "",
    remainLevelIndex: 2,
    remainLevelLabels: REMAIN_LEVEL_OPTIONS.map((item) => item.label),
    remainPreviewText: "正常",
    saving: false,
  },

  onLoad() {
    const currentItem = wx.getStorageSync("pending_confirm_item") as {
      sessionId: string
      itemName: string
      category: string
      remainLevel: number
    } | null

    if (!currentItem) {
      wx.showToast({
        title: "未找到待确认项",
        icon: "none",
      })
      wx.navigateBack()
      return
    }

    const remainLevelIndex = getRemainIndex(currentItem.remainLevel)
    this.setData({
      sessionId: currentItem.sessionId,
      name: currentItem.itemName,
      category: currentItem.category === "unknown" ? "" : currentItem.category,
      remainLevelIndex,
      remainPreviewText: getRemainMeta(REMAIN_LEVEL_OPTIONS[remainLevelIndex].value).label,
    })
  },

  handleNameInput(event: WechatMiniprogram.CustomEvent<{ value: string }>) {
    this.setData({
      name: event.detail.value,
    })
  },

  handleCategoryInput(event: WechatMiniprogram.CustomEvent<{ value: string }>) {
    this.setData({
      category: event.detail.value,
    })
  },

  handleRemainChange(event: WechatMiniprogram.CustomEvent<{ value: string }>) {
    const remainLevelIndex = Number(event.detail.value)
    this.setData({
      remainLevelIndex,
      remainPreviewText: getRemainMeta(REMAIN_LEVEL_OPTIONS[remainLevelIndex].value).label,
    })
  },

  async handleConfirm() {
    const name = this.data.name.trim()
    const category = this.data.category.trim() || "unknown"
    const remainLevel = REMAIN_LEVEL_OPTIONS[this.data.remainLevelIndex].value

    if (!name) {
      wx.showToast({
        title: "请输入名称",
        icon: "none",
      })
      return
    }

    this.setData({
      saving: true,
    })

    try {
      await applyPartialConfirmation({
        session_id: this.data.sessionId,
        item_name: name,
        category,
        count_delta: -1,
        remain_level: remainLevel,
        note: `修改后确认：${getCategoryLabel(category)} / ${this.data.remainPreviewText}`,
      })

      wx.showToast({
        title: "确认成功",
        icon: "success",
      })

      setTimeout(() => {
        wx.navigateBack()
      }, 250)
    } catch (error) {
      wx.showToast({
        title: error instanceof Error ? error.message : "确认失败",
        icon: "none",
      })
    } finally {
      this.setData({
        saving: false,
      })
    }
  },
})
