import { EventItem, fetchEvents } from "../../api/events"
import { getEventTypeMeta } from "../../utils/event-display"
import { formatUpdatedAt } from "../../utils/inventory-display"

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
  if (item.event_type.startsWith("manual_")) {
    const prefix = `${item.event_type}_`

    if (item.session_id.startsWith(prefix)) {
      const rawName = item.session_id.slice(prefix.length)
      const lastUnderlineIndex = rawName.lastIndexOf("_")

      if (lastUnderlineIndex > 0) {
        return rawName.slice(0, lastUnderlineIndex)
      }

      return rawName || "手动操作"
    }

    return "手动操作"
  }

  return item.session_id
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

Page<EventsPageData>({
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
