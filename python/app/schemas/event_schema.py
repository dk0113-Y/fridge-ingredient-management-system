import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(slots=True)
class ChangeRegion:
    x: int
    y: int
    width: int
    height: int
    score: float


@dataclass(slots=True)
class ObjectPayload:
    category: str
    name: str
    count_delta: int
    remain_level: float


@dataclass(slots=True)
class EventPayload:
    session_id: str
    timestamp: str
    event_type: str
    roi_id: str
    confidence: float
    before_frame: str
    after_frame: str
    change_regions: list[ChangeRegion]
    objects: list[ObjectPayload]
    need_user_confirm: bool


REQUIRED_FIELDS = {
    "session_id",
    "timestamp",
    "event_type",
    "roi_id",
    "confidence",
    "before_frame",
    "after_frame",
    "change_regions",
    "objects",
    "need_user_confirm",
}


def load_event_payload(event_file):
    payload = json.loads(Path(event_file).read_text(encoding="utf-8"))
    missing = REQUIRED_FIELDS - payload.keys()
    if missing:
        raise ValueError(f"Missing required event fields: {sorted(missing)}")

    change_regions = [
        ChangeRegion(
            x=int(region["x"]),
            y=int(region["y"]),
            width=int(region["width"]),
            height=int(region["height"]),
            score=float(region["score"]),
        )
        for region in payload["change_regions"]
    ]

    objects = [
        ObjectPayload(
            category=str(obj["category"]),
            name=str(obj["name"]),
            count_delta=int(obj["count_delta"]),
            remain_level=float(obj["remain_level"]),
        )
        for obj in payload["objects"]
    ]
    if not objects:
        objects = [
            ObjectPayload(
                category="unknown",
                name="unknown",
                count_delta=0,
                remain_level=0.0,
            )
        ]

    return EventPayload(
        session_id=str(payload["session_id"]),
        timestamp=str(payload["timestamp"]),
        event_type=str(payload["event_type"]),
        roi_id=str(payload["roi_id"]),
        confidence=float(payload["confidence"]),
        before_frame=str(payload["before_frame"]),
        after_frame=str(payload["after_frame"]),
        change_regions=change_regions,
        objects=objects,
        need_user_confirm=bool(payload["need_user_confirm"]),
    )
