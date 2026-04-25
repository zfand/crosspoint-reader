#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Sub-menu for Calibre Auto-Sync settings.
 * Shows enable toggle, sync hour, server URL, username, and password.
 * Includes a note that the device must be on USB power for auto-sync to fire.
 */
class CalibreAutoSyncSettingsActivity final : public Activity {
 public:
  explicit CalibreAutoSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CalibreAutoSyncSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;

  static constexpr int MENU_ITEMS = 5;

  void handleSelection();
};
