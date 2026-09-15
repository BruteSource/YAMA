// ESP32 game console: shared hardware/input/menu shell. Each game lives in
// its own <name>.h/.cpp (see arkanoid.cpp, tetris.cpp) and is registered in
// games_registry.cpp. KEY0 always returns to the game-select menu.
// See README.md for the wiring reference.

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "hardware.h"
#include "palette.h"
#include "input.h"
#include "apps.h"
#include "menu.h"
#include "sound.h"

namespace {
  // Full-resolution but 1 byte/pixel (RGB332) canvas for screenshot capture.
  // A real GFXcanvas16 (2 bytes/pixel, 153600 bytes total) failed to
  // allocate here: ESP.getFreeHeap() reports ~317KB free, but
  // ESP.getMaxAllocHeap() (largest single contiguous block) is only
  // ~110KB — a platform-level heap fragmentation ceiling, not something
  // caused by this sketch. 240x320x1 byte = 76800 bytes fits well under
  // that. Adafruit_GFX only requires drawPixel() to be implemented; every
  // higher-level shape call (fillRect, fillCircle, print, ...) already has
  // a default implementation built on top of it, so this is correct (if
  // not fast) for every draw call the games make — fine for a one-off
  // debug capture.
  class Canvas8 : public Adafruit_GFX {
   public:
    Canvas8(int16_t w, int16_t h) : Adafruit_GFX(w, h) {
      buffer = (uint8_t*)calloc((size_t)w * h, 1);
    }
    ~Canvas8() { if (buffer) free(buffer); }
    uint8_t* getBuffer() const { return buffer; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
      if (!buffer || x < 0 || x >= _width || y < 0 || y >= _height) return;
      uint8_t r3 = (uint8_t)((color >> 13) & 0x07);
      uint8_t g3 = (uint8_t)((color >> 8) & 0x07);
      uint8_t b2 = (uint8_t)((color >> 3) & 0x03);
      buffer[(int32_t)y * _width + x] = (uint8_t)((r3 << 5) | (g3 << 2) | b2);
    }
   private:
    uint8_t* buffer;
  };

  Button encSw = { PIN_ENC_SW };
  Button key0  = { PIN_KEY0 };
  Button btn1  = { PIN_BTN1 };
  Button btn2  = { PIN_BTN2 };
  Button thumbSw = { PIN_THUMB_SW };

  long lastEncoderValue = 0;
  unsigned long lastFrameMs = 0;
  bool screenshotRequested = false;

  // Debug-only "held" latches: real hardware buttons stay pressed across
  // many frames on their own, but a single injected debug character only
  // asserts for the one frame it's read on. Charge-shot/pause-hold logic
  // (R-Type) needs a *sustained* multi-frame hold to test remotely, so
  // uppercase 'S'/'W' toggle a latch that forces Held true every frame
  // until toggled off again — lowercase stays the original momentary tap.
  bool debugKey0Latch = false;
  bool debugEncSwLatch = false;
  bool debugThumbSwLatch = false;

  // Debug-only: force an immediate, deterministic return to the menu
  // (selection reset to index 0) regardless of current state, WITHOUT
  // waiting for the global L3 2-second hold. Added because remote test
  // scripts need a reliable way to reach a known state — opening a new
  // serial connection resets the board via DTR/RTS *inconsistently*
  // (depends on the line state the previous connection left it in, not
  // something a test script can control), so it can't be used as a sync
  // point. This gives remote testing a real one, independent of that.
  bool forceMenuRequested = false;

  // Global "L3" (thumbstick push-switch) held 2 full seconds = force back
  // to the menu, from ANY game/state — independent of each game's own
  // wantsExit/pause logic. A universal escape hatch in case a game-specific
  // one is confusing or a game logic bug ever strands a future game.
  unsigned long thumbSwHeldSinceMs = 0;
  bool thumbSwExitLatched = false;
  const unsigned long THUMB_SW_EXIT_HOLD_MS = 2000;

  enum TopState { AT_MENU, AT_GAME };
  TopState topState = AT_MENU;
  int currentGame = -1;

  InputState readInput() {
    InputState in;

    noInterrupts();
    long enc = encoderValue;
    interrupts();
    in.encDelta = enc - lastEncoderValue;
    lastEncoderValue = enc;

    bool encSwChanged = updateButton(encSw);
    in.encSwHeld = encSw.pressed;
    in.encSwEdge = encSwChanged && encSw.pressed;

    bool key0Changed = updateButton(key0);
    in.key0Held = key0.pressed;
    in.key0Edge = key0Changed && key0.pressed;

    bool btn1Changed = updateButton(btn1);
    in.btn1Held = btn1.pressed;
    in.btn1Edge = btn1Changed && btn1.pressed;

    bool btn2Changed = updateButton(btn2);
    in.btn2Held = btn2.pressed;
    in.btn2Edge = btn2Changed && btn2.pressed;

    bool thumbSwChanged = updateButton(thumbSw);
    in.thumbSwHeld = thumbSw.pressed;
    in.thumbSwEdge = thumbSwChanged && thumbSw.pressed;
    hardwareReadThumbstick(in.thumbX, in.thumbY);

    // Debug input injection over Serial, for driving/testing the console
    // without touching the physical controls: a/d = encoder left/right one
    // tick, w = ENC SW, s = KEY0 (menu), 1/2 = BTN1/BTN2, l = thumbstick
    // click ("L3"). Purely additive — no effect when nothing is sent, so
    // normal hardware use is unaffected. Uppercase 'S'/'W'/'L' toggle a
    // sustained-hold latch (see debugKey0Latch above) instead of a
    // one-frame tap — needed to remotely test hold-duration mechanics like
    // R-Type's charge shot/pause, or the global L3 2-second exit below.
    while (Serial.available()) {
      char c = Serial.read();
      switch (c) {
        case 'a': in.encDelta -= 1; break;
        case 'd': in.encDelta += 1; break;
        case 'w': in.encSwEdge = true; in.encSwHeld = true; break;
        case 's': in.key0Edge = true; in.key0Held = true; break;
        case '1': in.btn1Edge = true; in.btn1Held = true; break;
        case '2': in.btn2Edge = true; in.btn2Held = true; break;
        case 'p': screenshotRequested = true; break;
        case 'l': in.thumbSwEdge = true; in.thumbSwHeld = true; break;
        case 'S': debugKey0Latch = !debugKey0Latch; break;
        case 'W': debugEncSwLatch = !debugEncSwLatch; break;
        case 'L': debugThumbSwLatch = !debugThumbSwLatch; break;
        case 'm': forceMenuRequested = true; break;
        default: break;
      }
    }
    if (debugKey0Latch) in.key0Held = true;
    if (debugEncSwLatch) in.encSwHeld = true;
    if (debugThumbSwLatch) in.thumbSwHeld = true;

    return in;
  }

  // Renders the currently-visible screen into a temporary offscreen canvas
  // (never touching the real display) and dumps it raw over Serial so it
  // can be reassembled into an image on the host — this display has no
  // MISO/readback wire, so this is the only way to see what's on it
  // remotely. `tft` is a pointer specifically so this redirection works
  // without every draw call needing to know about it.
  void captureScreenshot() {
    // Use the real display's CURRENT dimensions, not the fixed portrait
    // SCREEN_W/SCREEN_H — a landscape game (see hardwareSetOrientation)
    // leaves tftReal reporting 320x240 instead, and Canvas8 needs to match
    // whatever the active game is actually drawing into.
    int w = tftReal.width();
    int h = tftReal.height();
    Canvas8 canvas(w, h);
    uint8_t* buf = canvas.getBuffer();
    if (!buf) {
      Serial.println("SCR_FAIL alloc");
      return;
    }

    Adafruit_GFX* saved = tft;
    tft = &canvas;
    if (topState == AT_MENU) menuRenderSnapshot();
    else GAMES[currentGame].renderSnapshot();
    tft = saved;

    Serial.printf("SCR_BEGIN %d %d %lu RGB332\n", w, h, (unsigned long)(w * h));
    Serial.write(buf, w * h);
    Serial.println();
    Serial.println("SCR_END");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32 Game Console - starting");

  hardwareInit();
  paletteInit();
  soundInit();

  // Every input has shown live electrical chatter at one point or another
  // (KEY0/ENC SW, then BTN1, then ENC SW/BTN2 again) — never all at once,
  // at different times, with no single wiring fix sticking. 150ms was a
  // heavy-handed stopgap that itself started costing real gameplay
  // responsiveness (missed quick taps/rotates). Pulled back a good bit per
  // user request — user's working theory is the chatter may only happen
  // while USB-connected to a PC (electrical noise from that connection),
  // not a standing hardware fault, and hasn't caused a problem during
  // actual untethered gameplay so far. Testing at this lower value again
  // tomorrow; revisit upward only if chatter actually reappears in play.
  key0.debounceMs = 40;
  encSw.debounceMs = 40;
  btn1.debounceMs = 40;
  btn2.debounceMs = 40;

  lastFrameMs = millis();
  lastEncoderValue = encoderValue;

  menuInit();
  Serial.println("Ready.");
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrameMs) / 1000.0f;
  lastFrameMs = now;
  if (dt > 0.05f) dt = 0.05f;

  InputState in = readInput();

  if (screenshotRequested) {
    screenshotRequested = false;
    captureScreenshot();
  }

  if (forceMenuRequested) {
    forceMenuRequested = false;
    if (topState == AT_GAME) {
      if (GAMES[currentGame].onExit) GAMES[currentGame].onExit();
      hardwareSetOrientation(false);
    }
    topState = AT_MENU;
    currentGame = -1;
    menuInit();
    thumbSwHeldSinceMs = 0;
    thumbSwExitLatched = false;
    Serial.println("Forced to menu");
    return;
  }

  // Global "L3" (thumbstick click) held 2s: force back to the menu no
  // matter what the active game/state is, bypassing its own wantsExit
  // entirely. Tracked unconditionally (not just while AT_GAME) so the
  // hold-timer and latch always reset cleanly on release, regardless of
  // what state that release happens in — otherwise a stale timestamp from
  // a previous hold could cause a false-positive trigger later.
  if (in.thumbSwHeld) {
    if (thumbSwHeldSinceMs == 0) thumbSwHeldSinceMs = now;
    if (!thumbSwExitLatched && now - thumbSwHeldSinceMs >= THUMB_SW_EXIT_HOLD_MS) {
      thumbSwExitLatched = true;
    }
  } else {
    thumbSwHeldSinceMs = 0;
    thumbSwExitLatched = false;
  }

  if (topState == AT_GAME) {
    bool wantExit = thumbSwExitLatched ||
                     (GAMES[currentGame].wantsExit ? GAMES[currentGame].wantsExit(in) : in.key0Edge);
    if (wantExit) {
      Serial.println("Back to menu");
      if (GAMES[currentGame].onExit) GAMES[currentGame].onExit();
      topState = AT_MENU;
      currentGame = -1;
      hardwareSetOrientation(false);
      menuInit();
      delay(2);
      return;
    }
  }

  if (topState == AT_MENU) {
    int sel = menuUpdate(in);
    if (sel >= 0) {
      currentGame = sel;
      Serial.printf("Launching %s\n", GAMES[sel].name);
      hardwareSetOrientation(GAMES[sel].landscape);
      GAMES[sel].init();
      topState = AT_GAME;
    }
  } else {
    GAMES[currentGame].update(in, dt);
  }

  delay(2);
}
