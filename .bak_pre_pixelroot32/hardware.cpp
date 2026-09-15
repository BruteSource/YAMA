#include "hardware.h"

Adafruit_ST7789 tftReal = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_GFX* tft = &tftReal;
volatile long encoderValue = 0;

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
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_KEY0, INPUT_PULLUP);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_THUMB_SW, INPUT_PULLUP);

  encPrevAB = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), onEncoderChange, CHANGE);

  // Calibrate the thumbstick's rest position (assumes it's untouched during
  // boot) rather than assuming a hardcoded mid-scale reading — pot wiring/
  // tolerances mean "centered" isn't always exactly half of the ADC range.
  long sumX = 0, sumY = 0;
  const int CAL_SAMPLES = 16;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sumX += analogRead(PIN_THUMB_X);
    sumY += analogRead(PIN_THUMB_Y);
    delay(2);
  }
  thumbCenterX = sumX / CAL_SAMPLES;
  thumbCenterY = sumY / CAL_SAMPLES;

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
