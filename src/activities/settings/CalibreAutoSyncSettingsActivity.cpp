#include "CalibreAutoSyncSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr StrId kMenuNames[CalibreAutoSyncSettingsActivity::MENU_ITEMS] = {
    StrId::STR_CALIBRE_AUTO_SYNC,
    StrId::STR_CALIBRE_AUTO_SYNC_HOUR,
    StrId::STR_CALIBRE_SYNC_URL,
    StrId::STR_CALIBRE_SYNC_USERNAME,
    StrId::STR_CALIBRE_SYNC_PASSWORD,
};

}  // namespace

void CalibreAutoSyncSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void CalibreAutoSyncSettingsActivity::onExit() {
  SETTINGS.saveToFile();
  Activity::onExit();
}

void CalibreAutoSyncSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void CalibreAutoSyncSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // Toggle enable/disable
    SETTINGS.calibreAutoSync = SETTINGS.calibreAutoSync ? 0 : 1;
    requestUpdate();

  } else if (selectedIndex == 1) {
    // Cycle sync hour 0–23
    SETTINGS.calibreAutoSyncHour = (SETTINGS.calibreAutoSyncHour + 1) % 24;
    requestUpdate();

  } else if (selectedIndex == 2) {
    // Server URL
    const std::string current = SETTINGS.calibreServerUrl;
    const std::string prefill = current.empty() ? "http://" : current;
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CALIBRE_SYNC_URL), prefill, 128,
                                                InputType::Url),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            const std::string urlToSave = (kb.text == "http://" || kb.text == "https://") ? "" : kb.text;
            strncpy(SETTINGS.calibreServerUrl, urlToSave.c_str(), sizeof(SETTINGS.calibreServerUrl) - 1);
            SETTINGS.calibreServerUrl[sizeof(SETTINGS.calibreServerUrl) - 1] = '\0';
          }
          requestUpdate();
        });

  } else if (selectedIndex == 3) {
    // Username
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CALIBRE_SYNC_USERNAME),
                                                std::string(SETTINGS.calibreUsername), 64, InputType::Text),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            strncpy(SETTINGS.calibreUsername, kb.text.c_str(), sizeof(SETTINGS.calibreUsername) - 1);
            SETTINGS.calibreUsername[sizeof(SETTINGS.calibreUsername) - 1] = '\0';
          }
          requestUpdate();
        });

  } else if (selectedIndex == 4) {
    // Password
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_CALIBRE_SYNC_PASSWORD),
                                                std::string(SETTINGS.calibrePassword), 64, InputType::Password),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& kb = std::get<KeyboardResult>(result.data);
            strncpy(SETTINGS.calibrePassword, kb.text.c_str(), sizeof(SETTINGS.calibrePassword) - 1);
            SETTINGS.calibrePassword[sizeof(SETTINGS.calibrePassword) - 1] = '\0';
          }
          requestUpdate();
        });
  }
}

void CalibreAutoSyncSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_CAT_CALIBRE_SYNC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  // Reserve space for the USB power note above the button hints.
  constexpr int kNoteHeight = 20;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - kNoteHeight;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEMS,
      static_cast<int>(selectedIndex),
      [](int index) { return std::string(I18N.get(kMenuNames[index])); },
      nullptr, nullptr,
      [](int index) -> std::string {
        if (index == 0) {
          return SETTINGS.calibreAutoSync ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (index == 1) {
          char buf[8];
          snprintf(buf, sizeof(buf), "%02d:00", SETTINGS.calibreAutoSyncHour);
          return std::string(buf);
        } else if (index == 2) {
          return SETTINGS.calibreServerUrl[0] ? std::string(SETTINGS.calibreServerUrl) : tr(STR_NOT_SET);
        } else if (index == 3) {
          return SETTINGS.calibreUsername[0] ? std::string(SETTINGS.calibreUsername) : tr(STR_NOT_SET);
        } else if (index == 4) {
          return SETTINGS.calibrePassword[0] ? std::string("******") : tr(STR_NOT_SET);
        }
        return std::string();
      },
      true);

  // USB power note — drawn between the list and the button hints.
  const int noteY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - kNoteHeight;
  renderer.drawCenteredText(UI_10_FONT_ID, noteY, tr(STR_CALIBRE_SYNC_USB_ONLY), true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
