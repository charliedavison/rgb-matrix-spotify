#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/bin/spotify-matrix"
BUILD_BIN="${ROOT_DIR}/build/spotify-matrix"
ENV_FILE="${ROOT_DIR}/.env"

if [[ -x "${BIN}" ]]; then
  EXEC="${BIN}"
elif [[ -x "${BUILD_BIN}" ]]; then
  EXEC="${BUILD_BIN}"
else
  echo "Executable not found." >&2
  echo "Expected one of:" >&2
  echo "  ${BIN}" >&2
  echo "  ${BUILD_BIN}" >&2
  echo "Run ./setup.sh or: cmake --build build -j1" >&2
  exit 1
fi

TOKEN_CACHE="${ROOT_DIR}/.cache/spotify_token.json"

if [[ -f "${ENV_FILE}" ]]; then
  set -a
  # shellcheck disable=SC1090
  source "${ENV_FILE}"
  set +a
fi

cd "${ROOT_DIR}"
exec sudo -E "${EXEC}" \
  --token-cache "${TOKEN_CACHE}" \
  --poll-seconds 3 \
  --fps 15 \
  --rows 64 \
  --cols 64 \
  --chain-length 1 \
  --parallel 1 \
  --gpio-slowdown 4 \
  --no-hardware-pulse \
  --hardware-mapping adafruit-hat \
  "$@"
