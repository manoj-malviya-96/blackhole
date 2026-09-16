#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

if [[ "$(uname)" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
    if [[ -n "${QT_PREFIX}" ]]; then
        export CMAKE_PREFIX_PATH="${QT_PREFIX}:${CMAKE_PREFIX_PATH:-}"
        export PATH="${QT_PREFIX}/bin:${PATH}"
    fi
fi

PRESET="${1:-debug}"
echo "==> Configuring with preset '${PRESET}'..."
cmake --preset "${PRESET}"

echo "==> Building with preset '${PRESET}'..."
cmake --build --preset "${PRESET}"

BUILD_DIR="build/${PRESET}"
APP_BUNDLE="${BUILD_DIR}/blackhole_exe.app"
BIN="${BUILD_DIR}/blackhole_exe"

echo "==> Launching application..."
if [[ "$(uname)" == "Darwin" && -d "${APP_BUNDLE}" ]]; then
    open "${APP_BUNDLE}"
elif [[ -x "${APP_BUNDLE}/Contents/MacOS/blackhole_exe" ]]; then
    open "${APP_BUNDLE}"
elif [[ -x "${BIN}" ]]; then
    "${BIN}" "$@"
else
    echo "Error: Could not find executable in ${BUILD_DIR}" >&2
    exit 1
fi
