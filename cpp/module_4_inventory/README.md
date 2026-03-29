# Module 4: Inventory And Rules

This module implements the current inventory and rules baseline for:

- SQLite inventory storage
- event log persistence
- pending review queue
- shelf-life rules
- manual correction and confirmation flow

Current implementation status:

- `InventoryEngine` provides in-memory inventory mutation and review flow
- runtime config is loaded from `cpp/configs/module_4_inventory.cfg`
- a future SQLite adapter can replace or extend the current in-memory storage without changing the public rule-engine API
