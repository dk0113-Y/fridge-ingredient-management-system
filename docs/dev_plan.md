# Stage-1 Development Plan

## Objective

Ship a local demo that proves the end-to-end software loop before hardware migration.

## Milestones

1. Finish the C++ event pipeline with deterministic debug outputs.
2. Freeze `shared/event_schema.json`.
3. Finish Python ingest, SQLite update logic, and local page.
4. Cover the main competition test scenes with unit tests and sample cases.
5. Validate the manual confirmation loop for ambiguous events.

## Task Split

### Member A

- Finish `cpp/` modules
- Tune event thresholds
- Produce keyframe and diff outputs
- Expand tests for hand interference, occlusion, and low-light scenes

### Member B

- Finish `python/` API, DB, services, and page
- Keep ingest idempotent
- Support pending confirmation and manual correction
- Add backend tests around event-driven inventory updates

## Risk List

- Video decoding dependency on the local toolchain
- Event threshold instability in low-light videos
- Ambiguous partial-take cases without a classifier
- Inventory drift if manual correction flow is skipped
