#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "../reader/TxtReaderActivity.h"

class SentenceUnderlineActivity final : public TxtReaderActivity {
  struct LaidWord {
    std::string text;
    int x = 0;
    int y = 0;
    uint16_t sentenceIdx = 0;
  };

  std::vector<LaidWord> laidWords;
  std::vector<uint16_t> sentenceIndexPerLine;
  uint32_t totalSourceLines = 0;
  uint16_t currentSentenceIndex = 0;
  std::chrono::steady_clock::time_point lastSentenceTime{};

  static constexpr int kSentenceIntervalMs = 2500;

  void rebuildLaidWords();

 public:
  explicit SentenceUnderlineActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool preventAutoSleep() override { return true; }
  bool isReaderActivity() const override { return false; }
};
