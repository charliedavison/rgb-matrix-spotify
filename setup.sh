#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RGBMATRIX_DIR="${ROOT_DIR}/external/rpi-rgb-led-matrix"
RGBMATRIX_REPO="https://github.com/hzeller/rpi-rgb-led-matrix.git"
BUILD_DIR="${ROOT_DIR}/build"
BIN_DIR="${ROOT_DIR}/bin"
ENV_FILE="${ROOT_DIR}/.env"

log() {
  printf '==> %s\n' "$*"
}

warn() {
  printf 'warning: %s\n' "$*" >&2
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "Missing required command: $1"
  fi
}

is_linux() {
  [[ "$(uname -s)" == "Linux" ]]
}

is_raspberry_pi() {
  is_linux && [[ -f /proc/device-tree/model ]] && grep -qi raspberry /proc/device-tree/model
}

build_jobs() {
  local jobs
  jobs="$(nproc 2>/dev/null || echo 1)"
  if is_raspberry_pi && grep -qi "Pi Zero" /proc/device-tree/model 2>/dev/null; then
    jobs=1
    warn "Pi Zero detected; using a single build job to reduce memory use."
  elif [[ "${jobs}" -gt 4 ]]; then
    jobs=4
  fi
  echo "${jobs}"
}

install_packages() {
  if ! is_linux; then
    warn "Not on Linux; skipping apt package installation."
    warn "Install build-essential, cmake, and libcurl development headers manually."
    return
  fi

  if ! command -v apt-get >/dev/null 2>&1; then
    warn "apt-get not found; skipping system package installation."
    return
  fi

  log "Installing system packages"
  sudo apt-get update
  sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    libcurl4-openssl-dev \
    pkg-config
}

ensure_env_file() {
  if [[ -f "${ENV_FILE}" ]]; then
    log "Using existing ${ENV_FILE}"
    return
  fi

  if [[ ! -f "${ROOT_DIR}/.env.example" ]]; then
    die "Missing .env.example"
  fi

  cp "${ROOT_DIR}/.env.example" "${ENV_FILE}"
  log "Created ${ENV_FILE} from .env.example"
}

validate_env() {
  # shellcheck disable=SC1090
  source "${ENV_FILE}"

  if [[ -z "${SPOTIFY_CLIENT_ID:-}" || "${SPOTIFY_CLIENT_ID}" == "your_client_id" ]]; then
    die "Set SPOTIFY_CLIENT_ID in ${ENV_FILE} before running spotify-matrix."
  fi
  if [[ -z "${SPOTIFY_CLIENT_SECRET:-}" || "${SPOTIFY_CLIENT_SECRET}" == "your_client_secret" ]]; then
    die "Set SPOTIFY_CLIENT_SECRET in ${ENV_FILE} before running spotify-matrix."
  fi
  if [[ -z "${SPOTIFY_REDIRECT_URI:-}" ]]; then
    SPOTIFY_REDIRECT_URI="http://127.0.0.1:8888/callback"
  fi

  if [[ "${SPOTIFY_REDIRECT_URI}" != "http://127.0.0.1:8888/callback" ]]; then
    warn "Redirect URI is '${SPOTIFY_REDIRECT_URI}'."
    warn "Make sure the same URI is allowlisted in the Spotify developer dashboard."
  fi
}

clone_rgbmatrix() {
  if [[ -d "${RGBMATRIX_DIR}/.git" ]]; then
    log "RGB matrix library already present at ${RGBMATRIX_DIR}"
    return
  fi

  require_command git
  mkdir -p "${ROOT_DIR}/external"
  log "Cloning rpi-rgb-led-matrix"
  git clone --depth 1 "${RGBMATRIX_REPO}" "${RGBMATRIX_DIR}"
}

build_rgbmatrix() {
  log "Building rpi-rgb-led-matrix"
  make -C "${RGBMATRIX_DIR}" -j"$(build_jobs)" \
    RGBMATRIX_EXTRA_CFLAGS='-std=c++17'

  if [[ ! -f "${RGBMATRIX_DIR}/lib/librgbmatrix.a" ]]; then
    die "Expected ${RGBMATRIX_DIR}/lib/librgbmatrix.a after build"
  fi
}

build_spotify_matrix() {
  require_command cmake
  require_command make

  if [[ -d "${BUILD_DIR}/rpi-rgb-led-matrix" ]]; then
    log "Clearing stale CMake cache from previous build"
    rm -rf "${BUILD_DIR}"
  fi

  log "Configuring spotify-matrix"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DRGBMATRIX_ROOT="${RGBMATRIX_DIR}" \
    -DCMAKE_BUILD_TYPE=Release

  log "Building spotify-matrix"
  cmake --build "${BUILD_DIR}" -j"$(build_jobs)"

  mkdir -p "${BIN_DIR}"
  install -m 755 "${BUILD_DIR}/spotify-matrix" "${BIN_DIR}/spotify-matrix"
  log "Installed executable to ${BIN_DIR}/spotify-matrix"
}

maybe_authorize() {
  local token_cache="${ROOT_DIR}/.cache/spotify_token.json"
  if [[ -f "${token_cache}" ]]; then
    log "Spotify token cache already exists at ${token_cache}"
    return
  fi

  log "No Spotify token cache found."
  cat <<EOF

Next step: authorize Spotify once.

If this Pi is headless, forward port 8888 from your computer first:
  ssh -L 8888:127.0.0.1:8888 pi@raspberrypi.local

Then run:
  ${BIN_DIR}/spotify-matrix --auth-only --token-cache ${ROOT_DIR}/.cache/spotify_token.json

EOF
}

main() {
  cd "${ROOT_DIR}"

  log "Setting up spotify-matrix in ${ROOT_DIR}"
  install_packages
  ensure_env_file
  validate_env
  clone_rgbmatrix
  build_rgbmatrix
  build_spotify_matrix
  maybe_authorize

  cat <<EOF

Setup complete.

Run the matrix display with:
  ${ROOT_DIR}/run.sh

Or directly:
  sudo -E ${BIN_DIR}/spotify-matrix \\
    --rows 64 --cols 64 \\
    --chain-length 1 --parallel 1 \\
    --gpio-slowdown 4 \\
    --no-hardware-pulse \\
    --hardware-mapping adafruit-hat

EOF
}

main "$@"
