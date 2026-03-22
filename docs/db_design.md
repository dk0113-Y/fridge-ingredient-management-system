# SQLite Design

## Tables

### `inventory_items`

- `id`
- `name`
- `category`
- `count`
- `remain_level`
- `updated_at`

Tracks the latest stock snapshot used by the local page.

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

Stores imported event payloads. `session_id` is unique for stage-1 idempotent import.

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

Holds ambiguous cases such as `partial_take_out_candidate`.

## Update Rules

- `put_in` -> `count + 1`
- `take_out` -> `count - 1`, clamped to zero
- `partial_take_out_candidate` -> update remain level when possible and/or create a pending confirmation
- `no_change` -> record only

## Stage-1 Notes

- The classifier is still a stub, so many records will use `unknown` item metadata.
- The schema favors readability and demo stability over normalization.
