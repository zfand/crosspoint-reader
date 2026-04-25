# TODO

## Sleep screen overlay: Calibre auto-sync status

When Calibre auto-sync is enabled, the sleep screen should show a small overlay
indicating the next scheduled sync time, and update after a sync completes to
show whether it succeeded or failed.

**Desired behaviour**

| State | Overlay text |
|---|---|
| Sync enabled, not yet fired | `Next sync: 04:00` |
| Sync completed successfully | `Sync complete — 3 book(s)` |
| Sync completed, nothing new | `Sync complete — no new books` |
| Sync failed | `Sync failed` |

The overlay should appear as a small, unobtrusive box (e.g. bottom-right corner
of the sleep screen) so it does not interfere with cover art or a custom sleep
image.

**Implementation notes**

Three pieces of work:

1. **Persistent sync state (`RTC_NOINIT` or SD card)**

   A small struct needs to survive the timer-wakeup → sync → sleep cycle so the
   sleep screen can read it after the user manually wakes the device later.

   `RTC_NOINIT` memory is the cleanest option — zero SD card writes, survives
   deep sleep, does not persist across a full power-off (acceptable since the
   state is only meaningful for the current charge cycle).

   ```cpp
   struct RTC_NOINIT_ATTR CalibreSyncState {
       uint8_t magic;           // Sentinel to detect uninitialised memory
       uint8_t result;          // 0=never, 1=success, 2=failed
       uint8_t booksDownloaded;
       uint8_t nextSyncHour;
   };
   ```

   Alternatively, write a tiny file to `/.crosspoint/calibre_sync_state.bin` on
   the SD card — simpler but adds a write on every sync.

2. **`CalibreSyncRunner` writes the result**

   After sync completes (or fails), before calling `startDeepSleep()`, write the
   outcome into the persistent store.

3. **Sleep screen overlay in `SleepActivity::render()`**

   Read the sync state and, if `calibreAutoSync` is enabled, draw a small
   overlay using `UI_10_FONT_ID`. Must be orientation-aware — use
   `renderer.getOrientedViewableTRBL()` for positioning.

**Related code**

Sync runner: `src/network/CalibreSyncRunner.cpp`
Timer wakeup: `lib/hal/HalPowerManager.cpp`
Settings sub-menu: `src/activities/settings/CalibreAutoSyncSettingsActivity.cpp`
