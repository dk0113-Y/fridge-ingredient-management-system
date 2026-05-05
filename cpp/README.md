# C++ Module Layout

`cpp/` is now organized directly around the 5-module target structure defined in `docs/system_final_design_cpp_only.md`.

## Directory Layout

- `main.cpp`
  - module 5 entry point
  - current demo orchestration and CLI handling
- `module_1_event_capture/`
  - low-cost motion analysis
  - keyframe selection
  - runtime config loading
  - local image and video input/output
- `module_2_yolo_analysis/`
  - heuristic event classification logic
  - YOLO model-asset inspection and ONNX execution through ONNX Runtime first, with OpenCV DNN as fallback
  - grayscale-to-model preprocessing and ONNX output decoding
  - YOLO box matching and diff analysis
  - debug report output
- `module_3_fine_grained/`
  - independent fine-grained recognizer client skeleton
  - mock mode, provider config, and remote-call abstraction
- `module_4_inventory/`
  - inventory rules, pending review, and manual correction logic
  - optional SQLite persistence adapter for saving and restoring inventory snapshots
- `module_5_local_service/`
  - local service facade
  - lightweight local HTTP server baseline
  - service config loading
  - health, inventory, events, pending, confirm, and manual-update JSON responses
  - software closure evidence helper for applying final events to the in-memory inventory engine
- `configs/`
  - `module_1_event_capture.cfg`
  - `module_2_yolo.cfg`
  - `module_3_fine_grained.cfg`
  - `module_4_inventory.cfg`
  - `module_5_local_service.cfg`
- `tests/`
  - stage-1 regression tests for the current C++ main chain
  - `debug_module_1...5` single-module debug executables

## Current Implementation Boundary

The repository has only partially reached the final 5-module target:

- module 1 is implemented
- module 2 now contains the stage-1 heuristic detector, ONNX model-asset inspection/execution through ONNX Runtime first and OpenCV DNN as fallback, grayscale preprocessing, ONNX output decoding, and a separate YOLO diff analyzer
- module 3 has an independent cloud recognizer skeleton
- module 4 now has an inventory rule engine with pending-review and manual-update flow, plus an optional SQLite persistence baseline when sqlite3 is available
- module 5 now has a dedicated module directory with a local service facade and a lightweight local HTTP server baseline that exposes health, inventory, events, pending, confirm, and manual-update routes
- module 2 session replay and the module12 live harness can now write software closure evidence under each session's `final/` directory: `inventory_response.json`, `events_response.json`, `pending_response.json`, and `software_closure_report.json`; this path remains in-memory by default and can optionally load/save SQLite snapshots when sqlite3 is available and explicitly requested
- mini-program integration, board-side deployment, real camera/ONNX validation, and long-running HTTP/SQLite stability validation are still pending

## Build

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

If you want the native ONNX Runtime backend for module 2, add:

```powershell
-D FRIDGE_USE_ONNXRUNTIME=ON -D FRIDGE_ONNXRUNTIME_ROOT=<onnxruntime-sdk-root>
```

`FRIDGE_USE_ONNXRUNTIME` defaults to `ON`, but it only enables the backend when CMake can find the ONNX Runtime headers and library. If OpenCV with `dnn` is unavailable, use `-D FRIDGE_USE_OPENCV=OFF` and debug with a local `.pgm` frame directory. Builds without ONNX Runtime and without OpenCV DNN can still use mock/debug paths, but they do not execute `models/best.onnx`.

SQLite support for module 4 is controlled by:

```powershell
-D FRIDGE_USE_SQLITE=ON
```

`FRIDGE_USE_SQLITE` defaults to `ON`, but the SQLite adapter, SQLite closure wrapper, and SQLite debug executables are built only when CMake finds sqlite3 development files. If sqlite3 is unavailable, the in-memory inventory flow, software closure debug path, session runner, live harness, and normal tests still build and run.

When SQLite support is compiled, module-2 session replay can persist software closure state:

```powershell
fridge_module2_session_runner.exe `
  --session-dir data/test_sessions/module12_realtime_live/<session_id> `
  --module2-mode mock `
  --enable-sqlite-persistence `
  --sqlite-db data/runtime/fridge_inventory.db
```

`--sqlite-db` implies `--enable-sqlite-persistence`. If no database path is provided, the runtime default is `data/runtime/fridge_inventory.db`. `--reset-sqlite-db` is available for deterministic debug/session runs. `fridge_module12_realtime_live` also accepts `--enable-sqlite-persistence` and `--sqlite-db <path>`; it does not reset the database automatically.

## Local HTTP Server

`fridge_local_service_server` exposes the current Module 5 facade and Module 4 mutation APIs through a lightweight project-local C++ socket server. It is single-process and intended as a local/edge baseline, not a production web framework.

Routes:

- `GET /health`
- `GET /inventory`
- `GET /events`
- `GET /pending`
- `POST /confirm`
- `POST /manual_update`

Basic launch:

```powershell
build/cpp/fridge_local_service_server.exe --host 127.0.0.1 --port 8080
```

Manual debug request:

```powershell
curl http://127.0.0.1:8080/health
curl http://127.0.0.1:8080/inventory
curl -X POST http://127.0.0.1:8080/manual_update -H "Content-Type: application/json" -d "{\"item_name\":\"demo_yogurt\",\"category\":\"packaged_food\",\"count\":1,\"remain_level\":1.0,\"expire_date\":\"2026-04-12\",\"note\":\"debug manual update\"}"
```

Options:

- `--host <ip>`: default from `cpp/configs/module_5_local_service.cfg`, falling back to `0.0.0.0`
- `--port <number>`: default from `cpp/configs/module_5_local_service.cfg`, falling back to `8080`
- `--service-config <path>`: Module 5 service config
- `--inventory-config <path>`: Module 4 inventory rules config
- `--enable-sqlite-persistence`: load/save an `InventoryEngine` snapshot through SQLite when sqlite3 support was compiled
- `--sqlite-db <path>`: SQLite database path; implies `--enable-sqlite-persistence`; default is `data/runtime/fridge_inventory.db`
- `--seed-demo-data` or `--demo-data`: seed one deterministic inventory item for manual route checks

When SQLite support is compiled, the server initializes the schema, loads an existing snapshot at startup, and saves a snapshot after successful `/confirm` or `/manual_update` mutations. If SQLite persistence is requested but sqlite3 support was not compiled, startup fails with `SQLite persistence requested but this binary was built without sqlite3 support`. Without SQLite, the server runs in in-memory mode.

`fridge_debug_local_http_server` starts the server on `127.0.0.1:18080`, sends real HTTP requests, and verifies `/health`, `/inventory`, `/manual_update`, `/events`, `/pending`, and an invalid `/confirm` request. When SQLite support is compiled, the same debug executable also verifies HTTP manual update persistence and reload through SQLite. This is local debug evidence only; it is not mini-program, board, real camera, or real ONNX validation.

## Software Closure Debug

`fridge_debug_software_closure` exercises the current in-memory Module 2 event -> Module 4 inventory -> Module 5 facade evidence path with deterministic mock/debug events. It covers a committed `put_in`, a matching `take_out`, `partial_take_out_candidate`, `uncertain`, and low-confidence pending-review cases. Its artifacts are debug evidence only; they are not real ONNX, camera, or board validation.

When SQLite support is available, `fridge_debug_sqlite_persistence` writes `sqlite_persistence_debug/fridge_inventory.db` under the build output directory and verifies that inventory, events, pending reviews, and inventory change log records survive reload. `fridge_debug_software_closure_sqlite` verifies the runtime closure path: load previous `InventoryEngine` state from SQLite, apply a new event, save the updated snapshot, reload it, and check SQLite status fields in `software_closure_report.json`. These are SQLite debug evidence, not real camera, real ONNX, or board validation. The HTTP-specific SQLite debug path lives in `fridge_debug_local_http_server` when sqlite3 support is compiled.
