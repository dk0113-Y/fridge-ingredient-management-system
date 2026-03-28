import json
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from python.cloud_classifier.cli import main


def test_cli_outputs_json_for_mock_provider(capsys):
    image_path = Path(__file__).resolve().parents[1] / "cloud_classifier" / "examples" / "apple_crop.ppm"

    exit_code = main(
        [
            "--image",
            str(image_path),
            "--coarse-class",
            "fruit",
            "--candidates",
            "apple,orange,lemon,tomato,unknown",
        ]
    )

    captured = capsys.readouterr()
    payload = json.loads(captured.out)

    assert exit_code == 0
    assert payload["name"] == "apple"
    assert payload["category"] == "fruit"
    assert payload["provider"] == "mock"
    assert captured.err == ""


def test_cli_reports_missing_image_path(capsys, tmp_path):
    missing_path = tmp_path / "missing.ppm"

    with pytest.raises(SystemExit) as exc_info:
        main(
            [
                "--image",
                str(missing_path),
                "--coarse-class",
                "fruit",
                "--candidates",
                "apple,orange",
            ]
        )

    captured = capsys.readouterr()

    assert exc_info.value.code == 2
    assert "`--image` does not exist" in captured.err


def test_cli_reports_placeholder_provider_not_implemented(capsys):
    image_path = Path(__file__).resolve().parents[1] / "cloud_classifier" / "examples" / "apple_crop.ppm"

    exit_code = main(
        [
            "--image",
            str(image_path),
            "--coarse-class",
            "fruit",
            "--candidates",
            "apple,orange",
            "--provider",
            "xiaomi_mimo",
        ]
    )

    captured = capsys.readouterr()

    assert exit_code == 1
    assert "Provider 'xiaomi_mimo' is not implemented yet" in captured.err


def test_cli_loads_provider_from_config(capsys, tmp_path):
    image_path = Path(__file__).resolve().parents[1] / "cloud_classifier" / "examples" / "apple_crop.ppm"
    config_path = tmp_path / "cloud_classifier.json"
    config_path.write_text(
        """
        {
          "provider": "mock",
          "endpoint": "https://example.invalid/fine-grained",
          "api_key": "should-not-be-logged",
          "model_name": "generic-fg-v2",
          "timeout_ms": 1500,
          "max_retries": 2,
          "enable_base64_image": false
        }
        """.strip(),
        encoding="utf-8",
    )

    exit_code = main(
        [
            "--image",
            str(image_path),
            "--coarse-class",
            "fruit",
            "--candidates",
            "apple,orange",
            "--config",
            str(config_path),
        ]
    )

    captured = capsys.readouterr()
    payload = json.loads(captured.out)

    assert exit_code == 0
    assert payload["provider"] == "mock"
