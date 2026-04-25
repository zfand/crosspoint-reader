#include "FormatSdCardActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FormatSdCardActivity::onEnter() {
  Activity::onEnter();
  state = WARNING;
  requestUpdate();
}

void FormatSdCardActivity::onExit() { Activity::onExit(); }

void FormatSdCardActivity::doFormat() {
  LOG_INF("FORMAT_SD", "Starting SD card format...");
  const bool ok = Storage.formatCard();
  state = ok ? SUCCESS : FAILED;
  LOG_INF("FORMAT_SD", "Format %s", ok ? "succeeded" : "failed");
  requestUpdate();
}

void FormatSdCardActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FORMAT_SD_CARD));

  if (state == WARNING) {
    const int mid = pageHeight / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, mid - 60, tr(STR_FORMAT_SD_WARNING_1), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, mid - 30, tr(STR_FORMAT_SD_WARNING_2), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, mid + 10, tr(STR_FORMAT_SD_WARNING_3));

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_FORMAT_SD_CARD), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FORMATTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FORMATTING_SD));
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FORMAT_SD_SUCCESS), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_FORMAT_SD_FAILED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, tr(STR_CHECK_SERIAL_OUTPUT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void FormatSdCardActivity::loop() {
  if (state == WARNING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      LOG_INF("FORMAT_SD", "User confirmed format");
      {
        RenderLock lock(*this);
        state = FORMATTING;
      }
      requestUpdateAndWait();
      doFormat();
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }
}
