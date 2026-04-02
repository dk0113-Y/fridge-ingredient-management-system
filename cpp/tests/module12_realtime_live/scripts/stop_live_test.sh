#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../../.." && pwd)
OUTPUT_ROOT="${OUTPUT_ROOT:-$REPO_ROOT/data/test_sessions/module12_realtime_live}"
PID_FILE="$OUTPUT_ROOT/live_test.pid"

if [[ ! -f "$PID_FILE" ]]; then
  echo "no pid file found: $PID_FILE"
  exit 0
fi

PID=$(cat "$PID_FILE")
if kill -0 "$PID" 2>/dev/null; then
  kill "$PID"
  echo "stopped live test pid $PID"
else
  echo "process $PID is not running"
fi

rm -f "$PID_FILE"
