#!/usr/bin/env bash
# Build.sh — compiles the entire Skaldbok project on Linux (the counterpart of Build.bat):
#   1. Vue web client  (npm run build, skipped if Node is not installed)
#   2. C++ release     (cmake + ninja into build/release)
#
# Usage:
#   ./Build.sh           — full release build
#   ./Build.sh --deps    — first install the compiler, CMake, Node and SDL's libraries with apt (asks for sudo), then build
#   ./Build.sh --test    — release build + run tests
#   ./Build.sh --clean   — wipe build/release before building
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

DEPS=0 TEST=0 CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --deps)  DEPS=1 ;;
    --test)  TEST=1 ;;
    --clean) CLEAN=1 ;;
    *) echo "Unknown option: $arg (use --deps, --test or --clean)"; exit 1 ;;
  esac
done

# ------------------------------------------------------------------ system packages (Debian / Ubuntu)
if [ "$DEPS" = 1 ]; then
  if ! command -v apt-get >/dev/null; then
    echo "[deps] --deps only knows apt (Debian/Ubuntu). Install a C++23 compiler, cmake, ninja, pkg-config, Node.js and"
    echo "       SDL3's X11/Wayland development headers with your distribution's package manager."
    exit 1
  fi
  echo "[deps] Installing build tools and libraries (sudo)..."
  sudo apt-get update
  sudo apt-get install -y build-essential cmake ninja-build pkg-config \
    libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev \
    libgl1-mesa-dev libegl1-mesa-dev libdbus-1-dev libudev-dev \
    nodejs npm
fi

for tool in cmake ninja c++; do
  if ! command -v "$tool" >/dev/null; then
    echo "[cpp]  '$tool' not found. Run:  ./Build.sh --deps"
    exit 1
  fi
done

# ------------------------------------------------------------------ web client
if command -v npm >/dev/null; then
  echo "[web]  Building Vue client..."
  (cd web/client && npm install && npm run build) || { echo "[web]  Vue build failed."; exit 1; }
else
  echo "[web]  Node.js not found -- skipping Vue build."
  echo "       Install it (./Build.sh --deps, or https://nodejs.org) to include the web client."
fi

# ------------------------------------------------------------------ C++
echo
echo "[cpp]  Building C++ release..."
BUILD=build/release
[ "$CLEAN" = 1 ] && rm -rf "$BUILD"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"
[ "$TEST" = 1 ] && ctest --test-dir "$BUILD" --output-on-failure

echo
echo "Done. Executables are in $BUILD/"
