import { InventoryItem } from "../api/inventory"
import { formatTime } from "./util"

const CATEGORY_LABELS: Record<string, string> = {
  produce: "蔬果类",
  fresh_protein: "肉蛋生鲜类",
  beverage_dairy: "饮料乳品类",
  packaged_food: "包装食品类",
  other: "其他",
  fruit: "蔬果类",
  vegetable: "蔬果类",
  drink: "饮料乳品类",
  protein: "肉蛋生鲜类",
  unknown: "其他",
}

export function getCategoryLabel(category: string) {
  if (!category) {
    return "未分类"
  }

  return CATEGORY_LABELS[category] || category
}

export function getNameLabel(name: string) {
  if (!name || name === "unknown") {
    return "未命名食材"
  }

  return name
}

export function getRemainMeta(remainLevel: number) {
  const safeLevel = Math.max(0, Math.min(1, remainLevel))
  const percentText = `${Math.round(safeLevel * 100)}%`

  if (safeLevel <= 0.05) {
    return {
      label: "不足",
      className: "tag-low",
      percentText,
      progressStyle: `width: ${Math.round(safeLevel * 100)}%;`,
    }
  }

  if (safeLevel <= 0.3) {
    return {
      label: "偏低",
      className: "tag-low",
      percentText,
      progressStyle: `width: ${Math.round(safeLevel * 100)}%;`,
    }
  }

  if (safeLevel < 0.8) {
    return {
      label: "正常",
      className: "tag-mid",
      percentText,
      progressStyle: `width: ${Math.round(safeLevel * 100)}%;`,
    }
  }

  return {
    label: "充足",
    className: "tag-good",
    percentText,
    progressStyle: `width: ${Math.round(safeLevel * 100)}%;`,
  }
}

export function isLowInventory(item: Pick<InventoryItem, "count" | "remain_level">) {
  return item.count <= 1 || item.remain_level <= 0.3
}

export function formatUpdatedAt(updatedAt: string) {
  const normalized = updatedAt.replace(" ", "T")
  const date = new Date(normalized)

  if (Number.isNaN(date.getTime())) {
    return updatedAt
  }

  return formatTime(date)
}
