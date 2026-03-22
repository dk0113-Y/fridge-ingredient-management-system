import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.main import create_app


def write_event(event_dir, session_id, event_type, objects=None, need_user_confirm=False, nested=False):
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
                "category": "unknown",
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


@pytest.fixture
def client(tmp_path):
    event_dir = tmp_path / "outputs"
    runtime_dir = tmp_path / "runtime"
    event_dir.mkdir()
    runtime_dir.mkdir()

    app = create_app(
        {
            "TESTING": True,
            "EVENT_OUTPUT_DIR": str(event_dir),
            "DATABASE_PATH": str(runtime_dir / "test.sqlite3"),
        }
    )
    return app.test_client(), event_dir


def test_health_endpoint(client):
    test_client, _ = client
    response = test_client.get("/health")
    payload = response.get_json()
    assert response.status_code == 200
    assert payload["status"] == "ok"


def test_put_in_updates_inventory(client):
    test_client, event_dir = client
    write_event(event_dir, "session_put_in", "put_in")

    response = test_client.get("/inventory")
    payload = response.get_json()

    assert response.status_code == 200
    assert payload["inventory"][0]["name"] == "milk"
    assert payload["inventory"][0]["count"] == 1


def test_no_change_only_records_event(client):
    test_client, event_dir = client
    write_event(
        event_dir,
        "session_no_change",
        "no_change",
        objects=[
            {
                "category": "unknown",
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
    test_client, event_dir = client
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


def test_manual_adjust_endpoint(client):
    test_client, _ = client
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

    assert response.status_code == 200
    assert any(item["name"] == "egg" and item["count"] == 2 for item in inventory_payload["inventory"])
