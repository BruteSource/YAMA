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
constexpr uint16_t NOTE_F5  = 698;
constexpr uint16_t NOTE_G5  = 784;
constexpr uint16_t NOTE_AS5 = 932;
constexpr uint16_t NOTE_A5  = 880;
constexpr uint16_t NOTE_C6  = 1047;
constexpr uint16_t NOTE_F6  = 1397;
constexpr uint16_t NOTE_G6  = 1568;
constexpr uint16_t NOTE_AS6 = 1865;

// User-supplied RTTTL string, transcribed directly:
// "DnBMenu:d=16,o=5,b=170:c,p,c6,p,g,p,g6,p,a#,p,a#6,p,f,p,f6,p,
//  c,c,c6,p,g,g,g6,p,a#,a#,a#6,p,f,f,f6,p" — replaces the Rondo alla
// Turca loop (still liked less than this). d=16/b=170 means every
// step (note or rest) is a sixteenth note at 170bpm: wholenote =
// 60000*4/170 ≈ 1412ms, /16 ≈ 88ms — the standard RTTTL tempo formula,
// applied here since NoteSequencer takes milliseconds, not RTTTL's
// own duration codes. "p" = rest (0 Hz). A four-on-the-floor
// octave-jump bassline (root note then its octave, both against a
// 16th-note rest grid, doubling up unrested on the repeat of each
// phrase) — a genuine DnB-style pattern, not something transcribed
// from a copyrighted track.
constexpr Note kMenuTune[] = {
    { NOTE_C5, 88 }, { 0, 88 }, { NOTE_C6, 88 }, { 0, 88 },
    { NOTE_G5, 88 }, { 0, 88 }, { NOTE_G6, 88 }, { 0, 88 },
    { NOTE_AS5, 88 }, { 0, 88 }, { NOTE_AS6, 88 }, { 0, 88 },
    { NOTE_F5, 88 }, { 0, 88 }, { NOTE_F6, 88 }, { 0, 88 },

    { NOTE_C5, 88 }, { NOTE_C5, 88 }, { NOTE_C6, 88 }, { 0, 88 },
    { NOTE_G5, 88 }, { NOTE_G5, 88 }, { NOTE_G6, 88 }, { 0, 88 },
    { NOTE_AS5, 88 }, { NOTE_AS5, 88 }, { NOTE_AS6, 88 }, { 0, 88 },
    { NOTE_F5, 88 }, { NOTE_F5, 88 }, { NOTE_F6, 88 }, { 0, 88 },
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
