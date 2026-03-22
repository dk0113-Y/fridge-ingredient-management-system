# Smart Fridge Ingredient Recognition and Inventory Management

Project title: 冰箱食材识别与管理系统

## Stage-1 Goal

This repository currently focuses on the first competition milestone:

1. Read a local video on the vision side.
2. Detect a real fridge interaction event.
3. Export a structured `event.json`.
4. Ingest the event on the Python side.
5. Update SQLite inventory records.
6. Show inventory, recent events, pending confirmations, and manual correction on a local web page.

The code is intentionally scoped to a minimal runnable closed loop. Real object classifiers, live camera streams, and embedded deployment hooks are left as explicit TODOs.

## Active Directories

```text
.
├── cpp/                    # Stage-1 C/C++ vision pipeline
├── python/                 # Stage-1 Python backend and web UI
├── shared/                 # Shared protocol between C++ and Python
├── docs/                   # Architecture, API, DB, pipeline, development plan
├── data/
│   ├── keyframes/          # Vision side debug keyframes
│   ├── outputs/            # Vision side event JSON and diff outputs
│   ├── runtime/            # Local runtime database files
│   └── sample/             # Small sample data only
├── cpp_infer/              # Early placeholder kept for compatibility
└── python_backend/         # Early placeholder kept for compatibility
```

## Collaboration Branch Model

- `main`: stable demo branch
- `dev`: daily integration branch
- `feature/*`: task branches for each teammate

## Recommended Work Split

- Member A: vision event detection, ROI motion analysis, C/C++ inference path
- Member B: SQLite inventory flow, web API, pending confirmations, local page

## Quick Start

### C++ vision demo

See [docs/vision_pipeline.md](D:/AAA研电赛/code_0/docs/vision_pipeline.md).

The stage-1 C++ target is under [cpp/CMakeLists.txt](D:/AAA研电赛/code_0/cpp/CMakeLists.txt). The core pipeline is:

- `video_io`: local video or frame-sequence loading
- `roi_motion`: motion summary inside the fridge ROI
- `frame_selector`: before/after keyframe selection
- `event_detector`: `no_change`, `put_in`, `take_out`, `partial_take_out_candidate`

With OpenCV enabled, the demo reads a real local video file and writes `.jpg` debug images. Without OpenCV, the same pipeline can still be debugged with a local `.pgm` frame directory.

### Python backend

See [docs/backend_api.md](D:/AAA研电赛/code_0/docs/backend_api.md).

The backend reads `data/outputs/*_event.json`, writes SQLite state, and serves a local page with:

- current inventory
- recent events
- pending confirmations
- manual correction entry

## TODO

- [ ] Replace the null classifier with a real lightweight category model.
- [ ] Replace file-based input with live camera stream input.
- [ ] Add embedded deployment adapters for the board-side runtime.
- [ ] Add richer event semantics for drawer state, multi-object scenes, and user confirmation strategy.
