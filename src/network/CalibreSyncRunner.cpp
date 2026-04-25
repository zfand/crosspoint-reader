#include "CalibreSyncRunner.h"

#include <HalStorage.h>
#include <Logging.h>
#include <OpdsParser.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include <string>

#include "CrossPointSettings.h"
#include "HttpDownloader.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // Uses NVS-stored credentials from the last successful connection

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= CalibreSyncRunner::WIFI_TIMEOUT_MS) {
      LOG_ERR("CSYNC", "WiFi connection timed out after %lu ms", CalibreSyncRunner::WIFI_TIMEOUT_MS);
      return false;
    }
    delay(200);
  }
  LOG_INF("CSYNC", "WiFi connected: %s", WiFi.localIP().toString().c_str());
  return true;
}

// Returns the filename portion of a URL path (everything after the last '/').
static std::string filenameFromUrl(const std::string& url) {
  const size_t slash = url.rfind('/');
  if (slash == std::string::npos || slash + 1 >= url.size()) return url;
  return url.substr(slash + 1);
}

// Returns true if a file with the given base name already exists under BOOKS_DIR.
static bool bookAlreadyOnSd(const std::string& filename) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%.*s", CalibreSyncRunner::BOOKS_DIR,
           static_cast<int>(filename.size()), filename.data());
  return Storage.exists(path);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool CalibreSyncRunner::run(HalGPIO& /*gpio*/) {
  const char* serverUrl = SETTINGS.calibreServerUrl;
  if (serverUrl[0] == '\0') {
    LOG_ERR("CSYNC", "No Calibre server URL configured — aborting sync");
    return false;
  }

  if (!connectWifi()) {
    return false;
  }

  // Ensure the books directory exists on the SD card.
  Storage.mkdir(BOOKS_DIR);

  // Fetch the OPDS "Recently Added" feed and stream it directly into the parser.
  const std::string feedUrl = std::string(serverUrl) + OPDS_NEW_PATH;
  LOG_INF("CSYNC", "Fetching OPDS feed: %s", feedUrl.c_str());

  OpdsParser parser;
  OpdsParserStream parserStream(parser);
  const bool fetched = HttpDownloader::fetchUrl(feedUrl, parserStream,
                                                SETTINGS.calibreUsername,
                                                SETTINGS.calibrePassword);
  WiFi.mode(WIFI_OFF);

  if (!fetched) {
    LOG_ERR("CSYNC", "Failed to fetch OPDS feed from %s", feedUrl.c_str());
    return false;
  }
  if (parser.error()) {
    LOG_ERR("CSYNC", "OPDS feed parse error");
    return false;
  }

  // Reconnect WiFi for downloads (was turned off after fetch above).
  // Only reconnect if there are books to download.
  const auto entries = std::move(parser).getEntries();
  int downloaded = 0;

  for (const auto& entry : entries) {
    if (entry.type != OpdsEntryType::BOOK) continue;
    if (entry.href.empty()) continue;

    const std::string filename = filenameFromUrl(entry.href);
    if (filename.empty()) continue;

    // Only download EPUB files.
    const bool isEpub = filename.size() > 5 &&
                        filename.substr(filename.size() - 5) == ".epub";
    if (!isEpub) continue;

    if (bookAlreadyOnSd(filename)) {
      LOG_DBG("CSYNC", "Already on SD: %s", filename.c_str());
      continue;
    }

    // Lazy-reconnect WiFi on first book to download.
    if (WiFi.status() != WL_CONNECTED) {
      LOG_INF("CSYNC", "Reconnecting WiFi for download");
      if (!connectWifi()) {
        break;
      }
    }

    char destPath[256];
    snprintf(destPath, sizeof(destPath), "%s/%.*s", BOOKS_DIR,
             static_cast<int>(filename.size()), filename.data());

    // Build the full download URL. entry.href may be relative or absolute.
    std::string downloadUrl = entry.href;
    if (downloadUrl.rfind("http", 0) != 0) {
      downloadUrl = std::string(serverUrl) + "/" + downloadUrl;
    }

    LOG_INF("CSYNC", "Downloading: %s → %s", downloadUrl.c_str(), destPath);
    const auto err = HttpDownloader::downloadToFile(downloadUrl, destPath,
                                                    nullptr,
                                                    SETTINGS.calibreUsername,
                                                    SETTINGS.calibrePassword);
    if (err == HttpDownloader::OK) {
      LOG_INF("CSYNC", "Downloaded: %s", filename.c_str());
      downloaded++;
    } else {
      LOG_ERR("CSYNC", "Download failed for %s (err=%d)", filename.c_str(), static_cast<int>(err));
      Storage.remove(destPath);  // Remove partial file
    }
  }

  WiFi.mode(WIFI_OFF);
  LOG_INF("CSYNC", "Sync complete — %d book(s) downloaded", downloaded);
  return downloaded > 0;
}
