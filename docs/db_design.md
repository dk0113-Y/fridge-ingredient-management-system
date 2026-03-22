# SQLite 数据库设计

## 数据表

### `inventory_items`

- `id`
- `name`
- `category`
- `count`
- `remain_level`
- `updated_at`

用于保存当前库存快照，供本地页面直接展示。

### `events`

- `id`
- `session_id`
- `timestamp`
- `event_type`
- `roi_id`
- `confidence`
- `before_frame`
- `after_frame`
- `need_user_confirm`
- `raw_json`
- `source_file`
- `created_at`

用于保存导入后的事件载荷。第一阶段通过唯一的 `session_id` 保证事件导入幂等。

### `pending_confirmations`

- `id`
- `event_id`
- `session_id`
- `status`
- `item_name`
- `category`
- `remain_level`
- `note`
- `created_at`
- `resolved_at`

用于保存 `partial_take_out_candidate` 一类的模糊事件及其确认状态。

## 更新规则

- `put_in` -> `count + 1`
- `take_out` -> `count - 1`，最小截断为 0
- `partial_take_out_candidate` -> 尽可能更新 `remain_level`，并在需要时写入待确认表
- `no_change` -> 只记录事件，不更新库存

## 第一阶段说明

- 当前分类器仍是占位接口，因此很多记录会使用 `unknown` 作为物品元数据。
- 当前表结构优先考虑可读性、联调效率和演示稳定性，而不是高度范式化。
