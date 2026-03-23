import atexit
import json
from pathlib import Path
import shutil
import sqlite3
import sys
import tempfile

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.main import create_app


def write_event(event_dir, session_id, event_type, objects=None, need_user_confirm=False, nested=False):
TEMP_DIRS = []


@atexit.register
def cleanup_temp_dirs():
    for temp_dir in TEMP_DIRS:
        shutil.rmtree(temp_dir, ignore_errors=True)

    payload = {
        "session_id": session_id,
        "timestamp": "2026-03-22T16:00:00Z",
        "event_type": event_type,
        "roi_id": "main_compartment",
        "confidence": 0.92,
        "before_frame": f"data/sessions/{session_id}/before.jpg",
        "after_frame": f"data/sessions/{session_id}/after.jpg",
        "change_regions": [
            {
                "x": 10,
                "y": 12,
                "width": 40,
                "height": 30,
                "score": 0.43,
            }
        ],
        "objects": objects
        or [
            {
                "category": "other",
                "name": "milk",
                "count_delta": 1,
                "remain_level": 1.0,
            }
        ],
        "need_user_confirm": need_user_confirm,
    }
    if nested:
        path = Path(event_dir) / session_id / "event.json"
        path.parent.mkdir(parents=True, exist_ok=True)
    else:
        path = Path(event_dir) / f"{session_id}_event.json"
    path.write_text(json.dumps(payload, ensure_ascii=False), encoding="utf-8")
    return path


def read_pending_confirmation(db_path, session_id):
    with sqlite3.connect(db_path) as connection:
        connection.row_factory = sqlite3.Row
        row = connection.execute(
            """
            SELECT session_id, status, item_name, category, remain_level, note
            FROM pending_confirmations
            WHERE session_id = ?
            """,
            (session_id,),
        ).fetchone()
    return None if row is None else dict(row)


def read_event_count(db_path, session_id):
    with sqlite3.connect(db_path) as connection:
        row = connection.execute(
            "SELECT COUNT(*) FROM events WHERE session_id = ?",
            (session_id,),
        ).fetchone()
    return row[0]


def read_latest_event_types(db_path):
    with sqlite3.connect(db_path) as connection:
        rows = connection.execute(
            "SELECT event_type FROM events ORDER BY id DESC"
        ).fetchall()
    return [row[0] for row in rows]


def find_inventory_item(payload, name, category):
    return next(
        item
        for item in payload["inventory"]
        if item["name"] == name and item["category"] == category
    )


def find_inventory_items(payload, name, category):
    return [
        item
        for item in payload["inventory"]
        if item["name"] == name and item["category"] == category
    ]


@pytest.fixture
def client():
    project_root = Path(__file__).resolve().parents[2]
    temp_path = Path(tempfile.mkdtemp(dir=str(project_root), prefix=".tmp_pytest_"))
    TEMP_DIRS.append(temp_path)

    event_dir = temp_path / "outputs"
    runtime_dir = temp_path / "runtime"
    event_dir.mkdir()
    runtime_dir.mkdir()
    db_path = runtime_dir / "test.sqlite3"

    app = create_app(
        {
            "TESTING": True,
            "EVENT_OUTPUT_DIR": str(event_dir),
            "DATABASE_PATH": str(db_path),
        }
    )
    with app.test_client() as test_client:
        yield test_client, event_dir, db_path


def test_health_endpoint(client):
    test_client, _, _ = client
    response = test_client.get("/health")
    payload = response.get_json()
    assert response.status_code == 200
    assert payload["status"] == "ok"


def test_put_in_updates_inventory(client):
    test_client, event_dir, _ = client
    write_event(event_dir, "session_put_in", "put_in")

    response = test_client.get("/inventory")
    payload = response.get_json()

    assert response.status_code == 200
    assert payload["inventory"][0]["name"] == "milk"
    assert payload["inventory"][0]["count"] == 1


def test_no_change_only_records_event(client):
    test_client, event_dir, _ = client
    write_event(
        event_dir,
        "session_no_change",
        "no_change",
        objects=[
            {
                "category": "other",
                "name": "milk",
                "count_delta": 0,
                "remain_level": 0.0,
            }
        ],
    )

    events_response = test_client.get("/events")
    inventory_response = test_client.get("/inventory")

    assert len(events_response.get_json()["events"]) == 1
    assert inventory_response.get_json()["inventory"] == []


def test_partial_candidate_creates_pending_confirmation(client):
    test_client, event_dir, _ = client
    write_event(
        event_dir,
        "session_partial",
        "partial_take_out_candidate",
        objects=[
            {
                "category": "drink",
                "name": "juice",
                "count_delta": 0,
                "remain_level": 0.4,
            }
        ],
        need_user_confirm=True,
    )

    response = test_client.get("/inventory")
    payload = response.get_json()

    assert response.status_code == 200
    assert len(payload["pending_confirmations"]) == 1
    assert payload["pending_confirmations"][0]["session_id"] == "session_partial"


def test_nested_event_directory_is_supported(client):
    test_client, event_dir = client
    write_event(event_dir, "session_nested", "put_in", nested=True)

    response = test_client.get("/inventory")
    payload = response.get_json()

    assert response.status_code == 200
    assert any(item["name"] == "milk" and item["count"] == 1 for item in payload["inventory"])


def test_take_out_decrements_inventory_without_going_negative(client):
    test_client, event_dir, _ = client
    write_event(event_dir, "session_take_in", "put_in")
    write_event(
        event_dir,
        "session_take_out",
        "take_out",
        objects=[
            {
                "category": "other",
                "name": "milk",
                "count_delta": -1,
                "remain_level": 0.0,
            }
        ],
    )
    write_event(
        event_dir,
        "session_take_out_again",
        "take_out",
        objects=[
            {
                "category": "other",
                "name": "milk",
                "count_delta": -1,
                "remain_level": 0.0,
            }
        ],
    )

    payload = test_client.get("/inventory").get_json()
    milk = find_inventory_item(payload, "milk", "other")

    assert milk["count"] == 0


def test_reimporting_same_session_is_idempotent(client):
    test_client, event_dir, db_path = client
    write_event(event_dir, "session_idempotent", "put_in")

    first_payload = test_client.get("/inventory").get_json()
    second_payload = test_client.get("/inventory").get_json()

    assert first_payload["inventory"][0]["count"] == 1
    assert second_payload["inventory"][0]["count"] == 1
    assert read_event_count(db_path, "session_idempotent") == 1


def test_apply_partial_confirms_pending_and_updates_inventory(client):
    test_client, event_dir, db_path = client
    write_event(
        event_dir,
        "session_juice_put_in",
        "put_in",
        objects=[
            {
                "category": "drink",
                "name": "juice",
                "count_delta": 1,
                "remain_level": 1.0,
            }
        ],
    )
    write_event(
        event_dir,
        "session_apply_partial",
        "partial_take_out_candidate",
        objects=[
            {
                "category": "drink",
                "name": "juice",
                "count_delta": 0,
                "remain_level": 0.4,
            }
        ],
        need_user_confirm=True,
    )

    initial_payload = test_client.get("/inventory").get_json()
    assert len(initial_payload["pending_confirmations"]) == 1

    response = test_client.post(
        "/confirm",
        json={
            "action": "apply_partial",
            "session_id": "session_apply_partial",
            "item_name": "juice",
            "category": "drink",
            "remain_level": 0.4,
            "note": "confirmed by test",
        },
    )
    payload = test_client.get("/inventory").get_json()
    pending = read_pending_confirmation(db_path, "session_apply_partial")
    juice = find_inventory_item(payload, "juice", "beverage_dairy")

    assert response.status_code == 200
    assert payload["pending_confirmations"] == []
    assert juice["count"] == 1
    assert juice["remain_level"] == pytest.approx(0.4)
    assert pending["status"] == "confirmed"


def test_dismiss_marks_pending_confirmation_resolved(client):
    test_client, event_dir, db_path = client
    write_event(
        event_dir,
        "session_dismiss_partial",
        "partial_take_out_candidate",
        objects=[
            {
                "category": "other",
                "name": "unknown",
                "count_delta": 0,
                "remain_level": 0.3,
            }
        ],
        need_user_confirm=True,
    )

    initial_payload = test_client.get("/inventory").get_json()
    assert len(initial_payload["pending_confirmations"]) == 1

    response = test_client.post(
        "/confirm",
        json={
            "action": "dismiss",
            "session_id": "session_dismiss_partial",
            "note": "false alarm",
        },
    )
    payload = test_client.get("/inventory").get_json()
    pending = read_pending_confirmation(db_path, "session_dismiss_partial")

    assert response.status_code == 200
    assert payload["pending_confirmations"] == []
    assert payload["inventory"] == []
    assert pending["status"] == "dismissed"


def test_manual_adjust_endpoint(client):
    test_client, _, _ = client
    response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "item_name": "egg",
            "category": "protein",
            "count_delta": 2,
            "remain_level": 1.0,
        },
    )

    inventory_response = test_client.get("/inventory")
    inventory_payload = inventory_response.get_json()
    events_payload = test_client.get("/events").get_json()

    assert response.status_code == 200
    assert any(
        item["name"] == "egg" and item["category"] == "fresh_protein" and item["count"] == 2
        for item in inventory_payload["inventory"]
    )
    assert events_payload["events"][0]["event_type"] == "manual_add"


def test_manual_adjust_splits_same_name_when_remain_differs_and_merges_when_same(client):
    test_client, _, _ = client

    first_response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "item_name": "apple",
            "category": "fruit",
            "count_delta": 1,
            "remain_level": 1.0,
        },
    )
    second_response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "item_name": "apple",
            "category": "fruit",
            "count_delta": 1,
            "remain_level": 0.5,
        },
    )
    third_response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "item_name": "apple",
            "category": "fruit",
            "count_delta": 1,
            "remain_level": 0.5,
        },
    )

    inventory_payload = test_client.get("/inventory").get_json()
    apples = find_inventory_items(inventory_payload, "apple", "produce")
    apple_levels = sorted((item["remain_level"], item["count"]) for item in apples)

    assert first_response.status_code == 200
    assert second_response.status_code == 200
    assert third_response.status_code == 200
    assert apple_levels == [(0.5, 2), (1.0, 1)]


def test_manual_edit_and_delete_create_manual_events(client):
    test_client, _, db_path = client

    create_response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "item_name": "tomato",
            "category": "vegetable",
            "count_delta": 2,
            "remain_level": 1.0,
        },
    )
    inventory_payload = test_client.get("/inventory").get_json()
    tomato = find_inventory_item(inventory_payload, "tomato", "produce")

    edit_response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "inventory_id": tomato["id"],
            "item_name": "tomato",
            "category": "vegetable",
            "count_delta": -1,
            "remain_level": 0.5,
        },
    )
    updated_inventory_payload = test_client.get("/inventory").get_json()
    updated_tomato = find_inventory_item(updated_inventory_payload, "tomato", "produce")

    delete_response = test_client.post(
        "/confirm",
        json={
            "action": "manual_adjust",
            "inventory_id": updated_tomato["id"],
            "item_name": "tomato",
            "category": "vegetable",
            "count_delta": -updated_tomato["count"],
            "remain_level": updated_tomato["remain_level"],
        },
    )

    latest_event_types = read_latest_event_types(db_path)[:3]

    assert create_response.status_code == 200
    assert edit_response.status_code == 200
    assert delete_response.status_code == 200
    assert latest_event_types == ["manual_delete", "manual_edit", "manual_add"]
