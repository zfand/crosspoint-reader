#!/usr/bin/env bash
# E2E test for the SD Card info / format feature via Wokwi CLI.
#
# Prerequisites:
#   1. Build firmware:  pio run
#   2. Install Wokwi CLI: https://docs.wokwi.com/wokwi-ci/getting-started
#   3. Set WOKWI_CLI_TOKEN in environment (Wokwi subscription required)
#
# Usage:
#   ./test/e2e/sdcard_e2e.sh           # auto-detects wokwi-cli
#   WOKWI_CLI_TOKEN=xxx ./test/e2e/sdcard_e2e.sh

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FIRMWARE="$ROOT_DIR/.pio/build/default/firmware.bin"

if [ ! -f "$FIRMWARE" ]; then
  echo "ERROR: firmware not built. Run 'pio run' first." >&2
  exit 1
fi

WOKWI="$(command -v wokwi-cli 2>/dev/null || true)"
if [ -z "$WOKWI" ]; then
  echo "wokwi-cli not found — printing manual test checklist instead."
  cat <<'MANUAL'

Manual E2E Test Checklist — SD Card Feature
============================================

Hardware: Xteink X4 with SD card inserted.

Boot checks (serial output):
  [ ] "[SD] SD card detected"
  [ ] No panic or assertion failures

SD Card screen (Settings → System → SD Card):
  [ ] "Reading SD card info..." shown briefly during LOADING state
  [ ] Progress bar fills proportionally to used space
  [ ] "Used: X  |  Free: X  |  Total: X" label shows plausible values
  [ ] Confirm button label reads "Format SD Card"
  [ ] Back button exits to Settings

Cancel format flow:
  [ ] Press Confirm → warning screen shows
  [ ] Warning line 1: "This will permanently erase ALL files on the SD card."
  [ ] Warning line 2: "All books and reading progress will be lost!"
  [ ] Warning line 3: "This action cannot be undone."
  [ ] Press Back/Cancel → returns to SD Card info screen (space bar still correct)

Format flow:
  [ ] Press Confirm on SD Card screen → warning shown
  [ ] Press Confirm on warning → "Formatting SD Card..." shown
  [ ] Serial: "[FORMAT_SD] Starting SD card format..."
  [ ] Serial: "[FORMAT_SD] Format succeeded"
  [ ] Success screen shown
  [ ] Press Back → returns to SD Card info screen
  [ ] Space bar now shows mostly free space (card freshly formatted)

Post-format sanity:
  [ ] Settings still load correctly (settings survive format, re-saved from RAM)
  [ ] File browser shows empty SD card

MANUAL
  exit 0
fi

# ── Wokwi CLI automated run ──────────────────────────────────────────────────
echo "Running Wokwi E2E simulation..."

TIMEOUT=60  # seconds

# Expected serial output lines (in order)
EXPECT_BOOT="SD card detected"
EXPECT_FORMAT_START="FORMAT_SD.*Starting SD card format"
EXPECT_FORMAT_OK="FORMAT_SD.*Format succeeded"

LOGFILE="$(mktemp /tmp/wokwi-sdcard-XXXXXX.log)"
trap 'rm -f "$LOGFILE"' EXIT

"$WOKWI" run \
  --timeout "$TIMEOUT" \
  --diagram-file "$ROOT_DIR/diagram.json" \
  --firmware "$FIRMWARE" \
  2>&1 | tee "$LOGFILE" &

WOKWI_PID=$!

wait_for_pattern() {
  local pattern="$1"
  local label="$2"
  local deadline=$(( $(date +%s) + TIMEOUT ))
  while [ "$(date +%s)" -lt "$deadline" ]; do
    if grep -qE "$pattern" "$LOGFILE" 2>/dev/null; then
      echo "PASS: $label"
      return 0
    fi
    sleep 1
  done
  echo "FAIL: $label (pattern: $pattern)" >&2
  kill "$WOKWI_PID" 2>/dev/null || true
  exit 1
}

wait_for_pattern "$EXPECT_BOOT"         "SD card detected at boot"
wait_for_pattern "$EXPECT_FORMAT_START" "Format operation started"
wait_for_pattern "$EXPECT_FORMAT_OK"    "Format succeeded"

kill "$WOKWI_PID" 2>/dev/null || true
echo ""
echo "All E2E checks passed."
