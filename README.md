# Smart Fridge Ingredient Recognition and Inventory Management

This repository is moving toward the all-C/C++ target baseline defined in
[`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md).
That document is a final architecture baseline, not a complete list of what is
already implemented.

## Active Directories

```text
.
|-- cpp/                    # C++ modules, configs, tests, and main entry
|   |-- configs/            # Runtime config used by the C++ pipeline
|   |-- module_1...5        # Module directories from the final design
|   `-- main.cpp            # Main control entry
|-- python/
|   |-- miniprogram/        # Existing mini program kept as-is
|   `-- model_tools/        # Offline conversion helpers such as YOLO PT -> ONNX
|-- docs/                   # Final design and retained implementation notes
|-- data/
|   |-- runtime/            # Runtime database and local state
|   |-- sessions/           # Per-run debug outputs grouped by session
|   `-- video/              # Local test videos
|-- models/                 # YOLO model assets and notes
```

## What Is Implemented

- `cpp/`: stage-1 keyframe extraction, ROI motion analysis, event classification, and debug artifact output
- `cpp/module_2_yolo_analysis/`: YOLO runtime using ONNX Runtime first and OpenCV DNN as fallback, preprocessing, ONNX output decoding, box matching, and YOLO diff-analysis baseline
- `cpp/module_3_fine_grained/`: C++ fine-grained recognition client skeleton with mock mode and provider-neutral config
- `cpp/module_4_inventory/`: inventory rule engine, pending review flow, and manual update baseline
- `cpp/module_5_local_service/`: local service facade and lightweight local HTTP server baseline for health, inventory, events, pending review, confirm, and manual update
- `python/miniprogram/`: retained mini program frontend
- `python/model_tools/`: offline support scripts for model export and related tooling

## Key Documents

- [`docs/project_baseline.md`](docs/project_baseline.md): project tracking index and current implementation status for GPT/Codex-assisted development
- [`AGENTS.md`](AGENTS.md): repository-level execution rules for Codex
- [`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md): final target architecture baseline
- [`docs/backend_api_cpp_only.md`](docs/backend_api_cpp_only.md): target C++ local service/API baseline
- [`docs/vision_pipeline.md`](docs/vision_pipeline.md): module-1 vision front-end notes; module-2 status lives under `cpp/module_2_yolo_analysis/`

## Current Limits

- `models/best.onnx` is now wired into module 2 through ONNX Runtime when available, with OpenCV DNN kept as a secondary fallback
- `models/best.pt` is retained as the original training/export weight file
- builds without ONNX Runtime / OpenCV DNN still fall back to mock / `.pgm` debug paths and do not execute the ONNX graph
- module 4 still uses `InventoryEngine` as the rule engine, but software closure/session runs can optionally load/save its state through SQLite when sqlite3 is available
- module 5 now has a lightweight C++ local HTTP server baseline over the facade; the retained mini-program has an API adapter baseline for these C++ local HTTP routes, while real WeChat Developer Tools, phone LAN, board, camera, and long-running HTTP/SQLite validation are still pending
