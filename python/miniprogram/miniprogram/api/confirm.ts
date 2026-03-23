import { request } from "../utils/request"

interface ConfirmResponse {
  status: string
  action: string
  session_id: string
}

interface ManualAdjustPayload {
  action: "manual_adjust"
  inventory_id?: number
  item_name: string
  category: string
  count_delta: number
  remain_level: number
}

interface ApplyPartialPayload {
  action: "apply_partial"
  session_id: string
  item_name: string
  category: string
  remain_level: number
  note?: string
}

export function manualAdjustInventory(payload: Omit<ManualAdjustPayload, "action">) {
  return request<ConfirmResponse, ManualAdjustPayload>({
    url: "/confirm",
    method: "POST",
    data: {
      action: "manual_adjust",
      ...payload,
    },
  })
}

export function applyPartialConfirmation(payload: Omit<ApplyPartialPayload, "action">) {
  return request<ConfirmResponse, ApplyPartialPayload>({
    url: "/confirm",
    method: "POST",
    data: {
      action: "apply_partial",
      ...payload,
    },
  })
}
