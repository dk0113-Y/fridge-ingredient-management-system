# Module 2: YOLO Analysis

This module implements the second-stage coarse classification and diff-analysis chain.

Current status:

- it still contains the existing heuristic `event_detector`
- it now contains `YoloDiffAnalyzer` for detection matching, event reasoning, and crop planning
- it now contains `YoloRuntime`, which prefers ONNX Runtime for `models/best.onnx` and falls back to OpenCV DNN when ONNX Runtime is unavailable
- it now contains module-2 preprocessing, ONNX execution, and ONNX-output decoding, so the chain from `GrayFrame` to `YoloDetection` is defined inside C++
- it also contains debug artifact/report generation
- runtime config and diff-analysis config are now merged into one file: `cpp/configs/module_2_yolo.cfg`
- the repository currently contains both `models/best.pt` and `models/best.onnx`, and module-2 debug checks the configured ONNX asset directly
- continuous session replay can now poll module-1 output roots and backfill `stage2/*.json`, `final/event.json`, and Module 4/5 software closure evidence; the closure path is in-memory by default and can optionally load/save SQLite snapshots when sqlite3 support is compiled and explicitly requested

## Offline Session Replay

Module 1 session outputs under `data/` can now be replayed directly into module 2.

Typical input layout:

- `stage1/stage1_event.json` or `event.json`
- `stage1/before.jpg` / `before.jpg`
- `stage1/after.jpg` / `after.jpg`

After building, run:

```powershell
fridge_module2_session_runner.exe `
  --session-dir data/test_sessions/module12_realtime_live/<session_id> `
  --module2-mode mock `
  --config cpp/configs/module_2_yolo.cfg
```

To persist the software closure inventory state through SQLite when the binary was built with sqlite3 support:

```powershell
fridge_module2_session_runner.exe `
  --session-dir data/test_sessions/module12_realtime_live/<session_id> `
  --module2-mode mock `
  --enable-sqlite-persistence `
  --sqlite-db data/runtime/fridge_inventory.db
```

`--sqlite-db` implies SQLite persistence. If `--enable-sqlite-persistence` is set without a path, the default is `data/runtime/fridge_inventory.db`. `--reset-sqlite-db` exists only for deterministic debug/session validation.

Or replay the newest captured session under one root:

```powershell
fridge_module2_session_runner.exe `
  --latest-under data/test_sessions/module12_realtime_live `
  --module2-mode mock
```

Keep module 2 running and continuously poll for newly emitted module-1 sessions:

```powershell
fridge_module2_session_runner.exe `
  --watch-root data/test_sessions/module12_realtime_live `
  --module2-mode real_onnx_runtime `
  --poll-interval-ms 2000
```

For Linux validation with ONNX Runtime, point CMake at the SDK root:

```bash
cmake -S cpp -B cpp/build -G Ninja \
  -D FRIDGE_USE_OPENCV=ON \
  -D FRIDGE_USE_ONNXRUNTIME=ON \
  -D FRIDGE_ONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64
```

Outputs are written into the session's `stage2/` directory:

- `stage2/detections_before.json`
- `stage2/detections_after.json`
- `stage2/module2_result.json` with both `crop_requests` and realized `crop_artifacts`
- `stage2/crops/`
- `final/event.json`
- `final/inventory_response.json`
- `final/events_response.json`
- `final/pending_response.json`
- `final/software_closure_report.json`

Notes:

- `mock` mode reuses the stage-1 event type and change box to synthesize ONNX-like rows, so the existing C++ decode and diff-analysis path can be exercised on captured sessions today
- `real_onnx_runtime` mode runs the configured `best.onnx` model through ONNX Runtime when available; OpenCV DNN remains a fallback backend
- software closure evidence applies the final event to Module 4 `InventoryEngine` and writes Module 5 facade-style JSON responses; SQLite load/save is optional and explicit, while a real HTTP server is still pending
- `final/software_closure_report.json` now includes `sqlite_requested`, `sqlite_compiled`, `sqlite_db_ready`, `sqlite_loaded_existing_state`, `sqlite_saved_after_apply`, `sqlite_database_path`, and `sqlite_status_message`
- builds without ONNX Runtime and without OpenCV DNN can still use mock/debug/`.pgm` paths, but those paths do not execute the real ONNX graph
- the watcher only reprocesses sessions whose stage-1 inputs are newer than the existing stage-2/final JSON outputs
- reading `.jpg/.png` stage1 artifacts still requires an OpenCV-enabled build; `.pgm` artifacts still work without OpenCV in `mock` mode
- end-to-end YOLO models such as YOLO26 may expose separate box/score/class tensors, and the runtime now adapts to those layouts before diff analysis
