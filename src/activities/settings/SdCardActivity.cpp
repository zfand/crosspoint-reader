#include "SdCardActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "FormatSdCardActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BytesFormatter.h"

void SdCardActivity::loadSpaceInfo() {
  totalBytes = Storage.getCardTotalBytes();
  freeBytes = Storage.getCardFreeBytes();
}

void SdCardActivity::onEnter() {
  Activity::onEnter();
  state = LOADING;
  requestUpdate();
}

void SdCardActivity::onExit() { Activity::onExit(); }

void SdCardActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SD_CARD_INFO));

  if (state == LOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_READING_SD_INFO));
    renderer.displayBuffer();
    return;
  }

  // READY: space usage bar + labels
  const int barY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  const int barWidth = pageWidth - metrics.contentSidePadding * 2;

  if (totalBytes > 0) {
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, barY, barWidth, metrics.progressBarHeight},
                        static_cast<size_t>(totalBytes - freeBytes), static_cast<size_t>(totalBytes));
  }

  char usedBuf[16];
  char freeBuf[16];
  char totalBuf[16];
  BytesFormatter::format(totalBytes - freeBytes, usedBuf, sizeof(usedBuf));
  BytesFormatter::format(freeBytes, freeBuf, sizeof(freeBuf));
  BytesFormatter::format(totalBytes, totalBuf, sizeof(totalBuf));

  char spaceBuf[64];
  snprintf(spaceBuf, sizeof(spaceBuf), "%s %s  |  %s %s  |  %s %s",
           tr(STR_FORMAT_SD_USED), usedBuf,
           tr(STR_FORMAT_SD_FREE), freeBuf,
           tr(STR_FORMAT_SD_TOTAL), totalBuf);

  const int labelY = barY + metrics.progressBarHeight + metrics.verticalSpacing;
  renderer.drawCenteredText(UI_10_FONT_ID, labelY, spaceBuf);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FORMAT_SD_CARD), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void SdCardActivity::loop() {
  if (state == LOADING) {
    loadSpaceInfo();
    {
      RenderLock lock(*this);
      state = READY;
    }
    requestUpdateAndWait();
    return;
  }

  if (state == READY) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      startActivityForResult(
          std::make_unique<FormatSdCardActivity>(renderer, mappedInput),
          [this](const ActivityResult&) {
            // Re-read space after a format so the bar reflects the new state
            state = LOADING;
            requestUpdate();
          });
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
    }
    return;
  }
}
