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

echo "==> Configuring release build..."
cmake --preset release

echo "==> Building release..."
cmake --build --preset release

mkdir -p dist

if [[ "$(uname)" == "Darwin" ]]; then
    APP_BUNDLE="build/release/blackhole_exe.app"
    if command -v macdeployqt >/dev/null 2>&1; then
        echo "==> Running macdeployqt..."
        macdeployqt "${APP_BUNDLE}"
    fi
    echo "==> Packaging macOS bundle into dist/blackhole-macos.zip..."
    (cd build/release && zip -q -r -y "${REPO_ROOT}/dist/blackhole-macos.zip" blackhole_exe.app)
else
    BIN="build/release/blackhole_exe"
    cp "${BIN}" dist/
fi

echo "==> Packaging complete in dist/:"
ls -lh dist/
