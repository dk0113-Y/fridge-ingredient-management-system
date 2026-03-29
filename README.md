# Smart Fridge Ingredient Recognition and Inventory Management

This repository follows the all-C/C++ baseline defined in
[`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md).

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
- `cpp/module_2_yolo_analysis/`: ONNX model-asset inspection, grayscale preprocessing, ONNX output decoding, box matching, and YOLO diff-analysis baseline
- `cpp/module_3_fine_grained/`: C++ fine-grained recognition client skeleton with mock mode and provider-neutral config
- `cpp/module_4_inventory/`: inventory rule engine, pending review flow, and manual update baseline
- `cpp/module_5_local_service/`: local service facade for health, inventory, events, pending review, confirm, and manual update
- `python/miniprogram/`: retained mini program frontend
- `python/model_tools/`: offline support scripts for model export and related tooling

## Key Documents

- [`docs/system_final_design_cpp_only.md`](docs/system_final_design_cpp_only.md): final architecture baseline
- [`docs/backend_api_cpp_only.md`](docs/backend_api_cpp_only.md): C++ local HTTP API baseline
- [`docs/vision_pipeline.md`](docs/vision_pipeline.md): current C++ vision pipeline notes

## Current Limits

- `models/best.onnx` is now tracked by module 2, and module 2 already covers preprocessing plus ONNX-output decoding, but the current C++ runtime still does not execute the ONNX graph itself
- `models/best.pt` is retained as the original training/export weight file
- real YOLO inference still needs an ONNX Runtime or another C++ inference backend
- module 4 is still an in-memory inventory baseline and has not been switched to SQLite yet
- module 5 is still a local service facade and has not been connected to a real HTTP server yet
