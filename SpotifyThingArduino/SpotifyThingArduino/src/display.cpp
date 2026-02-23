#include "display.h"
#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R2, U8X8_PIN_NONE);

void displayInit() {
  Wire.setSDA(16);
  Wire.setSCL(17);
  Wire.begin();
  display.begin();
  display.setFont(u8g2_font_helvB08_tr);
}

void displayMessage(const char* line1, const char* line2) {
  display.clearBuffer();
  display.drawStr(0, 12, line1);
  if (line2) display.drawStr(0, 28, line2);
  display.sendBuffer();
}
