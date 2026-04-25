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
  enum State { WARNING, FORMATTING, SUCCESS, FAILED };

  State state = WARNING;

  void doFormat();
};
