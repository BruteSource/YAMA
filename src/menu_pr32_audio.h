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
constexpr uint16_t NOTE_GS4 = 415;
constexpr uint16_t NOTE_G4  = 392;
constexpr uint16_t NOTE_A4  = 440;
constexpr uint16_t NOTE_AS4 = 466;
constexpr uint16_t NOTE_B4  = 494;
constexpr uint16_t NOTE_C5 = 523;
constexpr uint16_t NOTE_D5 = 587;
constexpr uint16_t NOTE_E5 = 659;
constexpr uint16_t NOTE_G5 = 784;
constexpr uint16_t NOTE_GS5 = 831;
constexpr uint16_t NOTE_A5 = 880;
constexpr uint16_t NOTE_B5 = 988;
constexpr uint16_t NOTE_C6 = 1047;
constexpr uint16_t NOTE_D6 = 1175;

// "Rondo alla Turca" (the "Turkish March," 3rd movement of Mozart's
// Piano Sonata No. 11 in A major, K. 331, composed ~1783) — replaces
// the earlier "Axel F" tune at the user's request, both for how it
// sounds as a chiptune AND because it's genuinely public domain
// (Mozart died 1791; "Axel F" is a real, still-copyrighted 1984 pop
// song, so this also quietly fixes a real IP exposure the old tune had
// now that this project is public). Original arrangement for this
// project's single-voice piezo buzzer — a good-faith transcription of
// the famous melody (A minor intro into the instantly-recognizable
// A major "march" theme most people know this piece by), not sourced
// from any particular published arrangement. A 0 frequency is a rest.
// Loops via MenuScene::update() re-calling play() the instant
// isPlaying() goes false; the trailing rest is a deliberate breath
// before the loop repeats, not a bug.
constexpr Note kMenuTune[] = {
    // A minor intro
    { NOTE_E5, 120 }, { NOTE_D5, 120 }, { NOTE_C5, 120 }, { NOTE_B4, 120 },
    { NOTE_A4, 200 }, { 0, 80 },
    { NOTE_C5, 120 }, { NOTE_B4, 120 }, { NOTE_A4, 120 }, { NOTE_GS4, 120 },
    { NOTE_A4, 300 }, { 0, 150 },

    // The famous "Turkish march" theme, A major
    { NOTE_A5, 90 }, { NOTE_A5, 90 }, { NOTE_A5, 90 }, { NOTE_GS5, 90 },
    { NOTE_A5, 90 }, { NOTE_B5, 90 }, { NOTE_C6, 90 }, { NOTE_B5, 90 },
    { NOTE_A5, 90 }, { NOTE_GS5, 90 }, { NOTE_A5, 90 }, { 0, 90 },

    { NOTE_B5, 90 }, { NOTE_B5, 90 }, { NOTE_B5, 90 }, { NOTE_A5, 90 },
    { NOTE_B5, 90 }, { NOTE_C6, 90 }, { NOTE_D6, 90 }, { NOTE_C6, 90 },
    { NOTE_B5, 90 }, { NOTE_A5, 90 }, { NOTE_B5, 90 }, { 0, 90 },

    { NOTE_C6, 90 }, { NOTE_D6, 90 }, { NOTE_C6, 90 }, { NOTE_B5, 90 },
    { NOTE_A5, 90 }, { NOTE_GS5, 90 }, { NOTE_A5, 300 },

    { 0, 450 },
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
