#include "sound.h"
#include "hardware.h"
#include <Arduino.h>

namespace {
  // Short blocking arpeggio for rare one-off transitions (level clear, game
  // over) only — never called from per-frame hit/move SFX, which must stay
  // non-blocking (tone() itself already returns immediately).
  void playNotes(const int* freqs, const int* durs, int n) {
    for (int i = 0; i < n; i++) {
      tone(PIN_BUZZER, freqs[i], durs[i]);
      delay(durs[i] + 10);
    }
    noTone(PIN_BUZZER);
  }
}

void soundInit() {
  pinMode(PIN_BUZZER, OUTPUT);
}

void sfxUiMove()    { tone(PIN_BUZZER, 500, 25); }
void sfxUiConfirm() { tone(PIN_BUZZER, 850, 55); }
void sfxHit()       { tone(PIN_BUZZER, 300, 20); }
void sfxBreak()     { tone(PIN_BUZZER, 700, 35); }
void sfxPowerup()   { tone(PIN_BUZZER, 1050, 80); }
void sfxBad()       { tone(PIN_BUZZER, 180, 120); }

void sfxLevelClear() {
  int f[4] = { 523, 659, 784, 1047 };
  int d[4] = { 60, 60, 60, 130 };
  playNotes(f, d, 4);
}

void sfxGameOver() {
  int f[4] = { 392, 330, 262, 196 };
  int d[4] = { 120, 120, 120, 220 };
  playNotes(f, d, 4);
}

void sfxLineClear(int lines) {
  int f[4] = { 659, 784, 988, 1319 };
  int d[4] = { 70, 80, 90, 130 };
  int n = constrain(lines, 1, 4);
  playNotes(f, d, n);
}
