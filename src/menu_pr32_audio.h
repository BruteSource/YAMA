// Menu sound: a short looping background tune plus quick blips for
// moving the selection and confirming it. Sequencer mechanics
// (NoteSequencer) live in buzzer_audio.h, shared with Pac-Man's own
// note tables (pacman_pr32_audio.h) — see that file for why tone()
// calls here also always use the 2-argument form.
#pragma once
#include <Arduino.h>
#include "buzzer_audio.h"

namespace menu_pr32_audio {

using buzzer_audio::Note;
using buzzer_audio::NoteSequencer;

constexpr uint16_t NOTE_A3  = 220;
constexpr uint16_t NOTE_C4  = 262;
constexpr uint16_t NOTE_D4  = 294;
constexpr uint16_t NOTE_E4  = 330;
constexpr uint16_t NOTE_F4  = 349;
constexpr uint16_t NOTE_G4  = 392;
constexpr uint16_t NOTE_A4  = 440;
constexpr uint16_t NOTE_AS4 = 466;
constexpr uint16_t NOTE_C5 = 523;
constexpr uint16_t NOTE_D5 = 587;
constexpr uint16_t NOTE_E5 = 659;
constexpr uint16_t NOTE_G5 = 784;
constexpr uint16_t NOTE_C6 = 1047;
constexpr uint16_t NOTE_A5 = 880;

// "Axel F" (Harold Faltermeyer, Beverly Hills Cop) — user asked for
// this specific tune by name for the menu's background loop, replacing
// an earlier generic composition. Transcribed from a real, published
// Arduino tone()-sketch note table (nseidle/AxelF_DoorBell on GitHub,
// public-domain/Beerware-licensed), not composed from ear — its
// `line1[]`/`line1_durations[]` arrays (note + 1/N note-length
// denominator, e.g. 8=eighth, 16=sixteenth, 2=half) converted here to
// straight milliseconds via that same sketch's own "playSpeed 2" tempo
// (ms = 2000/denominator), THEN scaled by 0.65x — the source's own
// tempo alone played live and was reported as off; 0.65x is what the
// user dialed in and confirmed via the live tempo-tuning debug feature
// that used to live in menu_pr32.cpp/.h (removed now that a final value
// is locked in — see git history / arcade_menu_pacman.md memory if that
// mechanism is ever needed again for a different tune). A 0 frequency is
// a rest, matching the source sketch. Loops via MenuScene::update()
// re-calling play() the instant isPlaying() goes false; the trailing
// rest is a deliberate breath before the loop repeats, not a bug.
constexpr Note kMenuTune[] = {
    { NOTE_D4, 163 }, { 0, 163 }, { NOTE_F4, 216 }, { NOTE_D4, 81 }, { 0, 81 },
    { NOTE_D4, 81 }, { NOTE_G4, 163 }, { NOTE_D4, 163 }, { NOTE_C4, 163 },

    { NOTE_D4, 163 }, { 0, 163 }, { NOTE_A4, 216 }, { NOTE_D4, 81 }, { 0, 81 },
    { NOTE_D4, 81 }, { NOTE_AS4, 163 }, { NOTE_A4, 163 }, { NOTE_F4, 163 },

    { NOTE_D4, 163 }, { NOTE_A4, 163 }, { NOTE_D5, 163 }, { NOTE_D4, 81 }, { NOTE_C4, 81 },
    { 0, 81 }, { NOTE_C4, 81 }, { NOTE_A3, 163 }, { NOTE_E4, 163 }, { NOTE_D4, 650 },

    { 0, 650 },
};
constexpr int kMenuTuneLen = sizeof(kMenuTune) / sizeof(kMenuTune[0]);

// Selection moved (encoder tick or thumbstick push).
constexpr Note kMenuMoveSfx[] = {
    { NOTE_A5, 45 },
};
constexpr int kMenuMoveSfxLen = sizeof(kMenuMoveSfx) / sizeof(kMenuMoveSfx[0]);

// Selection confirmed (A/R3 on an available entry).
constexpr Note kMenuConfirmSfx[] = {
    { NOTE_C5, 60 }, { NOTE_G5, 60 }, { NOTE_C6, 100 },
};
constexpr int kMenuConfirmSfxLen = sizeof(kMenuConfirmSfx) / sizeof(kMenuConfirmSfx[0]);

} // namespace menu_pr32_audio
