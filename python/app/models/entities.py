from dataclasses import dataclass


@dataclass(slots=True)
class InventoryItem:
    id: int
    name: str
    category: str
    count: int
    remain_level: float
    updated_at: str


@dataclass(slots=True)
class EventRecord:
    id: int
    session_id: str
    timestamp: str
    event_type: str
    roi_id: str
    confidence: float
    before_frame: str
    after_frame: str
    need_user_confirm: bool
    source_file: str
    created_at: str


@dataclass(slots=True)
class PendingConfirmation:
    id: int
    event_id: int
    session_id: str
    status: str
    item_name: str
    category: str
    remain_level: float | None
    note: str
    created_at: str
    resolved_at: str | None
