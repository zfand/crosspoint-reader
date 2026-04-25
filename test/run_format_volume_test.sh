#!/usr/bin/env bash
# Builds and runs FormatVolumeTest — verifies that a FAT32 format operation
# transforms a raw disk image into a valid FAT32 volume.
#
# Requires mkfs.fat (dosfstools).  On Ubuntu: sudo apt-get install dosfstools
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/format_volume"
BINARY="$BUILD_DIR/FormatVolumeTest"

if ! command -v mkfs.fat >/dev/null 2>&1; then
  echo "ERROR: mkfs.fat not found.  Install dosfstools (e.g. sudo apt-get install -y dosfstools)" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/format_volume/FormatVolumeTest.cpp"
)


CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
)

c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
