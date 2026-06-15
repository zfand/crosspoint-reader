#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/xpath_utils"
BINARY="$BUILD_DIR/XPathUtilsTest"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/xpath_utils/XPathUtilsTest.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -I"$ROOT_DIR"
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
