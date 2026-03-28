from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any, Mapping


@dataclass(frozen=True)
class RecognizerConfig:
    provider: str = "mock"
    endpoint: str = ""
    api_key: str = ""
    model_name: str = ""
    timeout_ms: int = 15000
    max_retries: int = 0
    enable_base64_image: bool = False

    def sanitized_dict(self) -> dict[str, Any]:
        return {
            "provider": self.provider,
            "endpoint": self.endpoint,
            "model_name": self.model_name,
            "timeout_ms": self.timeout_ms,
            "max_retries": self.max_retries,
            "enable_base64_image": self.enable_base64_image,
        }


def load_recognizer_config(config_path: str | Path) -> RecognizerConfig:
    path = Path(config_path)
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"Recognizer config file does not exist: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"Recognizer config is not valid JSON: {path}: {exc}") from exc

    if not isinstance(payload, Mapping):
        raise ValueError("Recognizer config root must be a JSON object.")

    return recognizer_config_from_mapping(payload)


def recognizer_config_from_mapping(payload: Mapping[str, Any]) -> RecognizerConfig:
    provider = _normalize_provider(payload.get("provider"))
    endpoint = _normalize_string(payload.get("endpoint"))
    api_key = _normalize_string(payload.get("api_key"))
    model_name = _normalize_string(payload.get("model_name"))
    timeout_ms = _parse_positive_int(payload.get("timeout_ms", 15000), "timeout_ms")
    max_retries = _parse_non_negative_int(payload.get("max_retries", 0), "max_retries")
    enable_base64_image = _parse_bool(payload.get("enable_base64_image", False), "enable_base64_image")

    return RecognizerConfig(
        provider=provider,
        endpoint=endpoint,
        api_key=api_key,
        model_name=model_name,
        timeout_ms=timeout_ms,
        max_retries=max_retries,
        enable_base64_image=enable_base64_image,
    )


def _normalize_string(value: Any) -> str:
    if value is None:
        return ""
    return str(value).strip()


def _normalize_provider(value: Any) -> str:
    normalized = _normalize_string(value).lower().replace("-", "_")
    return normalized or "mock"


def _parse_positive_int(value: Any, field_name: str) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"`{field_name}` must be an integer.") from exc

    if parsed <= 0:
        raise ValueError(f"`{field_name}` must be greater than 0.")
    return parsed


def _parse_non_negative_int(value: Any, field_name: str) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"`{field_name}` must be an integer.") from exc

    if parsed < 0:
        raise ValueError(f"`{field_name}` must be greater than or equal to 0.")
    return parsed


def _parse_bool(value: Any, field_name: str) -> bool:
    if isinstance(value, bool):
        return value
    raise ValueError(f"`{field_name}` must be a boolean.")
