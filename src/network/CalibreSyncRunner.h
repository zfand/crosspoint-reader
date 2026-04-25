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
  // Returns true if at least one book was downloaded successfully.
  // All errors are logged via LOG_ERR; never throws.
  static bool run(HalGPIO& gpio);

 private:
  static constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
  static constexpr const char* BOOKS_DIR = "/books";
  static constexpr const char* OPDS_NEW_PATH = "/opds/new";
};
