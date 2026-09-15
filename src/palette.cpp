#include "palette.h"
#include "hardware.h"

uint16_t BG, HUD_LINE, TITLE_COLOR, TEXT_COLOR, WARN_COLOR;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

uint16_t lighten(uint16_t c, int amt) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = constrain(r + amt, 0, 31);
  g = constrain(g + amt * 2, 0, 63);
  b = constrain(b + amt, 0, 31);
  return (r << 11) | (g << 5) | b;
}
uint16_t darken(uint16_t c, int amt) { return lighten(c, -amt); }

void paletteInit() {
  BG          = rgb565(6, 8, 22);
  HUD_LINE    = rgb565(60, 70, 110);
  TITLE_COLOR = rgb565(255, 210, 60);
  TEXT_COLOR  = rgb565(220, 230, 255);
  WARN_COLOR  = rgb565(255, 80, 80);
}

void drawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  int16_t x1, y1; uint16_t w, h;
  tft->setTextSize(size);
  tft->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft->setTextColor(color);
  tft->setCursor((SCREEN_W - (int)w) / 2, y);
  tft->print(s);
}
