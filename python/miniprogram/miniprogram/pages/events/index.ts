import { EventItem, fetchEvents } from "../../api/events"
import { getEventTypeMeta } from "../../utils/event-display"
import {
  formatUpdatedAt,
  getCategoryLabel,
  getNameLabel,
} from "../../utils/inventory-display"

interface EventViewItem {
  id: number
  timeText: string
  typeText: string
  objectText: string
}

interface EventsPageData {
  loading: boolean
  errorText: string
  items: EventViewItem[]
  hasItems: boolean
  showEmptyState: boolean
}

function getEventObjectText(item: EventItem) {
  const nameText = getNameLabel(item.fine_name || item.coarse_class)
  const categoryText = getCategoryLabel(item.coarse_class)
  const quantityText = item.quantity_delta === 0 ? "" : ` / ${item.quantity_delta > 0 ? "+" : ""}${item.quantity_delta}`

  return `${nameText}（${categoryText}${quantityText}）`
}

function mapEventItem(item: EventItem): EventViewItem {
  const eventMeta = getEventTypeMeta(item.event_type)

  return {
    id: item.id,
    timeText: formatUpdatedAt(item.timestamp),
    typeText: eventMeta.text,
    objectText: getEventObjectText(item),
  }
}

Page<EventsPageData, WechatMiniprogram.IAnyObject>({
  data: {
    loading: true,
    errorText: "",
    items: [],
    hasItems: false,
    showEmptyState: false,
  },

  onShow() {
    void this.loadEvents()
  },

  async loadEvents() {
    this.setData({
      loading: true,
      errorText: "",
    })

    try {
      const response = await fetchEvents()
      const items = response.events
        .slice()
        .sort(
          (left, right) =>
            new Date(right.timestamp).getTime() - new Date(left.timestamp).getTime(),
        )
        .map(mapEventItem)

      this.setData({
        loading: false,
        items,
        hasItems: items.length > 0,
        showEmptyState: items.length === 0,
      })
    } catch (error) {
      this.setData({
        loading: false,
        errorText: error instanceof Error ? error.message : "加载事件列表失败。",
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
})
