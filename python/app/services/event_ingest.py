import json
from dataclasses import asdict
from pathlib import Path

from ..category_mapping import normalize_category
from ..db.database import get_connection
from ..schemas.event_schema import EventPayload, load_event_payload


def sync_event_directory(db_path, event_dir):
    imported = 0
    for event_file in sorted(Path(event_dir).rglob("*event.json")):
        imported += int(ingest_event_file(db_path, event_file))
    return imported


def ingest_event_file(db_path, event_file):
    payload = load_event_payload(event_file)

    with get_connection(db_path) as connection:
        existing = connection.execute(
            "SELECT id FROM events WHERE session_id = ?",
            (payload.session_id,),
        ).fetchone()
        if existing:
            return False

        raw_json = json.dumps(asdict(payload), ensure_ascii=False, indent=2)
        cursor = connection.execute(
            """
            INSERT INTO events (
                session_id, timestamp, event_type, roi_id, confidence,
                before_frame, after_frame, need_user_confirm, raw_json, source_file
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                payload.session_id,
                payload.timestamp,
                payload.event_type,
                payload.roi_id,
                payload.confidence,
                payload.before_frame,
                payload.after_frame,
                int(payload.need_user_confirm),
                raw_json,
                str(event_file),
            ),
        )
        event_id = cursor.lastrowid

        apply_inventory_effect(connection, payload)

        if payload.event_type == "partial_take_out_candidate" or payload.need_user_confirm:
            primary_object = payload.objects[0]
            connection.execute(
                """
                INSERT INTO pending_confirmations (
                    event_id, session_id, status, item_name, category, remain_level, note
                )
                VALUES (?, ?, 'pending', ?, ?, ?, ?)
                """,
                (
                    event_id,
                    payload.session_id,
                    primary_object.name,
                    normalize_category(primary_object.category),
                    primary_object.remain_level,
                    "Awaiting manual confirmation.",
                ),
            )

        connection.commit()
    return True


def apply_inventory_effect(connection, payload: EventPayload):
    primary_object = payload.objects[0]

    if payload.event_type == "put_in":
        upsert_inventory_item(
            connection,
            primary_object.name,
            primary_object.category,
            count_delta=1,
            remain_level=max(primary_object.remain_level, 1.0),
        )
    elif payload.event_type == "take_out":
        upsert_inventory_item(
            connection,
            primary_object.name,
            primary_object.category,
            count_delta=-1,
            remain_level=primary_object.remain_level,
        )
    elif payload.event_type == "partial_take_out_candidate":
        if primary_object.name != "unknown":
            upsert_inventory_item(
                connection,
                primary_object.name,
                primary_object.category,
                count_delta=0,
                remain_level=primary_object.remain_level,
            )
    elif payload.event_type == "no_change":
        return


def upsert_inventory_item(connection, name, category, count_delta=0, remain_level=None, inventory_id=None):
    name = name or "unknown"
    category = normalize_category(category)
    remain_level = _normalize_remain_level(remain_level)

    row = None
    if inventory_id is not None:
        row = connection.execute(
            """
            SELECT id, name, category, count, remain_level
            FROM inventory_items
            WHERE id = ?
            """,
            (inventory_id,),
        ).fetchone()

    if row is None and remain_level is not None:
        row = connection.execute(
            """
            SELECT id, name, category, count, remain_level
            FROM inventory_items
            WHERE name = ? AND category = ? AND remain_level = ?
            """,
            (name, category, remain_level),
        ).fetchone()

    if row is None and count_delta <= 0:
        row = connection.execute(
            """
            SELECT id, name, category, count, remain_level
            FROM inventory_items
            WHERE name = ? AND category = ?
            ORDER BY updated_at DESC, id DESC
            LIMIT 1
            """,
            (name, category),
        ).fetchone()

    if row is None:
        if count_delta <= 0:
            return {"changed": False}

        cursor = connection.execute(
            """
            INSERT INTO inventory_items (name, category, count, remain_level)
            VALUES (?, ?, ?, ?)
            """,
            (name, category, max(count_delta, 0), 0.0 if remain_level is None else remain_level),
        )
        return {
            "changed": True,
            "result_id": cursor.lastrowid,
            "result_name": name,
            "result_category": category,
            "result_count": max(count_delta, 0),
            "result_remain_level": 0.0 if remain_level is None else remain_level,
        }

    next_name = name
    next_category = category
    next_count = max(row["count"] + count_delta, 0)
    next_remain = row["remain_level"] if remain_level is None else remain_level

    merge_row = connection.execute(
        """
        SELECT id, count
        FROM inventory_items
        WHERE id != ? AND name = ? AND category = ? AND remain_level = ?
        """,
        (row["id"], next_name, next_category, next_remain),
    ).fetchone()

    if merge_row is not None:
        merged_count = merge_row["count"] + next_count
        connection.execute(
            """
            UPDATE inventory_items
            SET count = ?, updated_at = CURRENT_TIMESTAMP
            WHERE id = ?
            """,
            (merged_count, merge_row["id"]),
        )
        connection.execute(
            "DELETE FROM inventory_items WHERE id = ?",
            (row["id"],),
        )
        return {
            "changed": True,
            "result_id": merge_row["id"],
            "result_name": next_name,
            "result_category": next_category,
            "result_count": merged_count,
            "result_remain_level": next_remain,
        }

    semantic_changed = (
        next_name != row["name"]
        or next_category != row["category"]
        or next_count != row["count"]
        or next_remain != row["remain_level"]
    )

    if semantic_changed:
        connection.execute(
            """
            UPDATE inventory_items
            SET name = ?, category = ?, count = ?, remain_level = ?, updated_at = CURRENT_TIMESTAMP
            WHERE id = ?
            """,
            (next_name, next_category, next_count, next_remain, row["id"]),
        )

    return {
        "changed": semantic_changed,
        "result_id": row["id"],
        "result_name": next_name,
        "result_category": next_category,
        "result_count": next_count,
        "result_remain_level": next_remain,
    }


def _normalize_remain_level(remain_level):
    if remain_level is None:
        return None

    return round(float(remain_level), 4)
