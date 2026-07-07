#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${ROOT_DIR}/run-common.sh"

EXEC="$(find_executable)"
prepare_runtime
TOKEN_CACHE="$(token_cache_path)"

exec "${EXEC}" \
  --simulate \
  --token-cache "${TOKEN_CACHE}" \
  --poll-seconds 3 \
  --fps 15 \
  --rows 64 \
  --cols 64 \
  --idle-off-minutes 0 \
  "$@"
