#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/bin/spotify-matrix"
ENV_FILE="${ROOT_DIR}/.env"

if [[ ! -x "${BIN}" ]]; then
  echo "Executable not found at ${BIN}" >&2
  echo "Run ./setup.sh first." >&2
  exit 1
fi

if [[ -f "${ENV_FILE}" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "${ENV_FILE}"
  set +a
fi

exec sudo -E "${BIN}" \
  --rows 64 \
  --cols 64 \
  --chain-length 1 \
  --parallel 1 \
  --gpio-slowdown 4 \
  --no-hardware-pulse \
  --hardware-mapping adafruit-hat \
  "$@"
