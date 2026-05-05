# Mini-program integration with C++ local service

This mini-program is the retained frontend for the smart-fridge demo. Its API
layer targets the C++ local HTTP server exposed by Module 5.

## Start the service

Build first, then start the local server from the repository root:

```powershell
build/cpp/fridge_local_service_server.exe --host 127.0.0.1 --port 8080
```

For LAN or board-side testing, bind to a reachable host address such as
`--host 0.0.0.0 --port 8080`, then set the mini-program baseUrl to
`http://<board-or-PC-ip>:8080`.

## baseUrl

The mini-program keeps the saved baseUrl mechanism in
`miniprogram/utils/backend.ts`.

- Simulator default: `http://127.0.0.1:8080`
- WeChat Developer Tools simulator on the same PC: `http://127.0.0.1:8080`
- Real phone/device on LAN: `http://<board-or-PC-ip>:8080`
- Do not hard-code a LAN IP in source; use the connect/settings pages.

## Supported routes

- `GET /health`
- `GET /inventory`
- `GET /events`
- `GET /pending`
- `POST /confirm`
- `POST /manual_update`

The API modules normalize C++ fields into existing page view models:

- `item_id` -> `id`
- `item_name` -> `name`
- `last_update_time` -> `updated_at`
- label-style `remain_level` values (`full`, `half`, `low`, `empty`) -> numeric UI levels
- `/pending` records get a stable view id generated from `session_id` and list index; `session_id` remains the actual confirm key.

## Manual flows

- Pending confirmation uses `POST /confirm` with `action: "accept"`.
- The current partial-take-out confirmation UI sends `count_delta: -1` by default and preserves `item_name`, `category`, `remain_level`, and `note`.
- Inventory add/edit/delete uses `POST /manual_update` with absolute `count`; delete is represented as `count: 0`.

## WeChat local HTTP notes

- In WeChat Developer Tools, enable "Do not verify legal domain name" for local HTTP debugging.
- Real-device HTTP debugging requires the phone/device and server to be on the same LAN.
- Production mini-program mode has HTTPS/legal-domain requirements that are not covered by this local debug baseline.

## Current validation boundary

This repository change can be checked with TypeScript/static validation and the
existing C++ local HTTP debug executables. Real WeChat Developer Tools
interaction, phone LAN testing, board deployment, real camera validation, and
real ONNX Runtime validation still require human/manual validation.
