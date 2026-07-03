#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/run-common.sh"

EXEC="$(find_executable)"
prepare_runtime

exec_matrix \
  --token-cache "${TOKEN_CACHE}" \
  --poll-seconds 3 \
  --fps 15 \
  --rows 64 \
  --cols 64 \
  --chain-length 1 \
  --parallel 1 \
  --gpio-slowdown 4 \
  --pwm-bits 8 \
  --scan-mode 1 \
  --limit-refresh-rate-hz 90 \
  --no-busy-waiting \
  --no-hardware-pulse \
  --hardware-mapping adafruit-hat \
  --idle-off-minutes 30 \
  --night-start 23:00 \
  --night-end 07:00 \
  --night-brightness 12 \
  "$@"
