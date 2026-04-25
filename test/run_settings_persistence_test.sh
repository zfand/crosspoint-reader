#!/usr/bin/env bash
# Builds and runs SettingsPersistenceTest — verifies that settings.json
# survives a format-and-resave cycle on a FAT32 volume.
#
# Requires: mkfs.fat (dosfstools), mcopy/mmd/mdir (mtools).
# On Ubuntu: sudo apt-get install -y dosfstools mtools
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/settings_persistence"
BINARY="$BUILD_DIR/SettingsPersistenceTest"

for tool in mkfs.fat mcopy mmd mdir; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "ERROR: '$tool' not found.  Install dosfstools and mtools." >&2
    exit 1
  fi
done

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/format_volume/SettingsPersistenceTest.cpp"
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
