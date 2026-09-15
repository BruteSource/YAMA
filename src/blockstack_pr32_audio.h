// Block Stack's own note tables — sequencer mechanics (NoteSequencer)
// live in buzzer_audio.h, shared with every other scene. All original,
// not transcribed from any reference.
#pragma once
#include <Arduino.h>
#include "buzzer_audio.h"

namespace blockstack_pr32_audio {

using buzzer_audio::Note;
using buzzer_audio::NoteSequencer;

constexpr uint16_t NOTE_C4 = 262;
constexpr uint16_t NOTE_D4 = 294;
constexpr uint16_t NOTE_E4 = 330;
constexpr uint16_t NOTE_F4 = 349;
constexpr uint16_t NOTE_G4 = 392;
constexpr uint16_t NOTE_A4 = 440;
constexpr uint16_t NOTE_B4 = 494;
constexpr uint16_t NOTE_C5 = 523;
constexpr uint16_t NOTE_D5 = 587;
constexpr uint16_t NOTE_E5 = 659;
constexpr uint16_t NOTE_F5 = 698;
constexpr uint16_t NOTE_G5 = 784;
constexpr uint16_t NOTE_A5 = 880;
constexpr uint16_t NOTE_C6 = 1047;
constexpr uint16_t NOTE_A2 = 110;

// Piece locks into place.
constexpr Note kLockSfx[] = { { NOTE_C4, 30 } };
constexpr int kLockSfxLen = sizeof(kLockSfx) / sizeof(kLockSfx[0]);

// Rotate.
constexpr Note kRotateSfx[] = { { NOTE_G4, 20 } };
constexpr int kRotateSfxLen = sizeof(kRotateSfx) / sizeof(kRotateSfx[0]);

// Single/double/triple line clear — a short upward run, longer for
// bigger clears.
constexpr Note kClear1Sfx[] = { { NOTE_C5, 60 }, { NOTE_E5, 80 } };
constexpr int kClear1SfxLen = sizeof(kClear1Sfx) / sizeof(kClear1Sfx[0]);
constexpr Note kClear2Sfx[] = { { NOTE_C5, 50 }, { NOTE_E5, 50 }, { NOTE_G5, 80 } };
constexpr int kClear2SfxLen = sizeof(kClear2Sfx) / sizeof(kClear2Sfx[0]);
constexpr Note kClear3Sfx[] = { { NOTE_C5, 45 }, { NOTE_E5, 45 }, { NOTE_G5, 45 }, { NOTE_C6, 90 } };
constexpr int kClear3SfxLen = sizeof(kClear3Sfx) / sizeof(kClear3Sfx[0]);
// 4-line clear ("tetris") — the big one.
constexpr Note kClear4Sfx[] = {
    { NOTE_C5, 45 }, { NOTE_D5, 45 }, { NOTE_E5, 45 }, { NOTE_G5, 45 }, { NOTE_C6, 140 },
};
constexpr int kClear4SfxLen = sizeof(kClear4Sfx) / sizeof(kClear4Sfx[0]);

// Hold piece swapped.
constexpr Note kHoldSfx[] = { { NOTE_A4, 25 }, { NOTE_C5, 25 } };
constexpr int kHoldSfxLen = sizeof(kHoldSfx) / sizeof(kHoldSfx[0]);

// Hard drop thump.
constexpr Note kHardDropSfx[] = { { NOTE_A2, 40 } };
constexpr int kHardDropSfxLen = sizeof(kHardDropSfx) / sizeof(kHardDropSfx[0]);

// Level up.
constexpr Note kLevelUpSfx[] = { { NOTE_C5, 60 }, { NOTE_G5, 60 }, { NOTE_C6, 100 } };
constexpr int kLevelUpSfxLen = sizeof(kLevelUpSfx) / sizeof(kLevelUpSfx[0]);

// Game over.
constexpr Note kGameOverSfx[] = {
    { NOTE_G4, 100 }, { NOTE_F4, 100 }, { NOTE_E4, 100 }, { NOTE_C4, 100 }, { NOTE_A2, 250 },
};
constexpr int kGameOverSfxLen = sizeof(kGameOverSfx) / sizeof(kGameOverSfx[0]);

// Attract/title jingle.
constexpr Note kIntroJingle[] = {
    { NOTE_C4, 80 }, { NOTE_E4, 80 }, { NOTE_G4, 80 }, { NOTE_C5, 80 },
    { NOTE_B4, 80 }, { NOTE_C5, 160 },
};
constexpr int kIntroJingleLen = sizeof(kIntroJingle) / sizeof(kIntroJingle[0]);

// Background music loop — an original arrangement of "Korobeiniki"
// ("Коробейники"), the traditional 19th-century Russian folk melody
// famously used as Tetris's "Music A." The folk tune itself is public
// domain (a 160+ year old melody, not something any Tetris rights
// holder owns) — this is a fresh transcription/arrangement for this
// project's single-voice piezo buzzer via NoteSequencer, not copied
// from any specific game's arrangement. Looped manually by the scene
// (see BlockStackScene::updateMusic()) since NoteSequencer itself only
// plays once per play() call.
constexpr Note kThemeMusic[] = {
    // Phrase A
    { NOTE_E5, 400 }, { NOTE_B4, 200 }, { NOTE_C5, 200 }, { NOTE_D5, 400 },
    { NOTE_C5, 200 }, { NOTE_B4, 200 }, { NOTE_A4, 400 }, { NOTE_A4, 200 },
    { NOTE_C5, 200 }, { NOTE_E5, 400 }, { NOTE_D5, 200 }, { NOTE_C5, 200 },
    { NOTE_B4, 600 }, { NOTE_C5, 200 }, { NOTE_D5, 400 },
    { NOTE_E5, 400 }, { NOTE_C5, 400 }, { NOTE_A4, 400 }, { NOTE_A4, 400 },
    { 0, 400 },
    // Phrase B
    { NOTE_D5, 600 }, { NOTE_F5, 200 }, { NOTE_A5, 400 },
    { NOTE_G5, 200 }, { NOTE_F5, 200 }, { NOTE_E5, 600 }, { NOTE_C5, 200 },
    { NOTE_E5, 400 }, { NOTE_D5, 200 }, { NOTE_C5, 200 },
    { NOTE_B4, 400 }, { NOTE_B4, 200 }, { NOTE_C5, 200 }, { NOTE_D5, 400 },
    { NOTE_E5, 400 }, { NOTE_C5, 400 }, { NOTE_A4, 400 }, { NOTE_A4, 400 },
    { 0, 400 },
};
constexpr int kThemeMusicLen = sizeof(kThemeMusic) / sizeof(kThemeMusic[0]);

} // namespace blockstack_pr32_audio
