# Module 4: Inventory And Rules

This module implements the current inventory and rules baseline for:

- in-memory inventory mutation
- event log records
- pending review queue
- shelf-life rules
- manual correction and confirmation flow

Current implementation status:

- `InventoryEngine` provides in-memory inventory mutation and review flow
- runtime config is loaded from `cpp/configs/module_4_inventory.cfg`
- Module 2 final events can now be mapped into `InventoryEventInput` for an in-memory software closure path; committed and pending results are exposed through Module 5 facade JSON artifacts
- SQLite persistence is a future adapter/storage-layer target, not the current implementation
- a future SQLite adapter can replace or extend the current in-memory storage while keeping the public rule-engine API as stable as possible
