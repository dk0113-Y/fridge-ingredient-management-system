import json
from datetime import datetime, timezone
from uuid import uuid4

from ..category_mapping import normalize_category
from ..db.database import get_connection
from .event_ingest import sync_event_directory, upsert_inventory_item


class InventoryService:
    def __init__(self, db_path, event_dir):
        self.db_path = db_path
        self.event_dir = event_dir

    def sync(self):
        return sync_event_directory(self.db_path, self.event_dir)

    def health(self):
        self.sync()
        with get_connection(self.db_path) as connection:
            inventory_count = connection.execute("SELECT COUNT(*) AS total FROM inventory_items").fetchone()["total"]
            event_count = connection.execute("SELECT COUNT(*) AS total FROM events").fetchone()["total"]
            pending_count = connection.execute(
                "SELECT COUNT(*) AS total FROM pending_confirmations WHERE status = 'pending'"
            ).fetchone()["total"]
        return {
            "status": "ok",
            "inventory_items": inventory_count,
            "events": event_count,
            "pending_confirmations": pending_count,
        }

    def list_inventory(self):
        self.sync()
        with get_connection(self.db_path) as connection:
            rows = connection.execute(
                """
                SELECT id, name, category, count, remain_level, updated_at
                FROM inventory_items
                ORDER BY updated_at DESC, id DESC
                """
            ).fetchall()
        return [dict(row) for row in rows]

    def list_events(self):
        self.sync()
        with get_connection(self.db_path) as connection:
            rows = connection.execute(
                """
                SELECT id, session_id, timestamp, event_type, roi_id, confidence,
                       before_frame, after_frame, need_user_confirm, source_file, created_at
                FROM events
                ORDER BY created_at DESC, id DESC
                LIMIT 20
                """
            ).fetchall()
        return [dict(row) for row in rows]

    def list_pending_confirmations(self):
        self.sync()
        with get_connection(self.db_path) as connection:
            rows = connection.execute(
                """
                SELECT id, event_id, session_id, status, item_name, category,
                       remain_level, note, created_at, resolved_at
                FROM pending_confirmations
                WHERE status = 'pending'
                ORDER BY created_at DESC, id DESC
                """
            ).fetchall()
        return [dict(row) for row in rows]

    def confirm(self, payload):
        action = (payload.get("action") or "").strip()
        session_id = (payload.get("session_id") or "").strip()
        item_name = (payload.get("item_name") or "unknown").strip() or "unknown"
        category = normalize_category((payload.get("category") or "other").strip() or "other")
        count_delta = int(payload.get("count_delta", 0) or 0)
        inventory_id_raw = payload.get("inventory_id")
        inventory_id = None if inventory_id_raw in (None, "", "null") else int(inventory_id_raw)
        remain_level_raw = payload.get("remain_level")
        remain_level = None if remain_level_raw in (None, "", "null") else float(remain_level_raw)
        note = (payload.get("note") or "").strip()

        with get_connection(self.db_path) as connection:
            if action == "dismiss":
                connection.execute(
                    """
                    UPDATE pending_confirmations
                    SET status = 'dismissed', note = ?, resolved_at = CURRENT_TIMESTAMP
                    WHERE session_id = ?
                    """,
                    (note or "Dismissed by user.", session_id),
                )
            elif action == "apply_partial":
                upsert_inventory_item(
                    connection,
                    item_name,
                    category,
                    count_delta=0,
                    remain_level=remain_level if remain_level is not None else 0.5,
                )
                connection.execute(
                    """
                    UPDATE pending_confirmations
                    SET status = 'confirmed', item_name = ?, category = ?, remain_level = ?,
                        note = ?, resolved_at = CURRENT_TIMESTAMP
                    WHERE session_id = ?
                    """,
                    (
                        item_name,
                        category,
                        remain_level if remain_level is not None else 0.5,
                        note or "Confirmed by user.",
                        session_id,
                    ),
                )
            elif action == "manual_adjust":
                change = upsert_inventory_item(
                    connection,
                    item_name,
                    category,
                    count_delta=count_delta,
                    remain_level=remain_level,
                    inventory_id=inventory_id,
                )
                self._record_manual_event(
                    connection,
                    item_name=item_name,
                    category=category,
                    count_delta=count_delta,
                    remain_level=remain_level,
                    inventory_id=inventory_id,
                    note=note,
                    change=change,
                )
            else:
                raise ValueError(f"Unsupported action: {action}")

            connection.commit()

        return {
            "status": "ok",
            "action": action,
            "session_id": session_id,
        }

    def _record_manual_event(self, connection, item_name, category, count_delta, remain_level, inventory_id, note, change):
        if not change or not change.get("changed"):
            return

        event_type = self._classify_manual_event(inventory_id, change)
        timestamp = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
        session_id = f"{event_type}_{item_name}_{uuid4().hex[:8]}"
        raw_json = json.dumps(
            {
                "source": "manual_adjust",
                "event_type": event_type,
                "item_name": item_name,
                "category": category,
                "count_delta": count_delta,
                "remain_level": remain_level,
                "inventory_id": inventory_id,
                "note": note,
                "result": change,
            },
            ensure_ascii=False,
        )

        connection.execute(
            """
            INSERT INTO events (
                session_id, timestamp, event_type, roi_id, confidence,
                before_frame, after_frame, need_user_confirm, raw_json, source_file
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                session_id,
                timestamp,
                event_type,
                "manual_adjust",
                1.0,
                "",
                "",
                0,
                raw_json,
                "manual://mini-program",
            ),
        )

    def _classify_manual_event(self, inventory_id, change):
        if inventory_id is None:
            return "manual_add"

        if change.get("result_count", 0) == 0:
            return "manual_delete"

        return "manual_edit"
