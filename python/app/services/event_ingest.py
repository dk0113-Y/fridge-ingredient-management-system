import json
from dataclasses import asdict
from pathlib import Path

from ..db.database import get_connection
from ..schemas.event_schema import EventPayload, load_event_payload


def sync_event_directory(db_path, event_dir):
    imported = 0
    for event_file in sorted(Path(event_dir).glob("*_event.json")):
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
                    primary_object.category,
                    primary_object.remain_level,
                    "Awaiting user confirmation for partial inventory change.",
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


def upsert_inventory_item(connection, name, category, count_delta=0, remain_level=None):
    name = name or "unknown"
    category = category or "unknown"

    row = connection.execute(
        """
        SELECT id, count, remain_level
        FROM inventory_items
        WHERE name = ? AND category = ?
        """,
        (name, category),
    ).fetchone()

    if row is None:
        initial_count = max(count_delta, 0)
        initial_remain = 0.0 if remain_level is None else remain_level
        connection.execute(
            """
            INSERT INTO inventory_items (name, category, count, remain_level)
            VALUES (?, ?, ?, ?)
            """,
            (name, category, initial_count, initial_remain),
        )
        return

    next_count = max(row["count"] + count_delta, 0)
    next_remain = row["remain_level"] if remain_level is None else remain_level
    connection.execute(
        """
        UPDATE inventory_items
        SET count = ?, remain_level = ?, updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (next_count, next_remain, row["id"]),
    )
