#pragma once

#include <string>

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

  static constexpr int MENU_ITEMS = 7;

 private:
  ButtonNavigator buttonNavigator;
  size_t selectedIndex = 0;
  std::string ntpStatus;

  void handleSelection();
};
