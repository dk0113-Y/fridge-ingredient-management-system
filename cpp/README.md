# C++ Module Layout

`cpp/` is now organized directly around the 5-module structure defined in `docs/system_final_design_cpp_only.md`.

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
  - YOLO model-asset inspection
  - grayscale-to-model preprocessing and ONNX output decoding
  - YOLO box matching and diff analysis
  - debug report output
- `module_3_fine_grained/`
  - independent fine-grained recognizer client skeleton
  - mock mode, provider config, and remote-call abstraction
- `module_4_inventory/`
  - inventory rules, pending review, and manual correction logic
- `module_5_local_service/`
  - local service facade
  - service config loading
  - health, inventory, events, pending, confirm, and manual-update JSON responses
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
- module 2 now contains the stage-1 heuristic detector, ONNX model-asset inspection, grayscale preprocessing, ONNX output decoding, and a separate YOLO diff analyzer
- module 3 has an independent cloud recognizer skeleton
- module 4 now has an inventory rule engine with pending-review and manual-update flow
- module 5 now has a dedicated module directory with a local service facade that exposes health, inventory, events, pending, confirm, and manual-update JSON responses
- an actual embedded/local HTTP server is still pending

## Build

```powershell
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

If OpenCV is unavailable, use `-D FRIDGE_USE_OPENCV=OFF` and debug with a local `.pgm` frame directory.
