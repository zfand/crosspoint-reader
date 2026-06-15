#pragma once
#include <string>

/**
 * Persists Calibre auto-sync state to /.crosspoint/calibre_sync.json.
 * feedUpdated: the <updated> timestamp of the last fetched "By Newest" feed.
 * lastSynced:  the <dc:date>/<published> of the most recently downloaded book.
 *
 * On load failure (file absent or corrupt) both fields are empty — first sync
 * downloads all entries in the feed and then writes the initial state.
 */
struct CalibreSyncState {
  std::string feedUpdated;
  std::string lastSynced;

  void load();
  bool save() const;
};
