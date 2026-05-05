# 本地服务接口说明（全 C/C++ 版本）

## 1. 文档定位

本文档用于覆盖旧的 Python/Flask 后端接口说明，作为当前项目本地服务层的目标接口基线。

注意：本文档描述的是本地服务接口目标，不代表当前仓库已经接入完整 HTTP server。当前仓库中的模块 5 主要是 local service facade / JSON response baseline，已覆盖 health、inventory、events、pending、confirm、manual-update 等响应；真实 embedded/local HTTP server、端口监听和路由绑定尚未完成。

当前版本约束：

- 最终本地服务由 C/C++ 实现，不再使用 Python/Flask。
- 小程序部分暂不调整，因此接口字段应尽量稳定。
- 服务端主要面向本地网页调试和小程序读取，不追求复杂云端能力。

---

## 2. 服务职责

目标本地服务负责：

- 提供库存查询接口
- 提供事件记录查询接口
- 提供待确认事件确认接口
- 提供手动库存修正接口
- 提供健康检查接口

说明：

目标架构中，事件检测、YOLO 推理、数据库更新都由 C/C++ 主程序内部完成；HTTP 服务只负责对外暴露状态与控制入口。当前实现中，模块 5 仍是 facade，直接返回 JSON 字符串，尚未提供真实 HTTP server。

---

## 3. 推荐实现

后续接入真实 HTTP server 时，建议在 C/C++ 中使用：

- HTTP 服务：`cpp-httplib` 或 `CivetWeb`
- JSON：`nlohmann/json`
- SQLite：`sqlite3`。当前模块 4 已有可选 `SQLiteInventoryStore` persistence baseline；软件闭环/session runtime 可在显式启用时通过 SQLite load/save `InventoryEngine` 快照；模块 5 facade 仍主要从 `InventoryEngine` 状态生成响应，真实 HTTP server 读取 SQLite 仍待接入。

---

## 4. 路由定义

### `GET /health`

返回服务健康状态、数据库连接状态、最近事件时间等。

#### 建议返回

```json
{
  "status": "ok",
  "service": "fridge_local_service",
  "db_ready": true,
  "last_event_time": "2026-03-29 14:32:10"
}
```

---

### `GET /inventory`

返回当前库存项和待确认统计。

#### 建议返回

```json
{
  "items": [
    {
      "item_id": 1,
      "item_name": "光明酸奶",
      "category": "packaged_food",
      "count": 2,
      "remain_level": "full",
      "expire_date": "2026-04-12",
      "last_update_time": "2026-03-29 14:32:10"
    }
  ],
  "pending_review_count": 1
}
```

---

### `GET /events`

按时间倒序返回最近事件列表。

#### 建议返回

```json
{
  "events": [
    {
      "session_id": "session_20260329_01",
      "event_type": "put_in",
      "coarse_class": "packaged_food",
      "fine_name": "光明酸奶",
      "quantity_delta": 1,
      "yolo_confidence": 0.82,
      "llm_confidence": 0.88,
      "review_required": false,
      "timestamp": "2026-03-29 14:32:10"
    }
  ]
}
```

---

### `POST /confirm`

用于处理待确认事件，主要包括：

- 果蔬类部分取出候选确认
- `uncertain` 事件驳回或接受
- 手动修正数量或剩余量

#### 建议请求字段

- `action`
- `session_id`
- `item_name`
- `category`
- `count_delta`
- `remain_level`
- `note`

#### 建议请求示例

```json
{
  "action": "confirm_partial_take",
  "session_id": "session_20260329_02",
  "item_name": "草莓",
  "category": "fruit_vegetable",
  "count_delta": -1,
  "remain_level": "half",
  "note": "用户确认取出部分草莓"
}
```

---

### `POST /manual_update`

用于手动修正库存项，适合演示与复杂场景兜底。

#### 建议请求字段

- `item_name`
- `category`
- `count`
- `remain_level`
- `expire_date`
- `note`

---

### `GET /pending`

返回当前待确认事件列表。

#### 建议返回

```json
{
  "pending": [
    {
      "session_id": "session_20260329_02",
      "event_type": "partial_take_out_candidate",
      "category": "fruit_vegetable",
      "item_name": "草莓",
      "reason": "matched target changed but quantity unchanged",
      "timestamp": "2026-03-29 15:06:22"
    }
  ]
}
```

---

## 5. 与数据库的关系

HTTP 服务不再扫描 `event.json` 进行导入。

目标版本改为：

- C/C++ 主程序内部直接生成事件对象
- 直接写入 SQLite
- HTTP 服务从 SQLite 读取库存、事件和待确认数据
- `event.json` 仅作为调试日志和可追溯记录

当前仓库已有可选 Module 4 SQLite persistence baseline，可保存和恢复 `InventoryEngine` 快照；Module 2 session replay 和 module12 live harness 可在 sqlite3 可用且显式启用时先从 SQLite 加载状态、应用当前事件、再保存更新后的快照。模块 5 当前仍从 facade/engine 状态生成 JSON 响应，尚未完成真实 HTTP server 直接读取 SQLite。

---

## 6. 当前正式结论

1. 最终本地服务统一由 C/C++ 实现。
2. 不再保留 Python/Flask 作为最终架构组成部分。
3. 小程序当前不做调整，因此接口字段尽量保持稳定。
4. 目标事件主链为“C/C++ 主程序内部处理并直接写库”，而非“扫描 JSON 再导入数据库”；当前已有软件闭环可选 SQLite load/save baseline，仍需补齐真实 HTTP server。
