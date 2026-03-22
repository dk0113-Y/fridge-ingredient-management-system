# Task Split

## Objective

Build the stage-1 software-first closed loop for the competition project:

1. Read a local video on the C/C++ side.
2. Detect a real fridge interaction event.
3. Export structured `event.json`.
4. Ingest the event on the Python side.
5. Update SQLite inventory state.
6. Show inventory, recent events, pending confirmations, and manual correction in a local web page.

## Member A Tasks

### Scope

Member A owns the C/C++ vision mainline under `cpp/`.

### Responsibilities

- Maintain `cpp/CMakeLists.txt` and keep the stage-1 target buildable.
- Implement and refine:
  - `video_io`
  - `roi_motion`
  - `frame_selector`
  - `event_detector`
  - `main.cpp`
- Keep the stage-1 event output limited to:
  - `no_change`
  - `put_in`
  - `take_out`
  - `partial_take_out_candidate`
- Export:
  - `data/keyframes/*_before.*`
  - `data/keyframes/*_after.*`
  - `data/outputs/*_diff.*`
  - `data/outputs/*_event.json`
- Keep the classifier as an interface-only placeholder.
- Expand tests for:
  - hand interference
  - short occlusion
  - organizing without inventory change
  - low-light scenes
- Keep clear TODOs for:
  - later classifier integration
  - later camera-stream input
  - later board-side migration

### Deliverables

- Runnable `fridge_vision_demo`
- Stable `event.json` export matching `shared/event_schema.json`
- C++ test cases and updated pipeline notes

## Member B Tasks

### Scope

Member B owns the Python backend and inventory loop under `python/`.

### Responsibilities

- Maintain `python/requirements.txt`.
- Implement and refine:
  - `app/main.py`
  - `app/api`
  - `app/services`
  - `app/models`
  - `app/db`
  - `app/schemas`
- Keep SQLite schema aligned with stage-1 needs:
  - `inventory_items`
  - `events`
  - `pending_confirmations`
- Read `data/outputs/*_event.json` and make ingestion idempotent.
- Update inventory rules:
  - `put_in` -> `count + 1`
  - `take_out` -> `count - 1`
  - `partial_take_out_candidate` -> update remain level and/or create pending confirmation
  - `no_change` -> event log only
- Provide and maintain:
  - `GET /health`
  - `GET /events`
  - `GET /inventory`
  - `POST /confirm`
- Keep the local page usable for demo:
  - current inventory
  - recent events
  - pending confirmations
  - manual correction
- Add backend tests around ingest, inventory update, and confirmation flow.

### Deliverables

- Runnable Flask backend
- SQLite database bootstrap
- Working local demo page
- Passing backend tests

## Shared Tasks

### Shared Protocol

- Maintain `shared/event_schema.json` as the single contract between C++ and Python.
- Any schema change must be reflected on both sides in the same branch or same PR.

### Shared Documents

- Keep the following docs in sync with the implementation:
  - `docs/architecture.md`
  - `docs/vision_pipeline.md`
  - `docs/backend_api.md`
  - `docs/db_design.md`
  - `docs/dev_plan.md`
  - `docs/task_split.md`

### Integration

- Use `dev` as the integration branch.
- Develop features in `feature/*` branches.
- Run a local end-to-end check before merging:
  1. C++ generates `event.json`
  2. Python imports it
  3. SQLite updates correctly
  4. Web page reflects the change

### Coordination Rules

- Do not change the event type naming casually.
- Do not bypass `shared/event_schema.json`.
- Keep generated files out of Git except tracked placeholders.
- Record any threshold changes or inventory rule changes in docs.

## Milestones

### M1: Closed-Loop Skeleton

- C++ can export a valid event JSON.
- Python can ingest JSON and write SQLite.
- Web page can show inventory and recent events.

### M2: Competition-Oriented Robustness

- C++ tests cover hand interference, occlusion, no-change organizing, and low light.
- Python handles ambiguous events through pending confirmations.
- End-to-end local demo is repeatable.

### M3: Pre-Hardware Handoff

- Interfaces are frozen enough for board-side migration.
- TODO markers for classifier, camera stream, and embedded runtime are explicit.
- Documents are updated for the next implementation phase.

## Acceptance Criteria

### Vision Side

- Given a valid local input source, the C++ demo produces keyframes, diff output, and `event.json`.
- The exported event type is one of the four stage-1 values.
- The exported JSON matches the shared schema fields.

### Backend Side

- The backend imports unseen event JSON files without duplicate inserts.
- Inventory updates follow the defined stage-1 rules.
- Pending confirmations are created for ambiguous partial events.
- Manual correction can update inventory state from the local page or API.

### End-to-End Demo

- A generated `put_in` event increases inventory.
- A generated `take_out` event decreases inventory.
- A generated `partial_take_out_candidate` event appears in pending confirmation.
- A generated `no_change` event is logged but does not change stock.

### Documentation and Collaboration

- The task split is clear enough that A and B can work in parallel.
- The branch workflow remains `main` / `dev` / `feature/*`.
- Changes merged to `dev` are reproducible locally by the other teammate.
