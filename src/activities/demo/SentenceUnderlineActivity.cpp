#include "SentenceUnderlineActivity.h"

#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>

#include <chrono>
#include <cctype>
#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr const char* kDemoTxtPath = "/.crosspoint/SentenceDemo.txt";

/// One logical line per newline. If the translation has no newlines, split after ". " / "? " / "! " so
/// source-line indices advance (underline can cycle) without requiring every locale to use YAML blocks.
std::string normalizeDemoBodyNewlines(std::string body) {
  if (body.find('\n') != std::string::npos) {
    return body;
  }
  for (size_t i = 0; i + 1 < body.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(body[i]);
    if ((c == '.' || c == '?' || c == '!') && body[i + 1] == ' ') {
      body[i + 1] = '\n';
    }
  }
  return body;
}

std::unique_ptr<Txt> createSentenceDemoTxt() {
  if (!Storage.exists("/.crosspoint")) {
    Storage.mkdir("/.crosspoint");
  }
  FsFile f;
  if (!Storage.openFileForWrite("SND", kDemoTxtPath, f)) {
    return nullptr;
  }
  const std::string body = normalizeDemoBodyNewlines(tr(STR_SENTENCE_DEMO_BODY));
  if (!body.empty()) {
    f.write(body.data(), body.size());
  }
  f.close();

  auto txt = std::unique_ptr<Txt>(new Txt(kDemoTxtPath, "/.crosspoint"));
  if (!txt->load()) {
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
    : TxtReaderActivity(renderer, mappedInput, createSentenceDemoTxt(), "SentenceUnderline") {}

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
  initialized = false;
  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  requestUpdate();
}

void SentenceUnderlineActivity::onExit() {
  TxtReaderActivity::onExit();
}

void SentenceUnderlineActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
    return;
  }
  if (nextTriggered && currentPage < totalPages - 1) {
    currentPage++;
    requestUpdate();
    return;
  }

  if (totalSourceLines == 0) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSentenceTime).count();
  if (elapsed >= kSentenceIntervalMs) {
    lastSentenceTime = now;
    currentSentenceIndex =
        static_cast<uint16_t>((static_cast<uint32_t>(currentSentenceIndex) + 1u) % totalSourceLines);
    requestUpdate();
  }
}

void SentenceUnderlineActivity::rebuildLaidWords() {
  laidWords.clear();
  if (currentPageLines.empty() || sentenceIndexPerLine.size() != currentPageLines.size()) {
    return;
  }

  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;
  const int bottomLimit = renderer.getScreenHeight() - cachedOrientedMarginBottom;
  int y = cachedOrientedMarginTop;

  for (size_t idx = 0; idx < currentPageLines.size(); ++idx) {
    const std::string& line = currentPageLines[idx];
    const uint16_t sidx = sentenceIndexPerLine[idx];

    if (y + lineHeight > bottomLimit) {
      break;
    }

    if (line.empty()) {
      y += lineHeight;
      continue;
    }

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

    size_t j = 0;
    while (j < line.size()) {
      while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) {
        j++;
      }
      if (j >= line.size()) {
        break;
      }
      size_t k = j;
      while (k < line.size() && !std::isspace(static_cast<unsigned char>(line[k]))) {
        k++;
      }
      const std::string word = line.substr(j, k - j);
      const int wordW = renderer.getTextWidth(cachedFontId, word.c_str());
      const int spaceW = renderer.getSpaceWidth(cachedFontId, EpdFontFamily::REGULAR);
      int advance = wordW;
      if (x > cachedOrientedMarginLeft) {
        advance += spaceW;
      }
      if (x > cachedOrientedMarginLeft && x + advance > cachedOrientedMarginLeft + contentWidth) {
        x = cachedOrientedMarginLeft;
        y += lineHeight;
        if (y + lineHeight > bottomLimit) {
          return;
        }
      }
      if (x > cachedOrientedMarginLeft) {
        x += spaceW;
      }
      laidWords.push_back(LaidWord{word, x, y, sidx});
      x += wordW;
      j = k;
    }
    y += lineHeight;
  }
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
  rebuildLaidWords();

  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  fcm->resetStats();

  auto drawContent = [&]() {
    for (const auto& w : laidWords) {
      renderer.drawText(cachedFontId, w.x, w.y, w.text.c_str(), true, EpdFontFamily::REGULAR);
    }
    for (const auto& w : laidWords) {
      if (w.sentenceIdx != currentSentenceIndex) {
        continue;
      }
      const int fullWordWidth = renderer.getTextWidth(cachedFontId, w.text.c_str(), EpdFontFamily::REGULAR);
      const int underlineY = w.y + renderer.getFontAscenderSize(cachedFontId) + 2;
      renderer.drawLine(w.x, underlineY, w.x + fullWordWidth, underlineY, 3, true);
    }
  };

  auto scope = fcm->createPrewarmScope();
  drawContent();
  scope.endScanAndPrewarm();

  drawContent();

  renderStatusBar();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&drawContent]() { drawContent(); });
  }
}
