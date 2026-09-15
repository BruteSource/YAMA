// Shared "console" look — background/text/title colors used by the menu and
// available to every game. Games are free to define extra colors of their own.
#pragma once
#include <Arduino.h>

extern uint16_t BG, HUD_LINE, TITLE_COLOR, TEXT_COLOR, WARN_COLOR;

// Standalone RGB888->RGB565 packer (what Adafruit_SPITFT::color565 does) —
// a free function rather than a `tft` method so it also works when `tft`
// is pointed at a plain Adafruit_GFX canvas (e.g. for screenshot capture),
// which doesn't have color565.
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

uint16_t lighten(uint16_t c, int amt);
uint16_t darken(uint16_t c, int amt);

void paletteInit();

void drawCentered(const char* s, int y, uint8_t size, uint16_t color);
