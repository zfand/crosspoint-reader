#pragma once

#include "activities/Activity.h"

class FormatSdCardActivity final : public Activity {
 public:
  explicit FormatSdCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FormatSdCard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { LOADING, INFO, FORMATTING, SUCCESS, FAILED };

  State state = LOADING;
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;

  void doFormat();
  static void formatBytesHuman(uint64_t bytes, char* buf, size_t bufLen);
};
