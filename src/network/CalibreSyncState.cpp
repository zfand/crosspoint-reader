#include "CalibreSyncState.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace {
constexpr char STATE_FILE[] = "/.crosspoint/calibre_sync.json";
}

void CalibreSyncState::load() {
  feedUpdated.clear();
  lastSynced.clear();

  if (!Storage.exists(STATE_FILE)) return;

  const String json = Storage.readFile(STATE_FILE);
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json.c_str())) {
    LOG_ERR("CSYNC", "Failed to parse calibre_sync.json");
    return;
  }

  feedUpdated = doc["feed_updated"] | std::string("");
  lastSynced  = doc["last_synced"]  | std::string("");
}

bool CalibreSyncState::save() const {
  JsonDocument doc;
  doc["feed_updated"] = feedUpdated;
  doc["last_synced"]  = lastSynced;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(STATE_FILE, json);
}
