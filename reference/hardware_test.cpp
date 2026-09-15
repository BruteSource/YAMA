// ESP32 hardware bring-up test
// Board: ESP32 DevKit + 2.0" 240x320 ST7789 SPI TFT + EC11 rotary encoder
//        (with onboard KEY0 button) + 2 hand-wired buttons
//
// Wiring:
//   TFT SDA/MOSI -> D23 (HW SPI MOSI, VSPI default)
//   TFT SCK      -> D18 (HW SPI SCK, VSPI default, not user-listed but required)
//   TFT RES      -> D4
//   TFT DC       -> D2
//   TFT CS       -> D15
//   TFT BLK      -> 3V3 (always on)
//   Encoder A    -> D13 (INPUT_PULLUP) -- moved from D12: D12 is an ESP32
//                  boot strapping pin (MTDI, selects flash voltage) and the
//                  encoder module's onboard pull-up held it high at reset,
//                  which broke flashing (esptool: "flash voltage set by a
//                  strapping pin: 1.8V" on a 3.3V flash chip).
//   Encoder B    -> D14 (INPUT_PULLUP)
//   Encoder SW   -> D27 (INPUT_PULLUP)
//   KEY0 (on-board button, header pin "KO") -> D26 (INPUT_PULLUP) -- KO is
//                  NOT a common/ground return despite some wiring charts
//                  claiming so; it's KEY0's own active-low signal line, fed
//                  by an on-board pull-up, same pattern as PSH/TRA/TRB.
//   Button 1     -> D32 (INPUT_PULLUP)
//   Button 2     -> D33 (INPUT_PULLUP)

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ---- Display pins ----
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
// SCK (18) and MOSI (23) use the ESP32's default hardware VSPI pins.

// ---- Encoder / button pins ----
#define PIN_ENC_A   13
#define PIN_ENC_B   14
#define PIN_ENC_SW  27
#define PIN_KEY0    26
#define PIN_BTN1    32
#define PIN_BTN2    33

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ---- Quadrature encoder decoding (interrupt driven, full-step table) ----
// Table indexed by (previous 2-bit AB) << 2 | (current 2-bit AB)
static const int8_t ENC_TABLE[16] = {
  0, -1,  1,  0,
  1,  0,  0, -1,
 -1,  0,  0,  1,
  0,  1, -1,  0
};

volatile long encoderValue = 0;
volatile uint8_t encPrevAB = 0;

void IRAM_ATTR onEncoderChange() {
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);
  uint8_t curAB = (a << 1) | b;
  uint8_t idx = (encPrevAB << 2) | curAB;
  encoderValue += ENC_TABLE[idx];
  encPrevAB = curAB;
}

// ---- Debounced button state ----
struct Button {
  uint8_t pin;
  const char* name;
  bool pressed = false;
  bool lastReading = true; // pulled up = HIGH when idle
  unsigned long lastChangeMs = 0;
};

Button encSw = { PIN_ENC_SW, "ENC SW" };
Button key0  = { PIN_KEY0,   "KEY0" };
Button btn1  = { PIN_BTN1,   "BTN 1" };
Button btn2  = { PIN_BTN2,   "BTN 2" };
const int NUM_BUTTONS = 4;
Button* buttons[NUM_BUTTONS] = { &encSw, &key0, &btn1, &btn2 };

const unsigned long DEBOUNCE_MS = 25;

bool updateButton(Button& b) {
  bool reading = digitalRead(b.pin);
  bool changed = false;
  if (reading != b.lastReading) {
    b.lastChangeMs = millis();
    b.lastReading = reading;
  }
  if ((millis() - b.lastChangeMs) > DEBOUNCE_MS) {
    bool nowPressed = (reading == LOW);
    if (nowPressed != b.pressed) {
      b.pressed = nowPressed;
      changed = true;
    }
  }
  return changed;
}

// ---- Layout (landscape, 320x240) ----
long lastDrawnEncoderValue = 0x7fffffff; // force first draw

const int BOX_Y = 150;
const int BOX_W = 68;
const int BOX_H = 60;
const int BOX_GAP = 8;
const int BOX_X0 = 10;

void drawButtonBox(int index, Button& b) {
  int x = BOX_X0 + index * (BOX_W + BOX_GAP);
  uint16_t fill = b.pressed ? ST77XX_GREEN : ST77XX_BLACK;
  uint16_t border = ST77XX_WHITE;
  tft.fillRect(x, BOX_Y, BOX_W, BOX_H, fill);
  tft.drawRect(x, BOX_Y, BOX_W, BOX_H, border);
  tft.setTextColor(b.pressed ? ST77XX_BLACK : ST77XX_WHITE, fill);
  tft.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(b.name, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(x + (BOX_W - w) / 2, BOX_Y + BOX_H / 2 - h / 2);
  tft.print(b.name);
}

void drawEncoderValue() {
  tft.fillRect(0, 70, 320, 40, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  char buf[24];
  snprintf(buf, sizeof(buf), "Encoder: %ld", encoderValue);
  tft.setCursor(20, 75);
  tft.print(buf);
}

void colorTest() {
  uint16_t colors[] = { ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE, ST77XX_BLACK };
  const char* names[] = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };
  for (int i = 0; i < 5; i++) {
    tft.fillScreen(colors[i]);
    Serial.printf("Color test: %s\n", names[i]);
    delay(400);
  }
}

void drawStaticUI() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(20, 15);
  tft.print("ESP32 HW TEST");
  tft.drawFastHLine(0, 45, 320, ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 55);
  tft.print("Turn encoder / press buttons");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nESP32 Encoder + Color TFT hardware test");

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_KEY0, INPUT_PULLUP);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);

  encPrevAB = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), onEncoderChange, CHANGE);

  tft.init(240, 320);       // native panel resolution
  tft.setRotation(3);       // landscape, 320x240, USB/header pointing right-ish
  tft.invertDisplay(true);  // most ST7789 modules need this for correct colors; flip to false if colors look inverted

  Serial.println("Running color test (check for solid RED / GREEN / BLUE / WHITE / BLACK full-screen fills)...");
  colorTest();

  drawStaticUI();
  drawEncoderValue();
  for (int i = 0; i < NUM_BUTTONS; i++) drawButtonBox(i, *buttons[i]);

  Serial.println("Setup complete. Interact with encoder and buttons.");
}

void loop() {
  noInterrupts();
  long currentEncoder = encoderValue;
  interrupts();

  if (currentEncoder != lastDrawnEncoderValue) {
    lastDrawnEncoderValue = currentEncoder;
    drawEncoderValue();
    Serial.printf("Encoder: %ld\n", currentEncoder);
  }

  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (updateButton(*buttons[i])) {
      drawButtonBox(i, *buttons[i]);
      Serial.printf("%s: %s\n", buttons[i]->name, buttons[i]->pressed ? "PRESSED" : "released");
    }
  }

  delay(2);
}
