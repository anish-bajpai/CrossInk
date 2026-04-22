#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "../reader/TxtReaderActivity.h"

class SentenceUnderlineActivity final : public TxtReaderActivity {
  std::vector<uint16_t> sentenceIndexPerLine;
  uint32_t totalSourceLines = 0;
  uint16_t currentSentenceIndex = 0;
  std::chrono::steady_clock::time_point lastSentenceTime{};

  /// firstPageForSourceLine[s] = first page index that shows a wrapped row for source line s, or -1 if none.
  std::vector<int> firstPageForSourceLine;
  bool sentencePageMapValid = false;
  /// When true, auto-advance timer is stopped; Back then exits (finish).
  bool sentenceAutoPaused = false;

  static constexpr int kSentenceIntervalMs = 2500;

  void rebuildSentenceFirstPageMap();
  void applyPageForCurrentSentence();
  void syncSentenceIndexToCurrentPage();
  void resetSentenceAdvanceClock();

 public:
  explicit SentenceUnderlineActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool preventAutoSleep() override { return true; }
  bool isReaderActivity() const override { return false; }
};
