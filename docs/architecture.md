# Stage-1 Architecture

## Closed Loop

The current milestone implements a local closed loop:

1. `cpp/` reads a local video file or a frame sequence.
2. The vision pipeline finds the main fridge interaction window.
3. The event detector exports `before`, `after`, `diff`, and `event.json`.
4. `python/` scans `data/outputs/*_event.json`.
5. The backend stores the event in SQLite.
6. Inventory state is updated according to the event type.
7. A local web page shows inventory, recent events, pending confirmations, and manual correction forms.

## Module Split

### C/C++ vision side

- `video_io`: local file input abstraction
- `roi_motion`: frame-to-frame ROI motion summary
- `frame_selector`: stable before/after keyframe selection
- `event_detector`: stage-1 event state machine
- `shared/event_schema.json`: exported protocol contract

### Python backend side

- `app/db`: SQLite connection and schema bootstrap
- `app/schemas`: event payload validation and parsing
- `app/services`: event ingest and inventory update logic
- `app/api`: HTTP routes and local page rendering
- `app/models`: small domain entities for readability

## Stage-1 Trade-offs

- Real category classification is intentionally stubbed.
- Video ingestion is designed for OpenCV-first builds, with clear TODOs for board-side migration.
- `partial_take_out_candidate` is kept separate so the demo can show the user confirmation loop early.

## Next Expansion Points

- Replace the null classifier with a lightweight deployed model.
- Replace local file polling with camera stream ingestion.
- Add richer event fusion for hand interference and low-light robustness.
- Move the protocol and thresholds into versioned configs when the pipeline stabilizes.
