# Module 4: Inventory And Rules

This module implements the current inventory and rules baseline for:

- in-memory inventory mutation
- event log records
- pending review queue
- shelf-life rules
- manual correction and confirmation flow
- optional SQLite persistence for inventory snapshots

Current implementation status:

- `InventoryEngine` provides in-memory inventory mutation and review flow
- runtime config is loaded from `cpp/configs/module_4_inventory.cfg`
- Module 2 final events can now be mapped into `InventoryEventInput` for an in-memory software closure path; committed and pending results are exposed through Module 5 facade JSON artifacts
- `SQLiteInventoryStore` is an optional persistence adapter when CMake finds sqlite3 development files
- `InventoryEngine` remains the rule engine and source of mutation logic; SQLite saves and restores engine snapshots through persistence-specific snapshot APIs
- SQLite schema initialization covers `inventory`, `event_log`, `pending_review`, and `inventory_change_log`
- `fridge_debug_sqlite_persistence` is built only when SQLite support is available and proves save/load/reload behavior with deterministic debug events
- SQLite persistence is not yet wired into a real HTTP server, mini program integration, or full real-camera/real-ONNX pipeline run

## SQLite Persistence

SQLite support is controlled by `FRIDGE_USE_SQLITE`, which defaults to `ON`. If CMake cannot find sqlite3 headers and library, the SQLite adapter and debug executable are skipped while the in-memory inventory flow still builds and runs.

The baseline adapter uses the sqlite3 C API directly and stores the current in-memory state as a local ledger:

- `inventory`: current item count, category, remain level, expiry, and last update time
- `event_log`: applied or pending event records
- `pending_review`: manual-review queue
- `inventory_change_log`: committed inventory changes

The adapter does not import `event.json`; `event.json` remains debug/evidence output.
