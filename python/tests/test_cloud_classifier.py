from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from cloud_classifier import MockRecognizer, RecognizerConfig, XiaomiMimoRecognizer, load_recognizer_config


def test_mock_recognizer_returns_unknown_when_candidates_are_empty():
    recognizer = MockRecognizer()

    result = recognizer.recognize_crop(
        image_path="crops/anything.jpg",
        coarse_class="drink",
        candidate_labels=[],
    )

    assert result == {
        "name": "unknown",
        "category": "drink",
        "confidence": 0.0,
        "reason": "No candidate labels were provided to the mock recognizer.",
        "is_unknown": True,
        "provider": "mock",
    }


def test_mock_recognizer_uses_inventory_context_preferred_label():
    recognizer = MockRecognizer()

    result = recognizer.recognize_crop(
        image_path="crops/random.jpg",
        coarse_class="drink",
        candidate_labels=["coca_cola", "sprite", "fanta"],
        inventory_context={"preferred_label": "sprite"},
    )

    assert result["name"] == "sprite"
    assert result["category"] == "drink"
    assert result["confidence"] == pytest.approx(0.96)
    assert result["is_unknown"] is False
    assert result["provider"] == "mock"
    call_log = recognizer.get_last_call_log()
    assert call_log is not None
    assert call_log["provider"] == "mock"
    assert call_log["coarse_class"] == "drink"
    assert call_log["candidate_count"] == 3
    assert call_log["success"] is True
    assert call_log["parse_success"] is True
    assert call_log["request_id"] != ""


def test_mock_recognizer_uses_image_path_filename_hint():
    recognizer = MockRecognizer()

    result = recognizer.recognize_crop(
        image_path="crops/coca_cola_01.jpg",
        coarse_class="drink",
        candidate_labels=["sprite", "coca_cola", "fanta"],
    )

    assert result["name"] == "coca_cola"
    assert result["reason"] == "Matched candidate label from the image_path filename hint."


def test_mock_recognizer_falls_back_to_first_candidate_for_known_coarse_class():
    recognizer = MockRecognizer()

    result = recognizer.recognize_crop(
        image_path="crops/unknown_crop.jpg",
        coarse_class="fruit",
        candidate_labels=["apple", "pear"],
    )

    assert result["name"] == "apple"
    assert result["category"] == "fruit"
    assert result["confidence"] == pytest.approx(0.78)
    assert result["is_unknown"] is False


def test_xiaomi_mimo_recognizer_is_placeholder():
    recognizer = XiaomiMimoRecognizer()

    with pytest.raises(NotImplementedError, match="placeholder"):
        recognizer.recognize_crop(
            image_path="crops/example.jpg",
            coarse_class="drink",
            candidate_labels=["sprite"],
        )

    call_log = recognizer.get_last_call_log()
    assert call_log is not None
    assert call_log["provider"] == "xiaomi_mimo"
    assert call_log["success"] is False
    assert call_log["parse_success"] is False


def test_recognizer_config_loader_supports_new_fields(tmp_path):
    config_path = tmp_path / "cloud_classifier.json"
    config_path.write_text(
        """
        {
          "provider": "mock",
          "endpoint": "https://example.invalid/fine-grained",
          "api_key": "secret-token",
          "model_name": "generic-fg-v2",
          "timeout_ms": 2200,
          "max_retries": 3,
          "enable_base64_image": true
        }
        """.strip(),
        encoding="utf-8",
    )

    config = load_recognizer_config(config_path)

    assert config == RecognizerConfig(
        provider="mock",
        endpoint="https://example.invalid/fine-grained",
        api_key="secret-token",
        model_name="generic-fg-v2",
        timeout_ms=2200,
        max_retries=3,
        enable_base64_image=True,
    )
    assert "api_key" not in config.sanitized_dict()
