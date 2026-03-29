# C++ Cloud Fine-Grained Recognizer Skeleton

`cpp/module_3_fine_grained/` contains an independent C++ client skeleton for cloud-side fine-grained recognition.

## Scope

This module is intentionally independent from the stage-1 inventory and event chain. It only:

- loads provider settings from a config file
- exposes one unified `FineGrainedRecognizerClient`
- supports a deterministic mock mode for local integration
- reserves an HTTPS JSON request path for future cloud providers

It does not yet update inventory state or connect to the planned C++ inventory and HTTP modules.

## Config

Provider settings live in:

- `cpp/configs/module_3_fine_grained.cfg`

Supported fields:

```cfg
provider = mock
endpoint = https://example.invalid/v1/fine-grained-recognize
api_key =
model_name = generic-fine-grained-v1
timeout_ms = 15000
max_retries = 1
enable_base64_image = false
llm_confidence_threshold = 0.75
prompt_template = Identify the ingredient from the image. Coarse class: {coarse_class}. Candidate labels: {candidate_labels}. Return JSON with keys name, category, confidence, reason, is_unknown, provider.
```

These settings stay outside C++ code so a future provider can be swapped in without hardcoding a specific vendor endpoint, model binding, prompt, or confidence threshold.

## Demo

After building, run:

```powershell
fridge_cloud_classifier_demo.exe `
  --image cpp/module_3_fine_grained/examples/apple_crop.ppm `
  --coarse-class fruit `
  --candidates apple,orange,lemon `
  --config cpp/configs/module_3_fine_grained.cfg
```

The demo prints a JSON `RecognitionResult` to stdout.

## Call Log

Each call stores a sanitized `RecognitionCallLog` accessible through `lastCallLog()`. The log fields are:

- `request_id`
- `provider`
- `coarse_class`
- `candidate_count`
- `latency_ms`
- `success`
- `parse_success`

Sensitive fields such as `api_key` are intentionally excluded from logs.

## Notes

- `mock` mode does not call any real cloud API.
- Non-mock mode sends an HTTPS `POST` with a JSON request body through `libcurl`.
- Response parsing uses `nlohmann/json`.
- The request payload can carry `request_id`, `model_name`, `prompt`, `llm_confidence_threshold`, and optional `image_base64` based on config.
- If `libcurl` development files are unavailable at build time, mock mode still works and remote mode will fail with a clear runtime error.
