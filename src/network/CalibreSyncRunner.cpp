#include "CalibreSyncRunner.h"

#include <HalStorage.h>
#include <Logging.h>
#include <OpdsParser.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "CalibreSyncState.h"
#include "CrossPointSettings.h"
#include "HttpDownloader.h"
#include "OpdsServerStore.h"
#include "util/UrlUtils.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= CalibreSyncRunner::WIFI_TIMEOUT_MS) {
      LOG_ERR("CSYNC", "WiFi timed out after %lu ms", CalibreSyncRunner::WIFI_TIMEOUT_MS);
      return false;
    }
    delay(200);
  }
  LOG_INF("CSYNC", "WiFi connected: %s", WiFi.localIP().toString().c_str());
  return true;
}

// Case-insensitive exact match against comma-separated tag string (e.g. "News,Fiction").
static bool hasTag(const std::string& tags, const char* target) {
  if (!target || target[0] == '\0' || tags.empty()) return false;
  const char* p = tags.c_str();
  while (*p) {
    const char* end = p;
    while (*end && *end != ',') ++end;
    const size_t tlen = strlen(target);
    if (static_cast<size_t>(end - p) == tlen) {
      bool match = true;
      for (size_t i = 0; i < tlen; i++) {
        if (tolower(static_cast<unsigned char>(p[i])) != tolower(static_cast<unsigned char>(target[i]))) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
    p = *end ? end + 1 : end;
  }
  return false;
}

static bool containsNewest(const std::string& title) {
  for (size_t i = 0; i + 6 <= title.size(); i++) {
    if (tolower(static_cast<unsigned char>(title[i]))     == 'n' &&
        tolower(static_cast<unsigned char>(title[i + 1])) == 'e' &&
        tolower(static_cast<unsigned char>(title[i + 2])) == 'w' &&
        tolower(static_cast<unsigned char>(title[i + 3])) == 'e' &&
        tolower(static_cast<unsigned char>(title[i + 4])) == 's' &&
        tolower(static_cast<unsigned char>(title[i + 5])) == 't') {
      return true;
    }
  }
  return false;
}

// Delete oldest .epub files in FEED_DIR beyond maxCount (alphabetical = chronological for
// date-named Calibre news files).
static void pruneOldestEpubs(uint8_t maxCount) {
  if (maxCount == 0) return;

  auto files = Storage.listFiles(CalibreSyncRunner::FEED_DIR);

  size_t epubCount = 0;
  for (auto& f : files) {
    if (f.length() > 5 && f.endsWith(".epub")) {
      files[epubCount++] = std::move(f);
    }
  }
  files.resize(epubCount);

  if (files.size() <= maxCount) return;

  std::sort(files.begin(), files.end(),
            [](const String& a, const String& b) { return strcmp(a.c_str(), b.c_str()) < 0; });

  const size_t toDelete = files.size() - maxCount;
  char path[256];
  for (size_t i = 0; i < toDelete; i++) {
    snprintf(path, sizeof(path), "%s/%s", CalibreSyncRunner::FEED_DIR, files[i].c_str());
    LOG_INF("CSYNC", "Pruning old feed file: %s", files[i].c_str());
    Storage.remove(path);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int CalibreSyncRunner::syncNow() {
  const int8_t serverIdx = SETTINGS.calibreSyncServerIndex;
  if (serverIdx < 0) {
    LOG_ERR("CSYNC", "No sync server configured");
    return -1;
  }
  const auto* server = OPDS_STORE.getServer(static_cast<size_t>(serverIdx));
  if (!server) {
    LOG_ERR("CSYNC", "Sync server index %d not found in OPDS store", serverIdx);
    return -1;
  }
  const std::string& serverUrl = server->url;
  const std::string& username = server->username;
  const std::string& password = server->password;

  Storage.mkdir(FEED_DIR);

  // Step 1: Discover "By Newest" URL from root catalog.
  const std::string rootUrl = UrlUtils::ensureProtocol(serverUrl) + OPDS_ROOT_PATH;
  LOG_INF("CSYNC", "Fetching OPDS root: %s", rootUrl.c_str());

  OpdsParser rootParser;
  OpdsParserStream rootStream(rootParser);
  if (!HttpDownloader::fetchUrl(rootUrl, rootStream, username.c_str(),
                                password.c_str()) ||
      rootParser.error()) {
    LOG_ERR("CSYNC", "Failed to fetch OPDS root");
    return -1;
  }

  std::string newestUrl;
  for (const auto& entry : rootParser.getEntries()) {
    if (entry.type == OpdsEntryType::NAVIGATION && containsNewest(entry.title)) {
      newestUrl = UrlUtils::buildUrl(serverUrl, entry.href);
      break;
    }
  }
  if (newestUrl.empty()) {
    LOG_ERR("CSYNC", "Could not find 'By Newest' entry in OPDS root");
    return -1;
  }
  LOG_INF("CSYNC", "Newest feed: %s", newestUrl.c_str());

  // Step 2: Fetch the newest feed.
  OpdsParser feedParser;
  OpdsParserStream feedStream(feedParser);
  if (!HttpDownloader::fetchUrl(newestUrl, feedStream, username.c_str(),
                                password.c_str()) ||
      feedParser.error()) {
    LOG_ERR("CSYNC", "Failed to fetch newest feed");
    return -1;
  }

  // Step 3: Check if anything changed since last sync.
  CalibreSyncState state;
  state.load();

  const std::string& feedUpdated = feedParser.getFeedUpdated();
  if (!feedUpdated.empty() && feedUpdated == state.feedUpdated) {
    LOG_INF("CSYNC", "Feed unchanged (updated=%s) — nothing to do", feedUpdated.c_str());
    return 0;
  }

  // Step 4: Download books newer than last sync.
  const auto& entries = feedParser.getEntries();
  int downloaded = 0;
  std::string latestDate = state.lastSynced;

  for (const auto& entry : entries) {
    if (entry.type != OpdsEntryType::BOOK || entry.href.empty()) continue;

    const uint8_t tagMode = SETTINGS.calibreSyncTagMode;
    if (tagMode == 1 && !hasTag(entry.tags, "News")) continue;
    if (tagMode == 2 && !hasTag(entry.tags, SETTINGS.calibreSyncCustomTag)) continue;

    const std::string filename = UrlUtils::filenameFromUrl(entry.href);
    if (filename.size() <= 5 || filename.substr(filename.size() - 5) != ".epub") continue;

    if (!state.lastSynced.empty() && !entry.published.empty() &&
        entry.published <= state.lastSynced) {
      LOG_DBG("CSYNC", "Skipping (not newer): %s (%s)", filename.c_str(), entry.published.c_str());
      continue;
    }

    char destPath[256];
    snprintf(destPath, sizeof(destPath), "%s/%.*s", FEED_DIR,
             static_cast<int>(filename.size()), filename.data());

    if (Storage.exists(destPath)) {
      LOG_DBG("CSYNC", "Already on SD: %s", filename.c_str());
      if (entry.published > latestDate) latestDate = entry.published;
      continue;
    }

    const std::string downloadUrl = UrlUtils::buildUrl(serverUrl, entry.href);
    LOG_INF("CSYNC", "Downloading: %s", filename.c_str());

    const auto err = HttpDownloader::downloadToFile(downloadUrl, destPath, nullptr,
                                                    username.c_str(), password.c_str());
    if (err == HttpDownloader::OK) {
      LOG_INF("CSYNC", "Downloaded: %s", filename.c_str());
      downloaded++;
      if (entry.published > latestDate) latestDate = entry.published;
    } else {
      LOG_ERR("CSYNC", "Download failed for %s (err=%d)", filename.c_str(),
              static_cast<int>(err));
      Storage.remove(destPath);
    }
  }

  // Step 5: Persist updated state.
  if (!feedUpdated.empty()) state.feedUpdated = feedUpdated;
  if (!latestDate.empty()) state.lastSynced = latestDate;
  state.save();

  // Step 6: Prune oldest files beyond the configured feed size.
  pruneOldestEpubs(SETTINGS.calibreFeedMaxSize);

  LOG_INF("CSYNC", "Sync complete — %d book(s) downloaded", downloaded);
  return downloaded;
}

bool CalibreSyncRunner::run(HalGPIO& /*gpio*/) {
  if (!connectWifi()) return false;
  const int result = syncNow();
  WiFi.mode(WIFI_OFF);
  return result > 0;
}
