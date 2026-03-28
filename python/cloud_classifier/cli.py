from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .base import FineGrainedRecognizer
from .config import RecognizerConfig, load_recognizer_config
from .mock import MockRecognizer
from .xiaomi_mimo import XiaomiMimoRecognizer


class CliInputError(ValueError):
    """Raised when CLI arguments are syntactically valid but semantically invalid."""


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the cloud fine-grained recognizer prototype on one cropped image.",
        epilog=(
            "Example:\n"
            "  python -m python.cloud_classifier.cli "
            "--image python/cloud_classifier/examples/apple_crop.ppm "
            "--coarse-class fruit "
            "--candidates apple,orange,lemon,tomato,unknown"
        ),
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "--image",
        required=True,
        help="Path to one crop image file.",
    )
    parser.add_argument(
        "--coarse-class",
        required=True,
        dest="coarse_class",
        help="Coarse category from the upstream detector, for example fruit or drink.",
    )
    parser.add_argument(
        "--candidates",
        required=True,
        help="Comma-separated candidate fine-grained labels.",
    )
    parser.add_argument(
        "--provider",
        default=None,
        choices=("mock", "xiaomi_mimo"),
        help="Recognizer provider. Overrides the config file when set.",
    )
    parser.add_argument(
        "--config",
        default=None,
        help="Optional JSON config path. If omitted, configs/cloud_classifier.json is used when available.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    config = RecognizerConfig(provider=args.provider or "mock")

    try:
        image_path = _validate_image_path(args.image)
        coarse_class = _validate_non_empty(args.coarse_class, "--coarse-class")
        candidate_labels = _parse_candidates(args.candidates)
        config = _load_config(args.config, args.provider)
        recognizer = _build_recognizer(config)
        result = recognizer.recognize_crop(
            image_path=image_path,
            coarse_class=coarse_class,
            candidate_labels=candidate_labels,
        )
    except CliInputError as exc:
        parser.error(str(exc))
    except NotImplementedError as exc:
        print(f"Provider '{config.provider}' is not implemented yet: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:  # pragma: no cover - safety net for manual CLI use
        print(f"Recognition failed: {exc}", file=sys.stderr)
        return 1

    json.dump(result, sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")
    return 0


def _validate_image_path(raw_path: str) -> str:
    image_path = Path(_validate_non_empty(raw_path, "--image"))
    if not image_path.exists():
        raise CliInputError(f"`--image` does not exist: {image_path}")
    if not image_path.is_file():
        raise CliInputError(f"`--image` is not a file: {image_path}")
    return str(image_path)


def _validate_non_empty(value: str, argument_name: str) -> str:
    normalized = (value or "").strip()
    if not normalized:
        raise CliInputError(f"`{argument_name}` cannot be empty.")
    return normalized


def _parse_candidates(raw_candidates: str) -> list[str]:
    labels = [label.strip() for label in raw_candidates.split(",")]
    labels = [label for label in labels if label]
    if not labels:
        raise CliInputError(
            "`--candidates` must contain at least one non-empty label, for example: apple,orange,lemon"
        )
    return labels


def _build_recognizer(config: RecognizerConfig) -> FineGrainedRecognizer:
    if config.provider == "mock":
        return MockRecognizer(config=config)
    if config.provider == "xiaomi_mimo":
        return XiaomiMimoRecognizer(config=config)
    raise CliInputError(f"Unsupported provider: {config.provider}")


def _load_config(raw_config_path: str | None, provider_override: str | None) -> RecognizerConfig:
    if raw_config_path is not None:
        config_path = Path(_validate_non_empty(raw_config_path, "--config"))
        if not config_path.exists():
            raise CliInputError(f"`--config` does not exist: {config_path}")
        config = _load_config_file(config_path)
    else:
        default_path = Path("configs/cloud_classifier.json")
        config = _load_config_file(default_path) if default_path.exists() else RecognizerConfig()

    if provider_override is not None:
        config = RecognizerConfig(
            provider=provider_override,
            endpoint=config.endpoint,
            api_key=config.api_key,
            model_name=config.model_name,
            timeout_ms=config.timeout_ms,
            max_retries=config.max_retries,
            enable_base64_image=config.enable_base64_image,
        )
    return config


def _load_config_file(config_path: Path) -> RecognizerConfig:
    try:
        return load_recognizer_config(config_path)
    except ValueError as exc:
        raise CliInputError(str(exc)) from exc


if __name__ == "__main__":
    raise SystemExit(main())
