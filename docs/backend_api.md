# Backend API

## Purpose

The Python backend ingests vision-side event JSON files, stores event history, updates SQLite inventory state, and exposes a minimal local web/API surface.

## Routes

### `GET /health`

Returns service health plus current record counts.

### `GET /events`

Returns recent events in reverse chronological order.

### `GET /inventory`

Returns current inventory items and pending confirmations.

### `POST /confirm`

Handles:

- partial event confirmation
- dismissal of a pending candidate
- manual inventory adjustment

Supported form or JSON fields:

- `action`
- `session_id`
- `item_name`
- `category`
- `count_delta`
- `remain_level`
- `note`

### `GET /`

Renders a minimal local page for demo use.

## Sync Behavior

Before each read endpoint and page render, the backend scans `data/outputs/*_event.json` and imports unseen events.

This keeps the first demo loop simple:

- run C++ detector
- refresh the web page
- see the updated state
