from __future__ import annotations

from abc import ABC, abstractmethod
import json
import logging
import time
from typing import Any, Callable, Mapping, Sequence, TypedDict
import uuid

from .config import RecognizerConfig

_logger = logging.getLogger(__name__)


class RecognitionResult(TypedDict):
    name: str
    category: str
    confidence: float
    reason: str
    is_unknown: bool
    provider: str


class RecognitionCallLog(TypedDict):
    request_id: str
    provider: str
    coarse_class: str
    candidate_count: int
    latency_ms: int
    success: bool
    parse_success: bool


def make_result(
    name: str,
    category: str,
    confidence: float,
    reason: str,
    is_unknown: bool,
    provider: str,
) -> RecognitionResult:
    return {
        "name": name,
        "category": category,
        "confidence": float(confidence),
        "reason": reason,
        "is_unknown": bool(is_unknown),
        "provider": provider,
    }


def make_call_log(
    request_id: str,
    provider: str,
    coarse_class: str,
    candidate_count: int,
    latency_ms: int,
    success: bool,
    parse_success: bool,
) -> RecognitionCallLog:
    return {
        "request_id": request_id,
        "provider": provider,
        "coarse_class": coarse_class,
        "candidate_count": int(candidate_count),
        "latency_ms": int(latency_ms),
        "success": bool(success),
        "parse_success": bool(parse_success),
    }


def validate_result(payload: Mapping[str, Any]) -> RecognitionResult:
    if not isinstance(payload, Mapping):
        raise ValueError("Recognition result must be a mapping.")

    required_keys = ("name", "category", "confidence", "reason", "is_unknown", "provider")
    missing_keys = [key for key in required_keys if key not in payload]
    if missing_keys:
        raise ValueError(f"Recognition result is missing required keys: {', '.join(missing_keys)}")

    return make_result(
        name=str(payload["name"]),
        category=str(payload["category"]),
        confidence=float(payload["confidence"]),
        reason=str(payload["reason"]),
        is_unknown=bool(payload["is_unknown"]),
        provider=str(payload["provider"]),
    )


class FineGrainedRecognizer(ABC):
    def __init__(self, config: RecognizerConfig | None = None) -> None:
        self.config = config or RecognizerConfig()
        self._last_call_log: RecognitionCallLog | None = None

    @abstractmethod
    def recognize_crop(
        self,
        image_path: str,
        coarse_class: str,
        candidate_labels: Sequence[str],
        inventory_context: Mapping[str, Any] | None = None,
    ) -> RecognitionResult:
        """Recognize a crop into one fine-grained label."""

    def get_last_call_log(self) -> RecognitionCallLog | None:
        return self._last_call_log

    def _run_with_call_log(
        self,
        coarse_class: str,
        candidate_labels: Sequence[str],
        impl: Callable[[], Mapping[str, Any]],
    ) -> RecognitionResult:
        request_id = uuid.uuid4().hex
        started_at = time.perf_counter()
        parse_success = False
        success = False
        normalized_coarse_class = str(coarse_class or "").strip() or "unknown"
        candidate_count = sum(1 for label in candidate_labels if str(label or "").strip())

        try:
            result = validate_result(impl())
            parse_success = True
            success = True
            return result
        finally:
            latency_ms = int(round((time.perf_counter() - started_at) * 1000.0))
            self._last_call_log = make_call_log(
                request_id=request_id,
                provider=self.config.provider or "unknown",
                coarse_class=normalized_coarse_class,
                candidate_count=candidate_count,
                latency_ms=latency_ms,
                success=success,
                parse_success=parse_success,
            )
            _logger.info(
                "cloud_classifier_call %s",
                json.dumps(self._last_call_log, ensure_ascii=False, sort_keys=True),
            )
