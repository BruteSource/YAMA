// Pac-Man's own note tables — sound engine mechanics (NoteSequencer)
// live in buzzer_audio.h, shared with the menu. This project has no
// PCM/sample playback hardware (the board's only audio output is a
// 2-wire passive piezo on PIN_BUZZER, driven via tone()/noTone() square
// waves — no amp, DAC chip, or I2S wiring for PixelRoot32's real
// sample-based AudioEngine to output through), so this approximates the
// reference's real WAV/MP3 cues as note-frequency/duration sequences,
// the same way most hobbyist Arduino-buzzer Pac-Man projects do it.
#pragma once
#include <Arduino.h>
#include "hardware.h"
#include "buzzer_audio.h"

namespace pacman_pr32_audio {

using buzzer_audio::Note;
using buzzer_audio::NoteSequencer;

// Standard equal-temperament note frequencies (Hz), rounded — only the
// ones the tables below actually use.
constexpr uint16_t NOTE_B4  = 494;
constexpr uint16_t NOTE_C5  = 523;
constexpr uint16_t NOTE_DS5 = 622;
constexpr uint16_t NOTE_FS5 = 740;
constexpr uint16_t NOTE_B5  = 988;
constexpr uint16_t NOTE_C6  = 1047;
constexpr uint16_t NOTE_E6  = 1319;
constexpr uint16_t NOTE_G6  = 1568;
constexpr uint16_t NOTE_A3  = 220;
constexpr uint16_t NOTE_A4  = 440;
constexpr uint16_t NOTE_G4  = 392;
constexpr uint16_t NOTE_G5  = 784;
constexpr uint16_t NOTE_E4  = 330;
constexpr uint16_t NOTE_E5  = 659;
constexpr uint16_t NOTE_C4  = 262;
constexpr uint16_t NOTE_G3  = 196;
constexpr uint16_t NOTE_E3  = 165;
constexpr uint16_t NOTE_C3  = 131;

// The classic Pac-Man opening jingle — a commonly-published, best-effort
// transcription (this exact note sequence shows up across many
// independent Arduino/piezo Pac-Man sketches), not a sheet-accurate
// transcription of the arcade's real 3-voice chip tune. Played once at
// the start of a game (see PacManScene::init()), during which gameplay
// is deliberately frozen — matches the real arcade holding Pac-Man/
// ghosts still while this plays.
constexpr Note kIntroJingle[] = {
    { NOTE_B4, 100 }, { NOTE_B5, 100 }, { NOTE_FS5, 100 }, { NOTE_DS5, 100 },
    { 0, 50 },
    { NOTE_B5, 100 }, { NOTE_FS5, 100 }, { NOTE_DS5, 100 },
    { 0, 50 },
    { NOTE_C5, 100 }, { NOTE_C6, 100 }, { NOTE_G6, 100 }, { NOTE_E6, 100 },
    { 0, 50 },
    { NOTE_C6, 100 }, { NOTE_G6, 100 }, { NOTE_E6, 100 },
    { 0, 150 },
};
constexpr int kIntroJingleLen = sizeof(kIntroJingle) / sizeof(kIntroJingle[0]);

// Two-tone "waka" pulses, alternated by the caller (see
// PacManScene::updatePlayer()'s chomp toggle) in sync with the mouth
// animation rather than stored as a fixed sequence — not a NoteSequencer
// table. Raised a fourth from the original A3/A4 (220/440) after being
// reported live as "a lil low."
constexpr uint16_t kChompToneA = NOTE_E4;
constexpr uint16_t kChompToneB = NOTE_E5;
constexpr uint16_t kChompDurationMs = 60;

// Power pellet: a quick descending "power up" whoop.
constexpr Note kPowerPelletSfx[] = {
    { NOTE_A4, 60 }, { NOTE_E4, 60 }, { NOTE_C4, 60 }, { NOTE_A3, 80 },
};
constexpr int kPowerPelletSfxLen = sizeof(kPowerPelletSfx) / sizeof(kPowerPelletSfx[0]);

// Ghost eaten: a quick rising blip.
constexpr Note kGhostEatenSfx[] = {
    { NOTE_C5, 40 }, { NOTE_E5, 40 }, { NOTE_G5, 40 }, { NOTE_E6, 60 },
};
constexpr int kGhostEatenSfxLen = sizeof(kGhostEatenSfx) / sizeof(kGhostEatenSfx[0]);

// Bonus fruit eaten: a short pleasant chime.
constexpr Note kFruitSfx[] = {
    { NOTE_C5, 70 }, { NOTE_E5, 70 }, { NOTE_G5, 90 },
};
constexpr int kFruitSfxLen = sizeof(kFruitSfx) / sizeof(kFruitSfx[0]);

// Death: a descending "womp womp womp" — freezes gameplay same as the
// intro (see PacManScene's death handling).
constexpr Note kDeathSfx[] = {
    { NOTE_C5, 150 }, { NOTE_G4, 150 }, { NOTE_E4, 150 },
    { NOTE_C4, 150 }, { NOTE_G3, 150 }, { NOTE_E3, 150 }, { NOTE_C3, 300 },
};
constexpr int kDeathSfxLen = sizeof(kDeathSfx) / sizeof(kDeathSfx[0]);

// Level cleared: a short 3-note ascending fanfare.
constexpr Note kLevelCompleteSfx[] = {
    { NOTE_C5, 90 }, { NOTE_E5, 90 }, { NOTE_G5, 90 }, { NOTE_C6, 180 },
};
constexpr int kLevelCompleteSfxLen = sizeof(kLevelCompleteSfx) / sizeof(kLevelCompleteSfx[0]);

} // namespace pacman_pr32_audio
