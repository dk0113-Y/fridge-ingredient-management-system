import { request } from "../utils/request"

export interface MutationResponse {
  ok: boolean
  session_id?: string
  item_name?: string
  message: string
}

interface ManualUpdatePayload {
  item_name: string
  category: string
  count: number
  remain_level: number
  expire_date: string
  note: string
}

interface ApplyPartialPayload {
  action: "accept"
  session_id: string
  item_name: string
  category: string
  count_delta: number
  remain_level: number
  note: string
}

interface ApplyPartialInput {
  session_id: string
  item_name: string
  category: string
  count_delta?: number
  remain_level: number
  note?: string
}

interface ManualUpdateInput {
  item_name: string
  category: string
  count: number
  remain_level: number
  expire_date?: string
  note?: string
}

export function manualAdjustInventory(payload: ManualUpdateInput) {
  return request<MutationResponse, ManualUpdatePayload>({
    url: "/manual_update",
    method: "POST",
    data: {
      item_name: payload.item_name,
      category: payload.category,
      count: payload.count,
      remain_level: payload.remain_level,
      expire_date: payload.expire_date || "",
      note: payload.note || "",
    },
  })
}

export function applyPartialConfirmation(payload: ApplyPartialInput) {
  return request<MutationResponse, ApplyPartialPayload>({
    url: "/confirm",
    method: "POST",
    data: {
      action: "accept",
      session_id: payload.session_id,
      item_name: payload.item_name,
      category: payload.category,
      count_delta: payload.count_delta ?? -1,
      remain_level: payload.remain_level,
      note: payload.note || "",
    },
  })
}
