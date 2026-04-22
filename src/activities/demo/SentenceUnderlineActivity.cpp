#include "SentenceUnderlineActivity.h"

#include <FontCacheManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>

#include <algorithm>
#include <chrono>
#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

/// Same path as on SD card; simulator maps it to ./fs_/books/ (see scripts/ensure_sentence_demo_book.py).
constexpr const char* kSentenceDemoBookPath = "/books/sentence_demo.txt";
constexpr const char* kCrosspointCacheBase = "/.crosspoint";

std::unique_ptr<Txt> openSentenceDemoBook() {
  if (!Storage.exists(kSentenceDemoBookPath)) {
    LOG_ERR("SND",
            "Missing %s — copy data/sentence_demo.txt from the repo to SD:/books/ (simulator: run a simulator "
            "build so the pre-script populates ./fs_/books/).",
            kSentenceDemoBookPath);
    return nullptr;
  }
  auto txt = std::unique_ptr<Txt>(new Txt(kSentenceDemoBookPath, kCrosspointCacheBase));
  if (!txt->load()) {
    LOG_ERR("SND", "Failed to open %s", kSentenceDemoBookPath);
    return nullptr;
  }
  return txt;
}

uint32_t countSourceLines(const Txt& t) {
  const size_t sz = t.getFileSize();
  if (sz == 0) {
    return 0;
  }
  uint32_t lines = 1;
  constexpr size_t kChunk = 2048;
  std::vector<uint8_t> buf(kChunk);
  for (size_t off = 0; off < sz;) {
    const size_t n = std::min(kChunk, sz - off);
    if (!t.readContent(buf.data(), off, n)) {
      break;
    }
    for (size_t i = 0; i < n; ++i) {
      if (buf[i] == '\n') {
        lines++;
      }
    }
    off += n;
  }
  return lines;
}

}  // namespace

SentenceUnderlineActivity::SentenceUnderlineActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : TxtReaderActivity(renderer, mappedInput, openSentenceDemoBook(), "SentenceUnderline") {}

void SentenceUnderlineActivity::onEnter() {
  Activity::onEnter();

  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  mappedInput.setReaderMode(false);

  if (txt) {
    txt->setupCacheDir();
    totalSourceLines = countSourceLines(*txt);
  } else {
    totalSourceLines = 0;
  }

  lastSentenceTime = std::chrono::steady_clock::now();
  currentSentenceIndex = 0;
  sentenceAutoPaused = false;
  initialized = false;
  sentencePageMapValid = false;
  firstPageForSourceLine.clear();
  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  requestUpdate();
}

void SentenceUnderlineActivity::onExit() {
  TxtReaderActivity::onExit();
}

void SentenceUnderlineActivity::loop() {
  using Btn = MappedInputManager::Button;

  if (mappedInput.wasReleased(Btn::Confirm)) {
    if (mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
      finish();
      return;
    }
    sentenceAutoPaused = !sentenceAutoPaused;
    if (!sentenceAutoPaused) {
      lastSentenceTime = std::chrono::steady_clock::now();
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(Btn::Back)) {
    if (!sentenceAutoPaused) {
      sentenceAutoPaused = true;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(Btn::Left) || mappedInput.wasReleased(Btn::Down)) {
    if (totalSourceLines > 0) {
      currentSentenceIndex = static_cast<uint16_t>(
          (static_cast<uint32_t>(currentSentenceIndex) + totalSourceLines - 1u) % totalSourceLines);
      if (sentencePageMapValid) {
        applyPageForCurrentSentence();
      }
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(Btn::Right) || mappedInput.wasReleased(Btn::Up)) {
    if (totalSourceLines > 0) {
      currentSentenceIndex =
          static_cast<uint16_t>((static_cast<uint32_t>(currentSentenceIndex) + 1u) % totalSourceLines);
      if (sentencePageMapValid) {
        applyPageForCurrentSentence();
      }
      requestUpdate();
    }
    return;
  }

  if (totalSourceLines == 0 || sentenceAutoPaused) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSentenceTime).count();
  if (elapsed >= kSentenceIntervalMs) {
    lastSentenceTime = now;
    currentSentenceIndex =
        static_cast<uint16_t>((static_cast<uint32_t>(currentSentenceIndex) + 1u) % totalSourceLines);
    if (sentencePageMapValid) {
      applyPageForCurrentSentence();
    }
    requestUpdate();
  }
}

void SentenceUnderlineActivity::rebuildSentenceFirstPageMap() {
  firstPageForSourceLine.clear();
  if (!initialized || totalSourceLines == 0 || totalPages <= 0 || pageOffsets.empty()) {
    return;
  }
  firstPageForSourceLine.assign(static_cast<size_t>(totalSourceLines), -1);

  for (int p = 0; p < totalPages; ++p) {
    const size_t offset = pageOffsets[static_cast<size_t>(p)];
    size_t nextOffset = 0;
    uint32_t lineBase =
        (!pageStartSourceLine.empty() && static_cast<size_t>(p) < pageStartSourceLine.size())
            ? pageStartSourceLine[static_cast<size_t>(p)]
            : 0;
    uint32_t lineNext = lineBase;
    std::vector<std::string> lines;
    std::vector<uint16_t> sidx;
    if (!loadPageAtOffset(offset, lines, nextOffset, &lineBase, &lineNext, &sidx)) {
      continue;
    }
    for (uint16_t v : sidx) {
      const size_t si = static_cast<size_t>(v);
      if (si < firstPageForSourceLine.size() && firstPageForSourceLine[si] < 0) {
        firstPageForSourceLine[si] = p;
      }
    }
  }
}

void SentenceUnderlineActivity::applyPageForCurrentSentence() {
  if (firstPageForSourceLine.empty()) {
    return;
  }
  const size_t si = static_cast<size_t>(currentSentenceIndex);
  if (si >= firstPageForSourceLine.size()) {
    return;
  }
  const int fp = firstPageForSourceLine[si];
  if (fp >= 0 && fp < totalPages) {
    currentPage = fp;
  }
}

void SentenceUnderlineActivity::syncSentenceIndexToCurrentPage() {
  if (!initialized || !txt || pageOffsets.empty() || totalSourceLines == 0) {
    return;
  }
  const int p = std::clamp(currentPage, 0, std::max(0, totalPages - 1));
  const size_t offset = pageOffsets[static_cast<size_t>(p)];
  size_t nextOffset = 0;
  uint32_t lineBase =
      (!pageStartSourceLine.empty() && static_cast<size_t>(p) < pageStartSourceLine.size())
          ? pageStartSourceLine[static_cast<size_t>(p)]
          : 0;
  uint32_t lineNext = lineBase;
  std::vector<std::string> lines;
  std::vector<uint16_t> sidx;
  if (!loadPageAtOffset(offset, lines, nextOffset, &lineBase, &lineNext, &sidx) || sidx.empty()) {
    currentSentenceIndex =
        static_cast<uint16_t>(std::min<uint32_t>(lineBase, totalSourceLines > 0 ? totalSourceLines - 1u : 0u));
    return;
  }
  uint16_t mn = sidx[0];
  for (uint16_t v : sidx) {
    mn = std::min(mn, v);
  }
  currentSentenceIndex = mn;
}

void SentenceUnderlineActivity::render(RenderLock&&) {
  if (!txt) {
    return;
  }

  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (currentPage < 0) {
    currentPage = 0;
  }
  if (currentPage >= totalPages) {
    currentPage = totalPages - 1;
  }

  if (!sentencePageMapValid && totalPages > 0 && totalSourceLines > 0) {
    rebuildSentenceFirstPageMap();
    sentencePageMapValid = true;
    applyPageForCurrentSentence();
  }

  const size_t offset = pageOffsets[static_cast<size_t>(currentPage)];
  size_t nextOffset = 0;
  uint32_t lineBase =
      (!pageStartSourceLine.empty() && static_cast<size_t>(currentPage) < pageStartSourceLine.size())
          ? pageStartSourceLine[static_cast<size_t>(currentPage)]
          : 0;
  uint32_t lineNext = lineBase;

  sentenceIndexPerLine.clear();
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset, &lineBase, &lineNext, &sentenceIndexPerLine);

  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();

  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  // Match TxtReaderActivity::renderPage: one drawText per wrapped row (no second word-wrap pass).
  auto drawContent = [&]() {
    int y = cachedOrientedMarginTop;
    for (size_t i = 0; i < currentPageLines.size(); ++i) {
      const std::string& line = currentPageLines[i];
      if (!line.empty()) {
        int x = cachedOrientedMarginLeft;
        switch (cachedParagraphAlignment) {
          case CrossPointSettings::CENTER_ALIGN: {
            const int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = cachedOrientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            const int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = cachedOrientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::LEFT_ALIGN:
          case CrossPointSettings::JUSTIFIED:
          default:
            break;
        }
        renderer.drawText(cachedFontId, x, y, line.c_str());
        if (i < sentenceIndexPerLine.size() && sentenceIndexPerLine[i] == currentSentenceIndex) {
          const int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
          const int underlineY = y + renderer.getFontAscenderSize(cachedFontId) + 2;
          renderer.drawLine(x, underlineY, x + textWidth, underlineY, 3, true);
        }
      }
      y += lineHeight;
    }
  };

  auto scope = fcm->createPrewarmScope();
  drawContent();
  scope.endScanAndPrewarm();

  drawContent();

  renderStatusBar();

  const char* backHint = sentenceAutoPaused ? tr(STR_BACK) : "Pause";
  const char* confirmHint = sentenceAutoPaused ? "Play" : "Pause";
  const auto labels = mappedInput.mapLabels(backHint, confirmHint, "Prev", "Next");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&drawContent]() { drawContent(); });
  }
}
