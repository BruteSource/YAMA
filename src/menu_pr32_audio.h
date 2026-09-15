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
constexpr uint16_t NOTE_F5  = 698;
constexpr uint16_t NOTE_G5  = 784;
constexpr uint16_t NOTE_A5  = 880;
constexpr uint16_t NOTE_B5  = 988;
constexpr uint16_t NOTE_C6  = 1047;
constexpr uint16_t NOTE_D6  = 1175;
constexpr uint16_t NOTE_E6  = 1319;
constexpr uint16_t NOTE_F6  = 1397;

// "Chips Chips (NES Style Music)" by mensch-maschine on Pixabay
// (https://pixabay.com/music/video-games-chips-chips-nes-style-music-165102/),
// Pixabay Content License — free to use/modify, no attribution
// required, so no IP concern the way the Rick Astley tune had.
// Transcribed directly from a user-supplied RTTTL string: "ChipsChips:
// d=4,o=5,b=140:8c,8e,8g,8c6,8g,8e,c,8d,8f,8a,8d6,8a,8f,d,8e,8g,8b,
// 8e6,8b,8g,e,8f,8a,8c6,8f6,8c6,8a,f" — a climbing set of major-triad
// arpeggios (C, then D, then E, then F as the root), each spanning up
// an octave and back down before landing on a held root note. Tempo
// is the string's own b=140: wholenote = 60000*4/140 ≈ 1714ms; d=4
// (quarter) ≈ 429ms, d=8 (eighth) ≈ 214ms — the standard RTTTL tempo
// formula, applied here since NoteSequencer takes milliseconds, not
// RTTTL's own duration codes. See menu_music_preference.md (memory)
// for the full menu-music iteration history.
constexpr Note kMenuTune[] = {
    { NOTE_C5, 214 }, { NOTE_E5, 214 }, { NOTE_G5, 214 }, { NOTE_C6, 214 },
    { NOTE_G5, 214 }, { NOTE_E5, 214 }, { NOTE_C5, 429 },

    { NOTE_D5, 214 }, { NOTE_F5, 214 }, { NOTE_A5, 214 }, { NOTE_D6, 214 },
    { NOTE_A5, 214 }, { NOTE_F5, 214 }, { NOTE_D5, 429 },

    { NOTE_E5, 214 }, { NOTE_G5, 214 }, { NOTE_B5, 214 }, { NOTE_E6, 214 },
    { NOTE_B5, 214 }, { NOTE_G5, 214 }, { NOTE_E5, 429 },

    { NOTE_F5, 214 }, { NOTE_A5, 214 }, { NOTE_C6, 214 }, { NOTE_F6, 214 },
    { NOTE_C6, 214 }, { NOTE_A5, 214 }, { NOTE_F5, 429 },
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
