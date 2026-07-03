#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/bin/spotify-matrix"
BUILD_BIN="${ROOT_DIR}/build/spotify-matrix"
ENV_FILE="${ROOT_DIR}/.env"

real_user_home() {
  if [[ -n "${SUDO_USER:-}" ]]; then
    getent passwd "${SUDO_USER}" | cut -d: -f6
  else
    echo "${HOME}"
  fi
}

token_cache_path() {
  echo "$(real_user_home)/.cache/rgb-spotify/spotify_token.json"
}

find_executable() {
  if [[ -x "${BIN}" ]]; then
    echo "${BIN}"
  elif [[ -x "${BUILD_BIN}" ]]; then
    echo "${BUILD_BIN}"
  else
    echo "Executable not found." >&2
    echo "Expected one of:" >&2
    echo "  ${BIN}" >&2
    echo "  ${BUILD_BIN}" >&2
    echo "Run ./setup.sh or: cmake --build build -j1" >&2
    return 1
  fi
}

prepare_runtime() {
  TOKEN_CACHE="$(token_cache_path)"
  OLD_TOKEN="${ROOT_DIR}/.cache/spotify_token.json"
  mkdir -p "$(dirname "${TOKEN_CACHE}")"
  if [[ -f "${OLD_TOKEN}" && ! -f "${TOKEN_CACHE}" ]]; then
    cp "${OLD_TOKEN}" "${TOKEN_CACHE}"
  fi

  if [[ -f "${ENV_FILE}" ]]; then
    set -a
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    set +a
  fi

  cd "${ROOT_DIR}"
}
