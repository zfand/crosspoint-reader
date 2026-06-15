#pragma once

#include <HalGPIO.h>

/**
 * CalibreSyncRunner
 *
 * Performs a silent background sync with a Calibre Content Server.
 * Called directly from setup() on a ScheduledSync wakeup — the display
 * is never initialised, so e-ink wear is zero during overnight syncs.
 *
 * Flow:
 *   1. Connect to the last-saved WiFi network (no UI).
 *   2. Fetch /opds/new from the configured Calibre Content Server.
 *   3. Download any EPUB entries not already present on the SD card.
 *   4. Disconnect WiFi and return — caller is responsible for sleeping.
 */
class CalibreSyncRunner {
 public:
  // Full sync: connects WiFi, runs syncNow(), disconnects.
  // Returns true if at least one book was downloaded.
  static bool run(HalGPIO& gpio);

  // WiFi-agnostic sync: discovers the "By Newest" feed from the root catalog,
  // skips if nothing has changed, downloads only books newer than last sync.
  // Returns number of books downloaded, or -1 on error.
  // Caller is responsible for WiFi being connected before calling.
  static int syncNow();

  static constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
  static constexpr const char* FEED_DIR = "/feed";
  static constexpr const char* OPDS_ROOT_PATH = "/opds";
};
