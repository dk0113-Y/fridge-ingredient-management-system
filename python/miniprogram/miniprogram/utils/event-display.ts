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

  if (eventType === "manual_add") {
    return { text: "手动新增", tagClass: "tag-good" }
  }

  if (eventType === "manual_edit") {
    return { text: "手动修改", tagClass: "tag-mid" }
  }

  if (eventType === "manual_delete") {
    return { text: "手动删除", tagClass: "tag-low" }
  }

  return { text: "无变化", tagClass: "tag-neutral" }
}
