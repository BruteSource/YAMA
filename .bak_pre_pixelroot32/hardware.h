// Shared board wiring + low-level input primitives used by every app.
// See README.md for the full physical wiring reference.
#pragma once
#include <Arduino.h>
#include <Adafruit_ST7789.h>

#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
// SCK (18) and MOSI (23) use the ESP32's default hardware VSPI pins.

#define PIN_ENC_A   13
#define PIN_ENC_B   14
#define PIN_ENC_SW  27
#define PIN_KEY0    26
#define PIN_BTN1    32
#define PIN_BTN2    33
// Piezo buzzer: free GPIO, not a boot-strapping pin. Also the VSPI MISO
// pin, unused since the display is write-only, so no conflict here.
#define PIN_BUZZER  19

// 2-pot analog thumbstick + push switch. X/Y are ADC1 (input-only, no
// WiFi/ADC2 conflict since WiFi is never enabled here); the switch is a
// normal GPIO with an internal pull-up, same pattern as the other buttons.
// Deliberately not D12 (see Gotcha #1). Swapped from the physical silkscreen/
// datasheet-obvious assignment (34=X, 35=Y) — on this specific stick, the
// left/right pot lands on D35 and the up/down pot on D34, confirmed by
// testing in the menu (right/left moved the selection, up/down did nothing
// until this was swapped). Keeping the swap here, in one place, means every
// consumer (menu, R-Type, calibration) can just trust "X = left/right,
// Y = up/down" without each needing its own correction.
#define PIN_THUMB_X   35
#define PIN_THUMB_Y   34
#define PIN_THUMB_SW  25

// `tft` is a pointer to the current draw target: normally the real display
// (tftReal), but temporarily redirected to an offscreen GFXcanvas16 during
// screenshot capture (see screenshot.h) so game code doesn't need to know
// the difference — every draw call just goes through `tft->`.
extern Adafruit_ST7789 tftReal;
extern Adafruit_GFX* tft;
extern volatile long encoderValue;

const int SCREEN_W = 240;
const int SCREEN_H = 320;

struct Button {
  uint8_t pin;
  unsigned long debounceMs = 25;
  bool pressed = false;
  bool lastReading = true;
  unsigned long lastChangeMs = 0;
};

// Polls one debounced button; returns true if `pressed` changed this call.
bool updateButton(Button& b);

// Configures all pins/interrupts and brings up the display. Call once from setup().
void hardwareInit();

// Switches the real display between portrait (used by menu + every game
// except a landscape one) and landscape, 90 degrees clockwise from that
// portrait orientation. Adafruit_GFX's width()/height() (and so tft->width()/
// height()) automatically swap to match. SCREEN_W/SCREEN_H above are NOT
// updated by this — they stay the fixed portrait values every existing game
// was written against; a landscape game must use tft->width()/tft->height()
// (or its own constants) instead.
void hardwareSetOrientation(bool landscape);

// Reads the thumbstick's two analog axes, applying a center deadzone and
// exponential smoothing (the ESP32's ADC is inherently noisy — this isn't a
// pin-choice problem, see README), and writes normalized -1.0..1.0 values
// (0 = centered) to x/y. Center calibration is captured once in
// hardwareInit() assuming the stick is at rest during boot.
void hardwareReadThumbstick(float& x, float& y);
