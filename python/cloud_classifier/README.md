# Cloud Fine-Grained Recognizer Prototype

`python/cloud_classifier/` is an isolated prototype module for cloud-side fine-grained recognition.

## Scope

This module only takes an already-cropped image and returns one unified JSON-like result:

```python
{
    "name": str,
    "category": str,
    "confidence": float,
    "reason": str,
    "is_unknown": bool,
    "provider": str,
}
```

## Non-Goals

This module does not make inventory decisions. It does not:

- update stock counts
- decide `put_in` or `take_out`
- create pending confirmations
- write to SQLite
- perform manual correction workflows

Those behaviors should stay in the existing inventory and event-processing chain.

## Included Classes

- `FineGrainedRecognizer`: unified abstract interface
- `MockRecognizer`: deterministic local mock for integration testing, with no external API call
- `XiaomiMimoRecognizer`: provider skeleton reserved for later cloud integration

## Shared Config

Provider-neutral config templates live in:

- `configs/cloud_classifier.template.json`
- `configs/cloud_classifier.json`

Supported fields:

```json
{
  "provider": "mock",
  "endpoint": "https://example.invalid/v1/fine-grained-recognize",
  "api_key": "",
  "model_name": "generic-fine-grained-v1",
  "timeout_ms": 15000,
  "max_retries": 1,
  "enable_base64_image": false
}
```

The interface stays provider-neutral. `provider` and `model_name` are configuration fields rather than hardcoded vendor bindings.

## CLI Usage

Run the CLI from the repository root:

```bash
python -m python.cloud_classifier.cli \
  --image python/cloud_classifier/examples/apple_crop.ppm \
  --coarse-class fruit \
  --candidates apple,orange,lemon,tomato,unknown
```

Use a config file explicitly when needed:

```bash
python -m python.cloud_classifier.cli \
  --image python/cloud_classifier/examples/apple_crop.ppm \
  --coarse-class fruit \
  --candidates apple,orange,lemon,tomato,unknown \
  --config configs/cloud_classifier.json
```

`--provider` still overrides the provider field from config.

Supported providers today:

- `mock`: available now
- `xiaomi_mimo`: reserved placeholder, currently returns a clear "not implemented yet" error

CLI behavior:

- Success prints only standard JSON to stdout.
- Input errors such as a missing image path or an empty candidate list are reported clearly on stderr.

## Call Log

Each recognizer stores the last sanitized call log via `get_last_call_log()`. The log fields are:

- `request_id`
- `provider`
- `coarse_class`
- `candidate_count`
- `latency_ms`
- `success`
- `parse_success`

Sensitive fields such as `api_key` are intentionally excluded from logs.

## Minimal Example Image Path

The repository includes a tiny valid sample image for CLI smoke testing:

- `python/cloud_classifier/examples/apple_crop.ppm`

For `MockRecognizer`, the filename stem `apple_crop` also helps demonstrate deterministic label selection.

## Python Example

```python
from cloud_classifier import MockRecognizer

recognizer = MockRecognizer()
result = recognizer.recognize_crop(
    image_path="crops/coca_cola_01.jpg",
    coarse_class="drink",
    candidate_labels=["coca_cola", "sprite", "fanta"],
    inventory_context={"preferred_label": "sprite"},
)

call_log = recognizer.get_last_call_log()
```

The mock recognizer is rule-based and deterministic, so it is suitable for interface alignment and end-to-end integration before a real cloud provider is connected.
