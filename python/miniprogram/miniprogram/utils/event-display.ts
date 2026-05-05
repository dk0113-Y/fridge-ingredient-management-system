export function getEventTypeMeta(eventType: string) {
  if (eventType === "put_in") {
    return { text: "放入", tagClass: "tag-good" }
  }

  if (eventType === "take_out") {
    return { text: "取出", tagClass: "tag-mid" }
  }

  if (eventType === "partial_take_out_candidate") {
    return { text: "待确认", tagClass: "tag-low" }
  }

  if (eventType === "uncertain") {
    return { text: "不确定", tagClass: "tag-low" }
  }

  if (eventType === "no_change") {
    return { text: "无变化", tagClass: "tag-neutral" }
  }

  return { text: eventType || "未知事件", tagClass: "tag-neutral" }
}
