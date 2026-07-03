#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/run-common.sh"

if lsmod 2>/dev/null | grep -q '^snd_bcm2835'; then
  echo "warning: snd_bcm2835 is loaded. Quality mode needs the GPIO 4->18 mod and onboard audio disabled." >&2
  echo "Run: echo 'blacklist snd_bcm2835' | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf && sudo reboot" >&2
fi

EXEC="$(find_executable)"
prepare_runtime

exec sudo -E "${EXEC}" \
  --token-cache "${TOKEN_CACHE}" \
  --poll-seconds 3 \
  --fps 15 \
  --rows 64 \
  --cols 64 \
  --chain-length 1 \
  --parallel 1 \
  --gpio-slowdown 4 \
  --pwm-bits 10 \
  --scan-mode 1 \
  --limit-refresh-rate-hz 100 \
  --no-busy-waiting \
  --hardware-mapping adafruit-hat-pwm \
  "$@"
