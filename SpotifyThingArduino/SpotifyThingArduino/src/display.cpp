#include "display.h"
#include <U8g2lib.h>
#include <Wire.h>

static constexpr uint8_t SDA_PIN = 16;
static constexpr uint8_t SCL_PIN = 17;

static constexpr int DISPLAY_WIDTH   = 128;
static constexpr int LINE1_Y        = 12;
static constexpr int LINE2_Y         = 28;
static constexpr uint8_t LINE_COUNT  = 2;
static constexpr int PROGRESS_BAR_Y  = 61;
static constexpr int PROGRESS_BAR_H  = 3;
static constexpr int PAUSE_ICON_X1   = 119;
static constexpr int PAUSE_ICON_X2   = 124;
static constexpr int PAUSE_ICON_Y    = 3;
static constexpr int PAUSE_ICON_W    = 3;
static constexpr int PAUSE_ICON_H    = 8;

static constexpr unsigned int SCROLL_SPEED_MS    = 40;
static constexpr int          SCROLL_PAUSE        = 60;
static constexpr unsigned int PROGRESS_UPDATE_MS  = 500;

static constexpr uint8_t CONTRAST_PLAYING = 200;
static constexpr uint8_t CONTRAST_PAUSED  = 30;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE);

struct LineState {
  String   text;
  int      offset;
  int      pauseTimer;
  bool     atEnd;
};

static LineState     lines[LINE_COUNT];
static bool          isPlaying         = true;
static uint32_t      progressMs        = 0;
static uint32_t      durationMs        = 0;
static unsigned long progressUpdatedAt = 0;
static unsigned long lastScrollMs      = 0;

static uint32_t estimatedProgress() {
  uint32_t estimated = progressMs;
  if (isPlaying) estimated += static_cast<uint32_t>(millis() - progressUpdatedAt);
  return min(estimated, durationMs);
}

static void resetLine(LineState& line, const char* text) {
  line.text       = text ? text : "";
  line.offset     = 0;
  line.pauseTimer = SCROLL_PAUSE;
  line.atEnd      = false;
}

static void redraw() {
  display.clearBuffer();

  if (lines[0].text.length() > 0)
    display.drawStr(lines[0].offset, LINE1_Y, lines[0].text.c_str());
  if (lines[1].text.length() > 0)
    display.drawStr(lines[1].offset, LINE2_Y, lines[1].text.c_str());

  if (!isPlaying) {
    display.drawBox(PAUSE_ICON_X1, PAUSE_ICON_Y, PAUSE_ICON_W, PAUSE_ICON_H);
    display.drawBox(PAUSE_ICON_X2, PAUSE_ICON_Y, PAUSE_ICON_W, PAUSE_ICON_H);
  }

  if (durationMs > 0) {
    int filled = static_cast<int>(DISPLAY_WIDTH * static_cast<long>(estimatedProgress()) / durationMs);
    display.drawFrame(0, PROGRESS_BAR_Y, DISPLAY_WIDTH, PROGRESS_BAR_H);
    if (filled > 0) display.drawBox(0, PROGRESS_BAR_Y, filled, PROGRESS_BAR_H);
  }

  display.sendBuffer();
}

static bool tickScroll() {
  bool changed = false;

  for (uint8_t i = 0; i < LINE_COUNT; i++) {
    if (lines[i].text.length() == 0) continue;

    int textWidth = display.getStrWidth(lines[i].text.c_str());
    if (textWidth <= DISPLAY_WIDTH) continue;

    if (lines[i].pauseTimer > 0) {
      lines[i].pauseTimer--;
      continue;
    }

    if (lines[i].atEnd) {
      lines[i].offset     = 0;
      lines[i].atEnd      = false;
      lines[i].pauseTimer = SCROLL_PAUSE;
      changed = true;
      continue;
    }

    lines[i].offset--;
    changed = true;

    if (-lines[i].offset >= textWidth - DISPLAY_WIDTH) {
      lines[i].atEnd      = true;
      lines[i].pauseTimer = SCROLL_PAUSE;
    }
  }

  return changed;
}

static bool tickProgress() {
  if (durationMs == 0) return false;
  static unsigned long lastMs = 0;
  const unsigned long now = millis();
  if (now - lastMs < PROGRESS_UPDATE_MS) return false;
  lastMs = now;
  return true;
}

void displayInit() {
  Wire.setSDA(SDA_PIN);
  Wire.setSCL(SCL_PIN);
  Wire.begin();
  display.begin();
  display.setFont(u8g2_font_helvB08_tr);
}

void displayMessage(const char* line1, const char* line2) {
  resetLine(lines[0], line1);
  resetLine(lines[1], line2);
  redraw();
}

void displaySetPlaying(bool playing) {
  if (playing == isPlaying) return;
  isPlaying = playing;
  display.setContrast(playing ? CONTRAST_PLAYING : CONTRAST_PAUSED);
  redraw();
}

void displaySetProgress(uint32_t pMs, uint32_t dMs) {
  progressMs        = pMs;
  durationMs        = dMs;
  progressUpdatedAt = millis();
}

void displayTick() {
  const unsigned long now = millis();
  bool changed = false;

  if (now - lastScrollMs >= SCROLL_SPEED_MS) {
    lastScrollMs = now;
    changed |= tickScroll();
  }

  changed |= tickProgress();

  if (changed) redraw();
}
