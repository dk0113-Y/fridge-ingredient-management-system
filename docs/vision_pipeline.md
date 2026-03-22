# Vision Pipeline Notes

## Goal

The C++ vision chain focuses on event detection first, then object existence, and only later on fine-grained category classification.

## Stage-1 State Machine

1. Load frames from a local source.
2. Compute motion summaries between adjacent frames.
3. Select one stable `before` frame and one stable `after` frame around the strongest interaction window.
4. Compare the selected pair inside the fridge ROI.
5. Emit one of:
   - `no_change`
   - `put_in`
   - `take_out`
   - `partial_take_out_candidate`

## Heuristics

- `changed_ratio`: how much of the ROI changed beyond a pixel threshold
- `mean_delta`: average signed brightness change inside the changed area
- `peak_transition`: strongest interaction transition in the sequence
- `stable_frame_score`: lower is better when picking before/after snapshots

## Current Threshold Intent

- Low `changed_ratio` means `no_change`.
- Negative `mean_delta` after a meaningful change suggests `put_in`.
- Positive `mean_delta` after a meaningful change suggests `take_out`.
- Medium change with weak sign evidence becomes `partial_take_out_candidate`.

These thresholds are intentionally simple and are expected to be tuned against:

- hand interference
- short occlusion
- organizing food without inventory change
- low-light scenes

## Inputs and Outputs

### Input

- Stage-1 target input: local video file
- Debug fallback: local frame directory

### Output

- `data/keyframes/*_before.jpg`
- `data/keyframes/*_after.jpg`
- `data/outputs/*_diff.jpg`
- `data/outputs/*_event.json`

If the build is done without OpenCV, debug images fall back to `.pgm` files while the event JSON contract stays the same.

## Known TODOs

- TODO: connect a real classifier model after event detection is stable.
- TODO: replace file input with live camera stream input.
- TODO: add embedded-side video decoding and runtime adapters.
