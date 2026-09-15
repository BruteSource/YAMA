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
#define PIN_R3      27  // encoder push switch
#define PIN_A       26  // KEY0 — the display module's own onboard button,
                         // physically silkscreened "KEY0" on the board
#define PIN_RB      32  // hand-wired "fire/confirm" button
#define PIN_LB      33  // hand-wired second button
// Piezo buzzer: free GPIO, not a boot-strapping pin. Also the VSPI MISO
// pin — but TFT_eSPI only claims MISO when TFT_MISO is defined (it falls
// back to -1/unclaimed otherwise — confirmed by reading TFT_eSPI.cpp's
// own init sequence), and this project never defines TFT_MISO, so
// there's genuinely no conflict with the display/SPI bus here (an
// earlier theory that engine.init() was reclaiming this pin turned out
// to be wrong once actually checked against TFT_eSPI's source).
#define PIN_BUZZER  19
// Backlight power switch, added post-launch: TFT BLK used to be wired
// direct to 3V3 (always on, no software control — see README.md). Now
// runs through an S8550 PNP high-side switch (emitter->3V3,
// collector->BLK, base->this GPIO through a 1k series resistor plus a
// 10k pull-up to 3V3 holding it off by default before firmware
// configures the pin). Logic is INVERTED because of the high-side PNP
// topology: LOW turns the backlight on, HIGH (or an unconfigured/
// floating pin) turns it off. Always go through
// hardwareSetBacklight() (hardware.cpp) rather than digitalWrite()
// directly so that inversion lives in exactly one place.
#define PIN_BACKLIGHT 21
//
// Driven via the Arduino core's own tone()/noTone() (see
// buzzer_audio.h) — NOT raw ledcSetup/ledcAttachPin/ledcWriteTone. Two
// from-scratch raw-LEDC attempts this project has tried (mechanically
// equivalent to what tone()/noTone() do internally) have BOTH produced
// no audible output at all for a reason never pinned down, most
// recently confirmed live this session. Plain tone()/noTone() (2-arg
// form only, see buzzer_audio.h's own top comment for why no duration
// is ever passed) is the only combination CONFIRMED by the user to
// produce real, reliable sound on this board — don't replace it without
// a live audible confirmation, no matter how good a theoretical fix
// looks in code. (That core's own noTone() also logs a harmless-looking
// but very verbose "LEDC is not initialized" line on every call — see
// buzzer_audio.h's own comment on why that's being left alone as a
// separate, lower-priority cosmetic issue rather than risking sound
// again to silence it.)

// Global mute — every scene's sound goes through buzzer_audio.h's
// buzzerTone()/buzzerNoTone() (never raw tone()/noTone() directly), and
// those check this flag first. Loaded from NVS at boot and kept in sync
// by hardwareSetMuted() (below) — don't assign this directly for a
// real user-facing toggle, or the change won't survive a reset.
extern bool audioMuted;

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
#define PIN_L3        25  // thumbstick's integrated push switch

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

// Configures input pins/interrupts/thumbstick calibration only — does NOT
// touch the display. Call once from setup(). Split out from display
// bring-up (hardwareInitDisplay()) so a build that renders through a
// different display pipeline entirely (see PixelRoot32 integration,
// src/main.cpp) can reuse this without ever touching tftReal/Adafruit_ST7789.
void hardwareInit();

// Sets backlight brightness via hardware PWM (LEDC) through the S8550
// high-side switch on PIN_BACKLIGHT (see that #define's comment for the
// wiring/logic-inversion details). 0 = fully off, 255 = fully on.
// hardwareSetBacklight() is a thin on/off convenience wrapper around
// this. Always go through one of these two instead of driving the pin
// directly. Defaults to full brightness, set at the end of
// hardwareInit().
void hardwareSetBrightness(uint8_t level);
inline void hardwareSetBacklight(bool on) { hardwareSetBrightness(on ? 255 : 0); }
// Last level passed to hardwareSetBrightness() — lets a caller (the
// idle-sleep code in main.cpp) save the level before forcing the
// backlight off and restore exactly that level on wake, rather than
// assuming full brightness.
uint8_t hardwareGetBrightness();
// Persists the CURRENT level (hardwareGetBrightness()) to NVS. Call
// this once when the user finishes an adjustment (e.g. the menu's
// brightness control returning to center), not on every frame while
// adjusting — NVS writes are wear-limited.
void hardwareSaveBrightness();

// Sets `audioMuted` and persists it to NVS in one call — use this
// instead of assigning `audioMuted` directly whenever the change should
// survive a reset/power-cycle (i.e. any real user-facing mute toggle).
void hardwareSetMuted(bool muted);

// Brings up tftReal (Adafruit_ST7789) — separate from hardwareInit() so a
// build that doesn't use this display pipeline never calls it, avoiding two
// display-driver libraries fighting over the same physical SPI pins.
void hardwareInitDisplay();

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

// Per-frame input snapshot fed to whichever PixelRoot32 Scene is
// currently active (menu, Bubble Bobble, Pac-Man, ...). Originally
// declared private to bubblebobble_pr32.h back when this project only
// built one game; moved here once a menu scene and a second game needed
// the identical shape, since it was already 100% game-agnostic (built
// from this file's own Button/updateButton()/hardwareReadThumbstick(),
// plus main.cpp's serial debug-injection switch — nothing here is
// Bubble-Bobble-specific). Not read from PixelRoot32's own InputManager
// (see main.cpp's readInput() for why: no encoder/analog-axis support
// there, and no way for the serial debug-injection protocol to fake a
// press through it).
struct Pr32Input {
    long encDelta = 0;
    bool aEdge = false;
    bool aHeld = false;
    bool rbEdge = false;
    bool rbHeld = false;
    bool lbEdge = false;
    bool lbHeld = false;
    bool r3Edge = false;
    bool r3Held = false;
    float thumbX = 0.0f;
    float thumbY = 0.0f;
};
