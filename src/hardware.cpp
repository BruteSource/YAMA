#include "hardware.h"
#include <Preferences.h>

Adafruit_ST7789 tftReal = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_GFX* tft = &tftReal;
volatile long encoderValue = 0;
bool audioMuted = false;

namespace {
  // Full-step quadrature decode table, indexed by (prevAB<<2)|curAB.
  const int8_t ENC_TABLE[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
  };
  volatile uint8_t encPrevAB = 0;

  void IRAM_ATTR onEncoderChange() {
    uint8_t a = digitalRead(PIN_ENC_A);
    uint8_t b = digitalRead(PIN_ENC_B);
    uint8_t curAB = (a << 1) | b;
    uint8_t idx = (encPrevAB << 2) | curAB;
    encoderValue += ENC_TABLE[idx];
    encPrevAB = curAB;
  }

  int thumbCenterX = 2048, thumbCenterY = 2048;
  float thumbSmoothX = 0.0f, thumbSmoothY = 0.0f;
  const float THUMB_DEADZONE = 0.10f;
  const float THUMB_SMOOTH_ALPHA = 0.35f;

  float normalizeThumbAxis(int raw, int center) {
    float v = (raw - center) / 2047.0f;
    v = constrain(v, -1.0f, 1.0f);
    if (fabsf(v) < THUMB_DEADZONE) v = 0.0f;
    return v;
  }
}

namespace {
  // Arbitrary, unused LEDC channel — arduino-esp32 2.x (this project is
  // pinned to platform espressif32@6.9.0, see platformio.ini) uses the
  // channel-based ledcSetup()/ledcAttachPin()/ledcWrite() API, separate
  // from whatever channel the Arduino core's own tone()/noTone()
  // internally claims for the buzzer (see buzzer_audio.h) — no overlap
  // since that's a different pin entirely.
  constexpr int kBacklightLedcChannel = 4;
  constexpr int kBacklightLedcFreqHz = 1000;
  constexpr int kBacklightLedcResolutionBits = 8;

  uint8_t currentBrightnessLevel = 255;
  // Namespace "pr32" shared across every persisted setting (muted,
  // brightness) — same Preferences library already used by the
  // dormant old-games code (bubblebobble.cpp etc.), just newly wired
  // into the active PixelRoot32 build.
  Preferences prefs;
}

void hardwareSetBrightness(uint8_t level) {
  // Inverted: the S8550 is a high-side PNP switch (see PIN_BACKLIGHT's
  // comment in hardware.h) — the pin spending more time LOW is what
  // turns the backlight (more) on, so the duty cycle written here is
  // the complement of the requested brightness.
  currentBrightnessLevel = level;
  ledcWrite(kBacklightLedcChannel, 255 - level);
}

uint8_t hardwareGetBrightness() {
  return currentBrightnessLevel;
}

void hardwareSaveBrightness() {
  prefs.begin("pr32", false);
  prefs.putUChar("bright", currentBrightnessLevel);
  prefs.end();
}

void hardwareSetMuted(bool muted) {
  audioMuted = muted;
  prefs.begin("pr32", false);
  prefs.putBool("muted", muted);
  prefs.end();
}

bool updateButton(Button& b) {
  bool reading = digitalRead(b.pin);
  bool changed = false;
  if (reading != b.lastReading) {
    b.lastChangeMs = millis();
    b.lastReading = reading;
  }
  if ((millis() - b.lastChangeMs) > b.debounceMs) {
    bool nowPressed = (reading == LOW);
    if (nowPressed != b.pressed) {
      b.pressed = nowPressed;
      changed = true;
    }
  }
  return changed;
}

void hardwareInit() {
  // This project never calls into WiFi or Bluetooth APIs, so both radios
  // stay uninitialized/powered-down by default — nothing to explicitly
  // disable here. (btStop() was tried and caused a boot-loop crash since
  // BT was never started in the first place; WiFi.mode(WIFI_OFF) was also
  // tried and cost ~600KB of flash pulling in the WiFi/TLS stack just to
  // say "off." Simplest and safest is to just not touch either.)

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_R3, INPUT_PULLUP);
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_RB, INPUT_PULLUP);
  pinMode(PIN_LB, INPUT_PULLUP);
  pinMode(PIN_L3, INPUT_PULLUP);

  ledcSetup(kBacklightLedcChannel, kBacklightLedcFreqHz, kBacklightLedcResolutionBits);
  ledcAttachPin(PIN_BACKLIGHT, kBacklightLedcChannel);

  // Load persisted mute/brightness settings (read-only open) — defaults
  // match this project's pre-persistence behavior (unmuted, full
  // brightness) the first time either key doesn't exist yet.
  prefs.begin("pr32", true);
  audioMuted = prefs.getBool("muted", false);
  uint8_t savedBrightness = prefs.getUChar("bright", 255);
  prefs.end();
  hardwareSetBrightness(savedBrightness);

  encPrevAB = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), onEncoderChange, CHANGE);

  // Calibrate the thumbstick's rest position (assumes it's untouched during
  // boot) rather than assuming a hardcoded mid-scale reading — pot wiring/
  // tolerances mean "centered" isn't always exactly half of the ADC range.
  // A single quick 16-sample/32ms average (the original approach) trusted
  // that the stick was already perfectly settled the instant boot reached
  // this line — if it was still returning from a nudge (e.g. right after
  // a reset the user's hand caused, or the spring hadn't fully settled
  // yet), the resulting center could land just far enough off that the
  // normal resting reading pokes through THUMB_DEADZONE afterward for the
  // rest of that session, reads as a small constant drift in one
  // direction (reported live: "Bub is drifting left slowly"). Now retries
  // the whole sampling window (up to a few times) whenever the samples
  // themselves show the stick was still moving (a wide min/max spread),
  // and settles on the median rather than the mean so one leftover
  // transient sample can't skew the result.
  const int CAL_SAMPLES = 24;
  const int CAL_MAX_ATTEMPTS = 5;
  const int CAL_SETTLED_SPREAD = 40; // ADC counts (of 4095) -- comfortably
                                      // tighter than THUMB_DEADZONE's ~205.
  int samplesX[CAL_SAMPLES], samplesY[CAL_SAMPLES];
  for (int attempt = 0; attempt < CAL_MAX_ATTEMPTS; ++attempt) {
    int minX = 4095, maxX = 0, minY = 4095, maxY = 0;
    for (int i = 0; i < CAL_SAMPLES; i++) {
      int rx = analogRead(PIN_THUMB_X);
      int ry = analogRead(PIN_THUMB_Y);
      samplesX[i] = rx;
      samplesY[i] = ry;
      if (rx < minX) minX = rx;
      if (rx > maxX) maxX = rx;
      if (ry < minY) minY = ry;
      if (ry > maxY) maxY = ry;
      delay(2);
    }
    bool settled = (maxX - minX) <= CAL_SETTLED_SPREAD && (maxY - minY) <= CAL_SETTLED_SPREAD;
    if (settled || attempt == CAL_MAX_ATTEMPTS - 1) break;
  }
  // Simple insertion sort (CAL_SAMPLES is tiny) to find the median —
  // robust against one or two leftover transient samples in a way a plain
  // mean isn't.
  for (int i = 1; i < CAL_SAMPLES; ++i) {
    int keyX = samplesX[i], keyY = samplesY[i];
    int j = i - 1;
    while (j >= 0 && samplesX[j] > keyX) { samplesX[j + 1] = samplesX[j]; --j; }
    samplesX[j + 1] = keyX;
    j = i - 1;
    while (j >= 0 && samplesY[j] > keyY) { samplesY[j + 1] = samplesY[j]; --j; }
    samplesY[j + 1] = keyY;
  }
  thumbCenterX = samplesX[CAL_SAMPLES / 2];
  thumbCenterY = samplesY[CAL_SAMPLES / 2];
}

void hardwareInitDisplay() {
  tftReal.init(240, 320);
  tftReal.setRotation(2); // 180 degrees from the original orientation (still portrait, 240x320)
  tftReal.invertDisplay(true);
}

void hardwareSetOrientation(bool landscape) {
  // Rotation 2 is our normal portrait (see hardwareInit). Rotation 3 (one
  // step further, +90 degrees clockwise) turned out to be upside-down on
  // this physical board — confirmed on real hardware. Rotation 1 is the
  // other landscape option, 180 degrees from rotation 3, and is correct.
  tftReal.setRotation(landscape ? 1 : 2);
}

void hardwareReadThumbstick(float& x, float& y) {
  float rawX = normalizeThumbAxis(analogRead(PIN_THUMB_X), thumbCenterX);
  float rawY = normalizeThumbAxis(analogRead(PIN_THUMB_Y), thumbCenterY);
  thumbSmoothX += (rawX - thumbSmoothX) * THUMB_SMOOTH_ALPHA;
  thumbSmoothY += (rawY - thumbSmoothY) * THUMB_SMOOTH_ALPHA;
  x = thumbSmoothX;
  y = thumbSmoothY;
}
