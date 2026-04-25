#pragma once

#include "activities/Activity.h"

class SdCardActivity final : public Activity {
 public:
  explicit SdCardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SdCard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { LOADING, READY };

  State state = LOADING;
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;

  void loadSpaceInfo();
  static void formatBytesHuman(uint64_t bytes, char* buf, size_t bufLen);
};
