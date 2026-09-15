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

constexpr uint16_t NOTE_C5  = 523;
constexpr uint16_t NOTE_D5  = 587;
constexpr uint16_t NOTE_E5  = 659;
constexpr uint16_t NOTE_FS5 = 740;
constexpr uint16_t NOTE_G5  = 784;
constexpr uint16_t NOTE_GS5 = 831;
constexpr uint16_t NOTE_A5  = 880;
constexpr uint16_t NOTE_B4  = 494;
constexpr uint16_t NOTE_B5  = 988;
constexpr uint16_t NOTE_C6  = 1047;

// "Creation3" — an ORIGINAL tune the user composed themselves (their
// own words: "I just made up"), so no IP question at all, unlike every
// previous menu-music attempt. Transcribed directly from their RTTTL
// string: "Creation3:d=4,o=4,b=160:8f#5,8f#5,8f#5,8d5,8p,8b,8p,8e5,8p,
// 8e5,8p,8e5,8g#5,8g#5,8a5,8b5,8a5,8a5,8a5,8e5,8p,8d5,8p,8f#5,8p,8f#5,
// 8p,8f#5,8e5,8e5,8f#5,8e5,8f#5,8f#5,8f#5,8d5,8p,8b,8p,8e5,8p,8e5,8p,
// 8e5,8g#5,8g#5,8a5,8b5,8a5,8a5,8a5,8e5,8p,8d5,8p,8f#5,8p,8f#5,8p,8f#5,
// 8e5,8e5" — a 30-note phrase (with a 2-note f#5/e5 bridge) played
// twice. Every step is an eighth note at the string's own b=160:
// wholenote = 60000*4/160 = 1500ms, /8 = 188ms — the standard RTTTL
// tempo formula, applied here since NoteSequencer takes milliseconds,
// not RTTTL's own duration codes. Bare "b" (no octave suffix) is o=4
// per the string's default, i.e. B4, not B5. See
// menu_music_preference.md (memory) for the full menu-music iteration
// history.
constexpr Note kMenuTune[] = {
    { NOTE_FS5, 188 }, { NOTE_FS5, 188 }, { NOTE_FS5, 188 }, { NOTE_D5, 188 },
    { 0, 188 }, { NOTE_B4, 188 }, { 0, 188 }, { NOTE_E5, 188 },
    { 0, 188 }, { NOTE_E5, 188 }, { 0, 188 }, { NOTE_E5, 188 },
    { NOTE_GS5, 188 }, { NOTE_GS5, 188 }, { NOTE_A5, 188 }, { NOTE_B5, 188 },
    { NOTE_A5, 188 }, { NOTE_A5, 188 }, { NOTE_A5, 188 }, { NOTE_E5, 188 },
    { 0, 188 }, { NOTE_D5, 188 }, { 0, 188 }, { NOTE_FS5, 188 },
    { 0, 188 }, { NOTE_FS5, 188 }, { 0, 188 }, { NOTE_FS5, 188 },
    { NOTE_E5, 188 }, { NOTE_E5, 188 },

    { NOTE_FS5, 188 }, { NOTE_E5, 188 },

    { NOTE_FS5, 188 }, { NOTE_FS5, 188 }, { NOTE_FS5, 188 }, { NOTE_D5, 188 },
    { 0, 188 }, { NOTE_B4, 188 }, { 0, 188 }, { NOTE_E5, 188 },
    { 0, 188 }, { NOTE_E5, 188 }, { 0, 188 }, { NOTE_E5, 188 },
    { NOTE_GS5, 188 }, { NOTE_GS5, 188 }, { NOTE_A5, 188 }, { NOTE_B5, 188 },
    { NOTE_A5, 188 }, { NOTE_A5, 188 }, { NOTE_A5, 188 }, { NOTE_E5, 188 },
    { 0, 188 }, { NOTE_D5, 188 }, { 0, 188 }, { NOTE_FS5, 188 },
    { 0, 188 }, { NOTE_FS5, 188 }, { 0, 188 }, { NOTE_FS5, 188 },
    { NOTE_E5, 188 }, { NOTE_E5, 188 },
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
