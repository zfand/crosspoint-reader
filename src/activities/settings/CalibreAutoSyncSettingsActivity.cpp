#include "CalibreAutoSyncSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CalibreSyncRunner.h"

namespace {

constexpr StrId kMenuNames[CalibreAutoSyncSettingsActivity::MENU_ITEMS] = {
    StrId::STR_CALIBRE_AUTO_SYNC,
    StrId::STR_CALIBRE_AUTO_SYNC_HOUR,
    StrId::STR_CALIBRE_SYNC_SERVER,
    StrId::STR_CALIBRE_SYNC_TAG_FILTER,
    StrId::STR_CALIBRE_SYNC_CUSTOM_TAG,
    StrId::STR_CALIBRE_FEED_MAX_SIZE,
    StrId::STR_CALIBRE_SYNC_NOW,
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

    if (SETTINGS.calibreAutoSync) {
      // Bootstrap system time so the sleep timer can be armed immediately.
      ntpStatus = tr(STR_CALIBRE_NTP_SYNCING);
      requestUpdateAndWait();

      LOG_INF("CASET", "Syncing time via NTP...");
      WiFi.mode(WIFI_STA);
      WiFi.begin();
      const unsigned long wifiStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
        vTaskDelay(200 / portTICK_PERIOD_MS);
      }
      if (WiFi.status() == WL_CONNECTED) {
        if (esp_sntp_enabled()) esp_sntp_stop();
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
        int retry = 0;
        while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < 50) {
          vTaskDelay(100 / portTICK_PERIOD_MS);
          retry++;
        }
        const bool synced = retry < 50;
        LOG_INF("CASET", synced ? "NTP time synced" : "NTP sync timed out");
        ntpStatus = synced ? tr(STR_CALIBRE_NTP_SYNCED) : tr(STR_CALIBRE_NTP_FAILED);
        if (esp_sntp_enabled()) esp_sntp_stop();
      } else {
        LOG_ERR("CASET", "WiFi connect failed — time not synced");
        ntpStatus = tr(STR_CALIBRE_NTP_FAILED);
      }
      WiFi.mode(WIFI_OFF);
    } else {
      ntpStatus.clear();
    }

    requestUpdate();

  } else if (selectedIndex == 1) {
    // Cycle sync hour 0–23
    SETTINGS.calibreAutoSyncHour = (SETTINGS.calibreAutoSyncHour + 1) % 24;
    requestUpdate();

  } else if (selectedIndex == 2) {
    // Cycle through configured OPDS servers (or clear selection)
    const auto count = static_cast<int8_t>(OPDS_STORE.getCount());
    if (count == 0) return;
    const int8_t next = SETTINGS.calibreSyncServerIndex + 1;
    SETTINGS.calibreSyncServerIndex = (next >= count) ? -1 : next;
    requestUpdate();

  } else if (selectedIndex == 3) {
    // Cycle tag filter: All → News → Custom
    SETTINGS.calibreSyncTagMode = (SETTINGS.calibreSyncTagMode + 1) % 3;
    requestUpdate();

  } else if (selectedIndex == 4) {
    // Edit custom tag via keyboard
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        strncpy(SETTINGS.calibreSyncCustomTag, kb.text.c_str(),
                sizeof(SETTINGS.calibreSyncCustomTag) - 1);
        SETTINGS.calibreSyncCustomTag[sizeof(SETTINGS.calibreSyncCustomTag) - 1] = '\0';
        requestUpdate();
      }
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                               tr(STR_CALIBRE_SYNC_CUSTOM_TAG),
                                               std::string(SETTINGS.calibreSyncCustomTag), 31,
                                               InputType::Text),
        handler);

  } else if (selectedIndex == 5) {
    // Cycle max feed size 1–20
    SETTINGS.calibreFeedMaxSize = (SETTINGS.calibreFeedMaxSize % 20) + 1;
    requestUpdate();

  } else if (selectedIndex == 6) {
    // Sync Now
    ntpStatus = tr(STR_CONNECTING);
    requestUpdateAndWait();

    WiFi.mode(WIFI_STA);
    WiFi.begin();
    const unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.mode(WIFI_OFF);
      ntpStatus = tr(STR_CALIBRE_SYNC_WIFI_FAIL);
      requestUpdate();
      return;
    }

    ntpStatus = tr(STR_CALIBRE_SYNC_FETCHING);
    requestUpdateAndWait();

    const int result = CalibreSyncRunner::syncNow();
    WiFi.mode(WIFI_OFF);

    if (result < 0) {
      ntpStatus = tr(STR_CALIBRE_SYNC_FAILED);
    } else if (result == 0) {
      ntpStatus = tr(STR_CALIBRE_SYNC_BOOKS_NONE);
    } else {
      char buf[48];
      snprintf(buf, sizeof(buf), tr(STR_CALIBRE_SYNC_BOOKS_NEW), result);
      ntpStatus = buf;
    }
    requestUpdate();
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
  // Reserve space for up to two note lines above the button hints.
  constexpr int kNoteHeight = 20;
  const int notesHeight = ntpStatus.empty() ? kNoteHeight : kNoteHeight * 2;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - notesHeight;

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
          const int8_t idx = SETTINGS.calibreSyncServerIndex;
          if (idx < 0) return tr(STR_NOT_SET);
          const auto* server = OPDS_STORE.getServer(static_cast<size_t>(idx));
          if (!server) return tr(STR_NOT_SET);
          return server->name.empty() ? server->url : server->name;
        } else if (index == 3) {
          const uint8_t mode = SETTINGS.calibreSyncTagMode;
          if (mode == 1) return std::string(tr(STR_NEWS));
          if (mode == 2) return std::string(tr(STR_CUSTOM));
          return std::string(tr(STR_ALL));
        } else if (index == 4) {
          return std::string(SETTINGS.calibreSyncCustomTag);
        } else if (index == 5) {
          char buf[8];
          snprintf(buf, sizeof(buf), "%d", SETTINGS.calibreFeedMaxSize);
          return std::string(buf);
        } else if (index == 6) {
          return std::string();
        }
        return std::string();
      },
      true);

  // Notes — drawn between the list and the button hints.
  const int note2Y = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - kNoteHeight;
  const int note1Y = note2Y - kNoteHeight;
  if (!ntpStatus.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, note1Y, tr(STR_CALIBRE_SYNC_USB_ONLY), true);
    renderer.drawCenteredText(UI_10_FONT_ID, note2Y, ntpStatus.c_str(), true);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, note2Y, tr(STR_CALIBRE_SYNC_USB_ONLY), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
