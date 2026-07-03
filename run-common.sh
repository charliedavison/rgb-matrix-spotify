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
  echo "${ROOT_DIR}/.cache/rgb-spotify/spotify_token.json"
}

ensure_token_cache_dir() {
  local cache_dir
  cache_dir="$(dirname "$(token_cache_path)")"
  mkdir -p "${cache_dir}"

  if [[ "$(id -u)" -eq 0 ]]; then
    chown -R "${RGB_SPOTIFY_UID}:${RGB_SPOTIFY_GID}" "${ROOT_DIR}/.cache"
    chmod -R u+rwX "${ROOT_DIR}/.cache"
  fi
}

migrate_token_cache() {
  TOKEN_CACHE="$(token_cache_path)"
  OLD_TOKEN="${ROOT_DIR}/.cache/spotify_token.json"
  HOME_TOKEN="$(real_user_home)/.cache/rgb-spotify/spotify_token.json"

  if [[ -f "${HOME_TOKEN}" && ! -f "${TOKEN_CACHE}" ]]; then
    cp "${HOME_TOKEN}" "${TOKEN_CACHE}"
  fi
  if [[ -f "${OLD_TOKEN}" && ! -f "${TOKEN_CACHE}" ]]; then
    cp "${OLD_TOKEN}" "${TOKEN_CACHE}"
  fi

  if [[ "$(id -u)" -eq 0 && -f "${TOKEN_CACHE}" ]]; then
    chown "${RGB_SPOTIFY_UID}:${RGB_SPOTIFY_GID}" "${TOKEN_CACHE}"
  fi
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

capture_runtime_user() {
  if [[ -n "${SUDO_USER:-}" ]]; then
    RGB_SPOTIFY_USER="${SUDO_USER}"
  else
    RGB_SPOTIFY_USER="${USER}"
  fi

  RGB_SPOTIFY_UID="$(getent passwd "${RGB_SPOTIFY_USER}" | cut -d: -f3)"
  RGB_SPOTIFY_GID="$(getent passwd "${RGB_SPOTIFY_USER}" | cut -d: -f4)"
  export RGB_SPOTIFY_USER RGB_SPOTIFY_UID RGB_SPOTIFY_GID
}

prepare_runtime() {
  capture_runtime_user

  TOKEN_CACHE="$(token_cache_path)"
  export TOKEN_CACHE RGB_SPOTIFY_TOKEN_CACHE="${TOKEN_CACHE}"

  ensure_token_cache_dir
  migrate_token_cache

  if [[ -f "${ENV_FILE}" ]]; then
    set -a
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    set +a
  fi

  cd "${ROOT_DIR}"
}

# Run the matrix binary as root for GPIO, passing the real user for token cache I/O.
# Works whether invoked as ./run.sh or sudo ./run.sh (avoids nested sudo losing SUDO_UID).
exec_matrix() {
  if [[ "$(id -u)" -eq 0 ]]; then
    exec env \
      "RGB_SPOTIFY_TOKEN_CACHE=${TOKEN_CACHE}" \
      "RGB_SPOTIFY_USER=${RGB_SPOTIFY_USER}" \
      "RGB_SPOTIFY_UID=${RGB_SPOTIFY_UID}" \
      "RGB_SPOTIFY_GID=${RGB_SPOTIFY_GID}" \
      "${EXEC}" "$@"
  else
    exec sudo -E env \
      "RGB_SPOTIFY_TOKEN_CACHE=${TOKEN_CACHE}" \
      "RGB_SPOTIFY_USER=${RGB_SPOTIFY_USER}" \
      "RGB_SPOTIFY_UID=${RGB_SPOTIFY_UID}" \
      "RGB_SPOTIFY_GID=${RGB_SPOTIFY_GID}" \
      "${EXEC}" "$@"
  fi
}
