#include "display.h"
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>

static constexpr uint8_t SDA_PIN = 16;
static constexpr uint8_t SCL_PIN = 17;

static constexpr int DISPLAY_WIDTH_PX = 128;
static constexpr int LINE1_Y = 12;
static constexpr int LINE2_Y = 28;
static constexpr uint8_t LINE_COUNT = 2;
static constexpr int PROGRESS_BAR_Y = 61;
static constexpr int PROGRESS_BAR_HEIGHT_PX = 3;
static constexpr int PAUSE_ICON_X1 = 119;
static constexpr int PAUSE_ICON_X2 = 124;
static constexpr int PAUSE_ICON_Y = 3;
static constexpr int PAUSE_ICON_WIDTH_PX = 3;
static constexpr int PAUSE_ICON_HEIGHT_PX = 8;

static constexpr unsigned int SCROLL_SPEED_MS = 40;
static constexpr int SCROLL_PAUSE_MS = 60;
static constexpr unsigned int PROGRESS_UPDATE_MS = 500;
static constexpr unsigned int CLOCK_UPDATE_MS = 30000;

static constexpr uint8_t CONTRAST_PLAYING = 200;
static constexpr uint8_t CONTRAST_PAUSED = 30;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE);

struct LineState
{
  String text;
  int offset;
  int pauseTimer;
  bool atEnd;
};

static LineState lines[LINE_COUNT];
static bool isPlaying = true;
static uint32_t songProgressMs = 0;
static uint32_t songDurationMs = 0;
static unsigned long progressUpdatedAt = 0;
static unsigned long lastScrollTimeMs = 0;
static bool clockMode = false;

static uint32_t estimatedProgress()
{
  uint32_t estimated = songProgressMs;
  if (isPlaying)
    estimated += static_cast<uint32_t>(millis() - progressUpdatedAt);
  return min(estimated, songDurationMs);
}

static void resetLine(LineState &line, const char *text)
{
  line.text = text ? text : "";
  line.offset = 0;
  line.pauseTimer = SCROLL_PAUSE_MS;
  line.atEnd = false;
}

static void drawClock()
{
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  String timeStr = (t->tm_hour < 10 ? "0" : "") + String(t->tm_hour) + ":" +
                   (t->tm_min < 10 ? "0" : "") + String(t->tm_min);

  display.setFont(u8g2_font_helvB18_tr);
  int w = display.getStrWidth(timeStr.c_str());
  display.drawStr((DISPLAY_WIDTH_PX - w) / 2, 40, timeStr.c_str());
  display.setFont(u8g2_font_helvB08_tr);
}
static void drawPauseIcon()
{
  display.drawBox(PAUSE_ICON_X1, PAUSE_ICON_Y, PAUSE_ICON_WIDTH_PX, PAUSE_ICON_HEIGHT_PX);
  display.drawBox(PAUSE_ICON_X2, PAUSE_ICON_Y, PAUSE_ICON_WIDTH_PX, PAUSE_ICON_HEIGHT_PX);
}

static void drawProgressBar()
{
  int filled = static_cast<int>(DISPLAY_WIDTH_PX * static_cast<long>(estimatedProgress()) / songDurationMs);
  display.drawFrame(0, PROGRESS_BAR_Y, DISPLAY_WIDTH_PX, PROGRESS_BAR_HEIGHT_PX);
  if (filled > 0)
    display.drawBox(0, PROGRESS_BAR_Y, filled, PROGRESS_BAR_HEIGHT_PX);
}

static void drawSongInfo()
{
  if (lines[0].text.length() > 0)
    display.drawStr(lines[0].offset, LINE1_Y, lines[0].text.c_str());
  if (lines[1].text.length() > 0)
    display.drawStr(lines[1].offset, LINE2_Y, lines[1].text.c_str());
}

static void redraw()
{
  display.clearBuffer();

  if (clockMode)
  {
    drawClock();
  }
  else
  {
    drawSongInfo();

    if (!isPlaying)
    {
      drawPauseIcon();
    }

    if (songDurationMs > 0)
    {
      drawProgressBar();
    }
  }

  display.sendBuffer();
}

static bool scrollTick()
{
  bool textDisplayChanged = false;

  for (uint8_t i = 0; i < LINE_COUNT; i++)
  {
    bool lineHasText = lines[i].text.length() > 0;
    if (!lineHasText) 
    {
      continue;
    }
      
    int textWidth = display.getStrWidth(lines[i].text.c_str());
    bool needsScroll = textWidth > DISPLAY_WIDTH_PX;
    if (!needsScroll)
      continue;

    bool lineIsPaused = lines[i].pauseTimer > 0;
    if (lineIsPaused)
    {
      lines[i].pauseTimer--;
      continue;
    }

    if (lines[i].atEnd)
    {
      lines[i].offset = 0;
      lines[i].atEnd = false;
      lines[i].pauseTimer = SCROLL_PAUSE_MS;
      textDisplayChanged = true;
      continue;
    }

    lines[i].offset--;
    textDisplayChanged = true;

    if (-lines[i].offset >= textWidth - DISPLAY_WIDTH_PX)
    {
      lines[i].atEnd = true;
      lines[i].pauseTimer = SCROLL_PAUSE_MS;
    }
  }

  return textDisplayChanged;
}

static bool progressBarTick()
{
  if (songDurationMs == 0)
  {
    return false;
  }

  static unsigned long lastProgressMs = 0;
  const unsigned long now = millis();
  if (now - lastProgressMs < PROGRESS_UPDATE_MS)
    return false;
  lastProgressMs = now;
  return true;
}

static bool clockTick()
{
  if (!clockMode)
  {
    return false;
  }

  static unsigned long lastUpdateMs = 0;
  const unsigned long now = millis();
  if (now - lastUpdateMs < CLOCK_UPDATE_MS)
    return false;
  lastUpdateMs = now;
  return true;
}

void displayInit()
{
  Wire.setSDA(SDA_PIN);
  Wire.setSCL(SCL_PIN);
  Wire.begin();
  display.begin();
  display.setFont(u8g2_font_helvB08_tr);
}

void displayEnterClockMode()
{
  clockMode = true;
  display.setContrast(CONTRAST_PAUSED);
  redraw();
}

void displayMessage(const char *line1, const char *line2)
{
  clockMode = false;
  display.setContrast(isPlaying ? CONTRAST_PLAYING : CONTRAST_PAUSED);
  resetLine(lines[0], line1);
  resetLine(lines[1], line2);
  redraw();
}

void displaySetPlaying(bool playing)
{
  if (playing == isPlaying)
    return;
  isPlaying = playing;
  display.setContrast(playing ? CONTRAST_PLAYING : CONTRAST_PAUSED);
  redraw();
}

void displaySetProgress(uint32_t pMs, uint32_t dMs)
{
  songProgressMs = pMs;
  songDurationMs = dMs;
  progressUpdatedAt = millis();
}

void displayTick()
{
  const unsigned long now = millis();
  bool needsRedraw = false;

  if (now - lastScrollTimeMs >= SCROLL_SPEED_MS)
  {
    lastScrollTimeMs = now;
    needsRedraw |= scrollTick();
  }

  needsRedraw |= progressBarTick();
  needsRedraw |= clockTick();

  if (needsRedraw)
  {
    redraw();
  }
}
