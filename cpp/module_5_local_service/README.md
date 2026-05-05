# Module 5: Local Service

Module 5 currently provides two layers:

- `LocalServiceFacade`: builds JSON responses from `InventoryEngine`
- `LocalHttpServer`: exposes the facade and Module 4 mutation APIs over a lightweight local HTTP server

The HTTP layer is intentionally small and project-local. It uses standard C++ plus platform sockets, links `ws2_32` on Windows, and does not add a web framework or cloud dependency.

## Routes

- `GET /health`: facade health JSON
- `GET /inventory`: current inventory JSON
- `GET /events`: current event log JSON
- `GET /pending`: pending review JSON
- `POST /confirm`: parses a `PendingDecision`, calls `LocalServiceFacade::handle_confirm`, and saves SQLite state after a successful mutation when SQLite persistence is enabled
- `POST /manual_update`: parses a `ManualInventoryUpdate`, calls `LocalServiceFacade::handle_manual_update`, and saves SQLite state after a successful mutation when SQLite persistence is enabled

`GET /` returns a small text status message. `OPTIONS` returns minimal CORS headers for local browser/debug clients.

## Executables

`fridge_local_service_server` runs until Ctrl+C:

```powershell
build/cpp/fridge_local_service_server.exe --host 127.0.0.1 --port 8080
```

SQLite remains optional:

```powershell
build/cpp/fridge_local_service_server.exe `
  --host 127.0.0.1 `
  --port 8080 `
  --enable-sqlite-persistence `
  --sqlite-db data/runtime/fridge_inventory.db
```

If `--sqlite-db` is provided, SQLite persistence is enabled. If SQLite is requested but the binary was built without sqlite3 support, startup fails with a clear message instead of silently falling back.

`fridge_debug_local_http_server` starts a local server, sends real HTTP requests, and checks route behavior. When sqlite3 support is compiled, it also checks that an HTTP manual update is saved and restored through SQLite.

## Boundaries

This is a local HTTP baseline for browsers, mini-program debug clients, and future integration. It is not yet mini-program integration evidence, board deployment evidence, real camera evidence, real ONNX Runtime evidence, or long-running stability evidence.
