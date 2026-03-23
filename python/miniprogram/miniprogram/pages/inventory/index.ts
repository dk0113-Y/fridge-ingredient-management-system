import { fetchInventory, InventoryItem } from "../../api/inventory"
import {
  formatUpdatedAt,
  getCategoryLabel,
  getNameLabel,
  getRemainMeta,
} from "../../utils/inventory-display"

interface InventoryViewItem extends InventoryItem {
  displayName: string
  displayCategory: string
  remainText: string
  remainPercentText: string
  remainTagClass: string
  updatedText: string
}

interface InventoryPageData {
  loading: boolean
  errorText: string
  items: InventoryViewItem[]
  hasItems: boolean
  showEmptyState: boolean
}

function mapInventoryItem(item: InventoryItem): InventoryViewItem {
  const remainMeta = getRemainMeta(item.remain_level)

  return {
    ...item,
    displayName: getNameLabel(item.name),
    displayCategory: getCategoryLabel(item.category),
    remainText: remainMeta.label,
    remainPercentText: remainMeta.percentText,
    remainTagClass: remainMeta.className,
    updatedText: formatUpdatedAt(item.updated_at),
  }
}

Page<InventoryPageData>({
  data: {
    loading: true,
    errorText: "",
    items: [],
    hasItems: false,
    showEmptyState: false,
  },

  onShow() {
    void this.loadInventory()
  },

  async loadInventory() {
    this.setData({
      loading: true,
      errorText: "",
    })

    try {
      const response = await fetchInventory()
      const items = response.inventory
        .filter((item) => item.count > 0)
        .sort((left, right) => right.id - left.id)
        .map(mapInventoryItem)

      this.setData({
        loading: false,
        items,
        hasItems: items.length > 0,
        showEmptyState: items.length === 0,
      })
    } catch (error) {
      this.setData({
        loading: false,
        errorText: error instanceof Error ? error.message : "加载库存失败。",
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

  handleEdit(event: WechatMiniprogram.BaseEvent) {
    const { id } = event.currentTarget.dataset as { id: number | string }
    const currentItem = this.data.items.find((item) => item.id === Number(id))

    if (!currentItem) {
      wx.showToast({
        title: "未找到该库存项",
        icon: "none",
      })
      return
    }

    wx.setStorageSync("inventory_form_item", currentItem)
    wx.navigateTo({
      url: `/pages/inventory-form/index?mode=edit&id=${currentItem.id}`,
    })
  },
})
