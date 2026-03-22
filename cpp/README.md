# Stage-1 C++ Vision Pipeline

This directory contains the minimal C++17 vision chain for the fridge event session:

1. load a local video or a debug frame directory
2. measure motion inside a configured ROI
3. pick a stable `before` frame and a stable `after` frame
4. compare the two keyframes
5. write `before`, `after`, `overlay`, and `event.json`

The stage-1 scope is intentionally narrow. Real category classification is still a TODO.

## Module Map

- `include/types.hpp`
  - shared structs used by the stage-1 pipeline
  - `EventResult` matches `shared/event_schema.json`
- `include/video_io.hpp` / `src/video_io.cpp`
  - load frames from a real video file when OpenCV is enabled
  - fallback to `.pgm` frame-directory input when OpenCV is disabled
  - write debug keyframes and overlay images
- `include/roi_motion.hpp` / `src/roi_motion.cpp`
  - compute motion summary inside a configured ROI
  - output changed ratio, signed mean delta, and change regions
- `include/frame_selector.hpp` / `src/frame_selector.cpp`
  - reject unusable black tail frames
  - find the first effective disturbance and the last effective disturbance
  - pick `before` from the stable part before motion
  - pick `after` from the stable part after motion
- `include/event_detector.hpp` / `src/event_detector.cpp`
  - classify `no_change`, `put_in`, `take_out`, or `partial_take_out_candidate`
  - combine polarity, multi-region balance, and region-vs-background consistency
  - keep a null classifier hook for later coarse classification
- `include/runtime_config.hpp` / `src/runtime_config.cpp`
  - load ROI and threshold settings from `configs/vision_stage1.cfg`
  - keep CLI ROI overrides available for quick debug
- `include/debug_report.hpp` / `src/debug_report.cpp`
  - write a separate `*_debug.json`
  - keep selection indices, thresholds, transition summaries, and output paths
- `src/main.cpp`
  - demo entry
  - current CLI accepts input path plus optional ROI settings
- `tests/test_main.cpp`
  - lightweight regression tests for stage-1 heuristics

## Current Keyframe Logic

The selector follows the stage-1 session model:

1. stable scene before the operation
2. disturbance inside ROI
3. stable scene after the operation

Implementation notes:

- black or near-black tail frames are treated as unusable
- only transitions between usable frames can start or end a motion segment
- `before` is taken from the usable frames before the first effective motion
- `after` is taken from the usable stable run after the last effective motion
- if no effective motion is found, the pipeline falls back to the first and last usable frames

This keeps the logic aligned with the competition requirement of event-based session analysis instead of hand detection.

## Build

### Windows, MinGW + Ninja, no OpenCV

Use an ASCII alias when the workspace path contains Chinese characters, otherwise `ninja` may fail during incremental scans.

```powershell
cmd /c subst X: "D:\AAA研电赛\code_0"
cmake -S X:\cpp -B X:\build\cpp-mingw -G Ninja `
  -D CMAKE_CXX_COMPILER="C:/Users/27319/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin/g++.exe" `
  -D FRIDGE_USE_OPENCV=OFF
cmake --build X:\build\cpp-mingw
ctest --test-dir X:\build\cpp-mingw --output-on-failure
```

### Ubuntu

```bash
cmake -S cpp -B build/cpp -G Ninja -D FRIDGE_USE_OPENCV=ON
cmake --build build/cpp
ctest --test-dir build/cpp --output-on-failure
```

If OpenCV is not available yet, set `FRIDGE_USE_OPENCV=OFF` and debug with a directory of `.pgm` frames.

## Config

By default, the demo auto-loads [configs/vision_stage1.cfg](D:/AAA研电赛/code_0/configs/vision_stage1.cfg) if the file exists.

You can also pass a custom config file:

```powershell
fridge_vision_demo.exe <video_or_frame_dir> --config configs/vision_stage1.cfg
```

Override order:

1. built-in defaults
2. config file values
3. CLI `--roi` and `--roi-id`

Use `roi = 0,0,0,0` to keep full-frame mode before the fridge ROI is calibrated.

## Demo Usage

```powershell
fridge_vision_demo.exe <video_or_frame_dir> --config configs/vision_stage1.cfg --roi 40,60,420,300 --roi-id shelf_main
```

Outputs:

- `data/sessions/<session_id>/before.jpg` or `.pgm`
- `data/sessions/<session_id>/after.jpg` or `.pgm`
- `data/sessions/<session_id>/overlay.jpg` or `.pgm`
- `data/sessions/<session_id>/event.json`
- `data/sessions/<session_id>/debug.json`

## Test Coverage

Current C++ tests cover:

- identical frames -> `no_change`
- dark object added -> `put_in`
- dark object removed -> `take_out`
- bright object added -> `put_in`
- bright object removed -> `take_out`
- weak sign evidence -> `partial_take_out_candidate`
- low-light object addition
- temporary occlusion with no final change
- ROI-outside motion should be ignored
- object rearrangement with inventory unchanged -> `no_change`
- trailing black frame should not be selected as the final keyframe

## TODO

- connect a lightweight coarse classifier after event detection is stable
- add ROI config file support for fixed shelf presets
- add live camera input
- add embedded board runtime adapters
