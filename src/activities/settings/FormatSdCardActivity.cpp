#include "FormatSdCardActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FormatSdCardActivity::formatBytesHuman(const uint64_t bytes, char* buf, const size_t bufLen) {
  if (bytes >= 1024ULL * 1024 * 1024) {
    snprintf(buf, bufLen, "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes >= 1024ULL * 1024) {
    snprintf(buf, bufLen, "%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    snprintf(buf, bufLen, "%llu KB", static_cast<unsigned long long>(bytes) / 1024ULL);
  }
}

void FormatSdCardActivity::onEnter() {
  Activity::onEnter();
  state = LOADING;
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

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (state == LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_READING_SD_INFO));
    renderer.displayBuffer();
    return;
  }

  if (state == INFO) {
    char usedBuf[16];
    char totalBuf[16];
    formatBytesHuman(totalBytes - freeBytes, usedBuf, sizeof(usedBuf));
    formatBytesHuman(totalBytes, totalBuf, sizeof(totalBuf));

    // Storage usage progress bar
    const int barWidth = pageWidth - metrics.contentSidePadding * 2;
    const int barY = contentTop;
    if (totalBytes > 0) {
      GUI.drawProgressBar(renderer,
                          Rect{metrics.contentSidePadding, barY, barWidth, metrics.progressBarHeight},
                          static_cast<size_t>(totalBytes - freeBytes), static_cast<size_t>(totalBytes));
    }

    // Space label: "Used: X.X GB / Total: X.X GB"
    char spaceBuf[48];
    snprintf(spaceBuf, sizeof(spaceBuf), "%s %s / %s %s", tr(STR_FORMAT_SD_USED), usedBuf,
             tr(STR_FORMAT_SD_FREE), totalBuf);
    const int labelY = barY + metrics.progressBarHeight + metrics.verticalSpacing;
    renderer.drawCenteredText(UI_10_FONT_ID, labelY, spaceBuf);

    // Warning lines
    const int warnY = labelY + 40;
    renderer.drawCenteredText(UI_10_FONT_ID, warnY, tr(STR_FORMAT_SD_WARNING_1), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, warnY + 30, tr(STR_FORMAT_SD_WARNING_2), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, warnY + 60, tr(STR_FORMAT_SD_WARNING_3));

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
  if (state == LOADING) {
    totalBytes = Storage.getCardTotalBytes();
    freeBytes = Storage.getCardFreeBytes();
    {
      RenderLock lock(*this);
      state = INFO;
    }
    requestUpdateAndWait();
    return;
  }

  if (state == INFO) {
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
