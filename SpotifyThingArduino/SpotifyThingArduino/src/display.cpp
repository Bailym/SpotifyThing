#include "display.h"
#include <U8g2lib.h>
#include <Wire.h>

static const int DISPLAY_WIDTH   = 128;
static const int SCROLL_SPEED_MS = 40;
static const int SCROLL_PAUSE    = 60;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE);

struct LineState {
  String text;
  int offset;
  int pauseTimer;
  bool atEnd;
};

static LineState lines[2];
static unsigned long lastScrollMs = 0;

static void redraw() {
  display.clearBuffer();
  if (lines[0].text.length() > 0)
    display.drawStr(lines[0].offset, 12, lines[0].text.c_str());
  if (lines[1].text.length() > 0)
    display.drawStr(lines[1].offset, 28, lines[1].text.c_str());
  display.sendBuffer();
}

void displayInit() {
  Wire.setSDA(16);
  Wire.setSCL(17);
  Wire.begin();
  display.begin();
  display.setFont(u8g2_font_helvB08_tr);
}

void displayMessage(const char* line1, const char* line2) {
  lines[0].text       = line1 ? line1 : "";
  lines[0].offset     = 0;
  lines[0].pauseTimer = SCROLL_PAUSE;
  lines[0].atEnd      = false;

  lines[1].text       = line2 ? line2 : "";
  lines[1].offset     = 0;
  lines[1].pauseTimer = SCROLL_PAUSE;
  lines[1].atEnd      = false;

  redraw();
}

void displayTick() {
  if (millis() - lastScrollMs < SCROLL_SPEED_MS) return;
  lastScrollMs = millis();

  bool changed = false;

  for (int i = 0; i < 2; i++) {
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

  if (changed) redraw();
}
