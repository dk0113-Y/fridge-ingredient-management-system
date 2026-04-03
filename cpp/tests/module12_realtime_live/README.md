# Module12 Realtime Live Harness

This directory contains a test-only live harness for validating the realtime linkage of:

- module 1: event detection and keyframe extraction
- module 2: YOLO differential analysis

It is not the final product version of module 5. The goal is to validate a live camera chain on a Linux laptop:

1. open one camera device
2. publish live MJPEG preview
3. maintain one stable baseline state for module 1 on the shared realtime frame stream
4. open an event only when the scene leaves that baseline and then settles into a new stable state
5. trigger module 2 only after valid `before` and `after` stable states are produced
6. persist every event into an isolated session directory

## Capture Strategy

The live harness no longer treats adjacent-frame motion as the final authority for keyframe extraction.
The current capture logic is:

1. wait for an initial stable baseline after startup
2. keep refreshing that baseline while the scene stays stable
3. open a disturbance window only when either:
   - adjacent-frame motion crosses the motion threshold, or
   - the current frame has drifted far enough from the stable baseline
4. suppress global exposure/brightness shifts with ROI-wide brightness compensation
5. emit a session only after the scene becomes stable again
6. use `baseline_frame -> settled_frame` as `before -> after`

This means the harness is now optimized for:

- long idle periods after boot
- continuous `put in / take out / reorganize` actions
- non-hand interactions such as rope-assisted pulls
- clean `before` and `after` frames instead of motion-midpoint snapshots

## Dependencies

- Linux with a camera device, usually `/dev/video0`
- CMake 3.16+
- A C++17 compiler
- OpenCV with `core`, `imgproc`, `imgcodecs`, `videoio`

## Build

```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build
```

Binary:

```bash
cpp/build/fridge_module12_realtime_live
```

## Live Preview

Start preview and the lightweight local status service:

```bash
cpp/tests/module12_realtime_live/scripts/run_live_preview.sh --device /dev/video0
```

Open in a browser:

- default behavior is LAN-first: bind on `0.0.0.0` and show your primary LAN IP
- open `http://<your_linux_ip>:8080/`
- open `http://<your_linux_ip>:8080/live.mjpeg`
- open `http://<your_linux_ip>:8080/status.json`
- open `http://<your_linux_ip>:8080/latest_event.json`

Stop it:

```bash
cpp/tests/module12_realtime_live/scripts/stop_live_test.sh
```

## Realtime Module12 Test

Run a single event capture and stop after the first finished session:

```bash
cpp/tests/module12_realtime_live/scripts/run_live_module12_test.sh \
  --case-id TC02_live_put_in \
  --device /dev/video0 \
  --module2-mode mock
```

If the YOLO backend is not connected yet, prefer capture-only validation:

```bash
cpp/tests/module12_realtime_live/scripts/run_live_module12_test.sh \
  --case-id CAPTURE_put_trial01 \
  --device /dev/video0 \
  --capture-only
```

In `capture_only` mode:

- module 1 now drives a stable-state capture machine instead of the older adjacent-frame event window
- `before.jpg`, `after.jpg`, and `overlay.jpg` are now saved as color camera snapshots, while the algorithm still runs on grayscale internally
- `before.jpg`, `after.jpg`, `overlay.jpg`, and `stage1_debug.json` remain the primary acceptance artifacts
- module 2 JSON is written as `stage2_skipped`
- browser status shows `capture_recorded` instead of pretending the semantic result is authoritative
- `test_report.json` now records `peak_interframe_ratio`, `peak_baseline_change_ratio`, `final_change_ratio`, and stable-run lengths for the emitted session
- transient disturbances that almost return to the original state are filtered out instead of being kept as weak sessions

Optional ROI override:

```bash
cpp/tests/module12_realtime_live/scripts/run_live_module12_test.sh \
  --case-id TC03_live_take_out \
  --device /dev/video0 \
  --module2-mode mock \
  --roi 120,80,900,520
```

## Test Cases

- `TC01_live_no_change`: only slight jitter/noise, target is `no_change`
- `TC02_live_put_in`: put one large object into the ROI, target is `put_in`
- `TC03_live_take_out`: remove one large object from the ROI, target is `take_out`
- `TC04_live_partial_fruit`: partial change for fruit/vegetable content, target is `partial_take_out_candidate`

If YOLO is not connected yet, you can also use non-semantic capture labels such as:

- `CAPTURE_put_trial01`
- `CAPTURE_take_trial01`
- `CAPTURE_no_change_trial01`

Important rule:

- `partial_take_out_candidate` is enabled only for `fruit_vegetable`
- drink scenes do not auto-judge partial removal

See the `cases/` directory for short operator notes.

## Output Layout

Every finished event writes a session under:

```text
data/test_sessions/module12_realtime_live/<session_id>/
```

Main outputs:

- `preview/latest_preview.jpg`
- `stage1/before.jpg`
- `stage1/after.jpg`
- `stage1/overlay.jpg`
- `stage1/stage1_event.json`
- `stage1/stage1_debug.json`
- `stage2/detections_before.json`
- `stage2/detections_after.json`
- `stage2/module2_result.json`
- `stage2/crops/`
- `final/event.json`
- `final/test_report.json`
- `meta/live_capture_meta.json`
- `meta/run_manifest.json`

The most recent run summary is also written to:

```text
cpp/tests/module12_realtime_live/manifests/latest_run.json
```

## Mode Notes

`mock`

- fully runnable today
- uses synthetic ONNX-like outputs to drive the existing C++ module 2 diff logic
- validates stage1 -> stage2 -> session artifacts -> final JSON linkage

`real_onnx_runtime`

- intentionally honest about current status
- the harness checks the current C++ runtime readiness through the existing module 2 runtime inspection
- if the backend is not ready, the harness writes stage2 JSON with the failure reason and falls back to the module 1 final event

`capture_only`

- recommended when the real YOLO backend is not connected
- validates only trigger stability, keyframe capture quality, and session completeness
- semantic labels in `stage1_event.json` are treated as debug output, not as pass/fail truth
- the session is emitted only after `stable baseline -> disturbance -> stable final state`

## Continuous Capture SOP

For continuous capture validation, keep the process running and do not stop after the first session:

```bash
cpp/tests/module12_realtime_live/scripts/run_live_module12_test.sh \
  --case-id CAPTURE_continuous_trial01 \
  --device /dev/video0 \
  --capture-only \
  --stop-after-events 0 \
  --duration-seconds 0 \
  --roi 220,140,420,240
```

Recommended operator rhythm:

- leave the ROI stable for 1 to 2 seconds before each action
- finish the action and move the hand or rope out of the ROI
- leave the final state stable for 1 to 2 seconds before the next action

Acceptance criteria for each emitted session:

- `before.jpg` is the clean pre-action stable state
- `after.jpg` is the clean post-action stable state
- `overlay.jpg` covers the real changed area instead of only the transient mover
- `stage1_debug.json` shows a non-trivial `final_change_ratio`
- `final/test_report.json` reports `capture_recorded` with `capture_valid=true`

## LAN Notes

- future live tests are expected to run on the LAN by default
- scripts now default to `--bind-host 0.0.0.0`
- `--public-host auto` tries to publish URLs using the machine's primary LAN IPv4
- you can override it explicitly if the machine has multiple NICs:

```bash
./cpp/tests/module12_realtime_live/scripts/run_live_preview.sh \
  --device /dev/video0 \
  --bind-host 0.0.0.0 \
  --public-host 192.168.1.23 \
  --port 8080
```

## Current Status

- Realtime camera preview: implemented
- Shared capture thread for preview and module 1: implemented
- Session-based artifacts and JSON: implemented
- Module 2 mock linkage: implemented
- Real ONNX Runtime inference backend: implemented when the build is configured with ONNX Runtime
- Live page now exposes `/latest_stage2.json` and `/latest_final_event.json` alongside the MJPEG preview
