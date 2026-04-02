#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../../.." && pwd)
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/cpp/build}"
BIN="${BIN:-$BUILD_DIR/fridge_module12_realtime_live}"
OUTPUT_ROOT="${OUTPUT_ROOT:-$REPO_ROOT/data/test_sessions/module12_realtime_live}"

DEVICE="/dev/video0"
CASE_ID="TC02_live_put_in"
MODULE2_MODE="mock"
ROI=""
BIND_HOST="0.0.0.0"
PUBLIC_HOST="auto"
PORT="8080"
STOP_AFTER_EVENTS="1"
CAPTURE_ONLY="0"
EXTRA_ARGS=()

resolve_public_host() {
  if [[ "$PUBLIC_HOST" != "auto" ]]; then
    printf '%s\n' "$PUBLIC_HOST"
    return
  fi

  local detected
  detected=$(hostname -I 2>/dev/null | awk '{print $1}')
  if [[ -n "$detected" ]]; then
    printf '%s\n' "$detected"
  else
    printf '127.0.0.1\n'
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device)
      DEVICE="$2"
      shift 2
      ;;
    --case-id)
      CASE_ID="$2"
      shift 2
      ;;
    --module2-mode)
      MODULE2_MODE="$2"
      shift 2
      ;;
    --roi)
      ROI="$2"
      shift 2
      ;;
    --bind-host)
      BIND_HOST="$2"
      shift 2
      ;;
    --public-host)
      PUBLIC_HOST="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --stop-after-events)
      STOP_AFTER_EVENTS="$2"
      shift 2
      ;;
    --capture-only)
      CAPTURE_ONLY="1"
      shift
      ;;
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

if [[ ! -x "$BIN" ]]; then
  echo "binary not found: $BIN"
  echo "build first: cmake -S \"$REPO_ROOT/cpp\" -B \"$BUILD_DIR\" && cmake --build \"$BUILD_DIR\""
  exit 1
fi

mkdir -p "$OUTPUT_ROOT"
PUBLIC_URL_HOST=$(resolve_public_host)

MOCK_CLASS="packaged_food"
case "$CASE_ID" in
  *TC04*|*partial*|*fruit*)
    MOCK_CLASS="fruit_vegetable"
    ;;
  *drink*)
    MOCK_CLASS="drink"
    ;;
esac

CMD=(
  "$BIN"
  --device "$DEVICE"
  --bind-host "$BIND_HOST"
  --public-host "$PUBLIC_HOST"
  --port "$PORT"
  --case-id "$CASE_ID"
  --module2-mode "$MODULE2_MODE"
  --mock-class "$MOCK_CLASS"
  --stop-after-events "$STOP_AFTER_EVENTS"
)

if [[ -n "$ROI" ]]; then
  CMD+=(--roi "$ROI")
fi

if [[ "$CAPTURE_ONLY" == "1" ]]; then
  CMD+=(--capture-only)
fi

CMD+=("${EXTRA_ARGS[@]}")

echo "Running module12 live test"
echo "Device: $DEVICE"
echo "Case: $CASE_ID"
echo "Module2 mode: $MODULE2_MODE"
echo "Capture only: $CAPTURE_ONLY"
echo "Output root: $OUTPUT_ROOT"
echo "Bind host: $BIND_HOST"
echo "Public host: $PUBLIC_URL_HOST"
echo "Browser: http://$PUBLIC_URL_HOST:$PORT/"
echo "Status: http://$PUBLIC_URL_HOST:$PORT/status.json"

"${CMD[@]}"
