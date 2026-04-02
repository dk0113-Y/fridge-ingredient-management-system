#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../../.." && pwd)
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/cpp/build}"
BIN="${BIN:-$BUILD_DIR/fridge_module12_realtime_live}"
OUTPUT_ROOT="${OUTPUT_ROOT:-$REPO_ROOT/data/test_sessions/module12_realtime_live}"
PID_FILE="$OUTPUT_ROOT/live_test.pid"
LOG_FILE="$OUTPUT_ROOT/live_preview.log"

DEVICE="/dev/video0"
BIND_HOST="0.0.0.0"
PUBLIC_HOST="auto"
PORT="8080"
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
    *)
      EXTRA_ARGS+=("$1")
      shift
      ;;
  esac
done

mkdir -p "$OUTPUT_ROOT"

if [[ ! -x "$BIN" ]]; then
  echo "binary not found: $BIN"
  echo "build first: cmake -S \"$REPO_ROOT/cpp\" -B \"$BUILD_DIR\" && cmake --build \"$BUILD_DIR\""
  exit 1
fi

if [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
  echo "live preview already running with pid $(cat "$PID_FILE")"
  exit 1
fi

PUBLIC_URL_HOST=$(resolve_public_host)

nohup "$BIN" \
  --device "$DEVICE" \
  --bind-host "$BIND_HOST" \
  --public-host "$PUBLIC_HOST" \
  --port "$PORT" \
  --preview-only \
  --stop-after-events 0 \
  "${EXTRA_ARGS[@]}" \
  >"$LOG_FILE" 2>&1 &

echo $! > "$PID_FILE"

echo "Live preview started"
echo "Device: $DEVICE"
echo "Bind host: $BIND_HOST"
echo "Public host: $PUBLIC_URL_HOST"
echo "Browser: http://$PUBLIC_URL_HOST:$PORT/"
echo "MJPEG: http://$PUBLIC_URL_HOST:$PORT/live.mjpeg"
echo "Status: http://$PUBLIC_URL_HOST:$PORT/status.json"
echo "Latest event: http://$PUBLIC_URL_HOST:$PORT/latest_event.json"
echo "PID file: $PID_FILE"
echo "Log file: $LOG_FILE"
