// Dig-Dug-style tunneling game's own note tables — original composition.
// Sequencer mechanics (NoteSequencer) live in buzzer_audio.h, shared
// with every other scene — see that file's top comment for why every
// tone() call goes through NoteSequencer rather than a raw duration-
// bearing tone() call.
#pragma once
#include <Arduino.h>
#include "buzzer_audio.h"

namespace digdug_pr32_audio {

using buzzer_audio::Note;
using buzzer_audio::NoteSequencer;

constexpr uint16_t NOTE_A2 = 110;
constexpr uint16_t NOTE_C4 = 262;
constexpr uint16_t NOTE_E4 = 330;
constexpr uint16_t NOTE_G4 = 392;
constexpr uint16_t NOTE_A4 = 440;
constexpr uint16_t NOTE_C5 = 523;
constexpr uint16_t NOTE_E5 = 659;
constexpr uint16_t NOTE_G5 = 784;
constexpr uint16_t NOTE_A5 = 880;
constexpr uint16_t NOTE_C6 = 1047;

// Pump started — short low blip, repeats while held (caller re-triggers).
constexpr Note kPumpSfx[] = { { NOTE_A4, 20 } };
constexpr int kPumpSfxLen = sizeof(kPumpSfx) / sizeof(kPumpSfx[0]);

// Enemy inflate stage increased — rising blip.
constexpr Note kInflateSfx[] = { { NOTE_C5, 25 } };
constexpr int kInflateSfxLen = sizeof(kInflateSfx) / sizeof(kInflateSfx[0]);

// Enemy burst — pop, short descending flourish.
constexpr Note kBurstSfx[] = { { NOTE_C6, 30 }, { NOTE_G5, 30 }, { NOTE_E5, 40 } };
constexpr int kBurstSfxLen = sizeof(kBurstSfx) / sizeof(kBurstSfx[0]);

// Rock starts shaking (telegraph before it falls).
constexpr Note kRockShakeSfx[] = { { NOTE_A2, 60 } };
constexpr int kRockShakeSfxLen = sizeof(kRockShakeSfx) / sizeof(kRockShakeSfx[0]);

// Rock lands / crushes something — low thud.
constexpr Note kRockCrushSfx[] = { { NOTE_A2, 40 }, { NOTE_A2, 90 } };
constexpr int kRockCrushSfxLen = sizeof(kRockCrushSfx) / sizeof(kRockCrushSfx[0]);

// Player digs into a fresh dirt tile — tiny tick, kept very short since
// it fires often.
constexpr Note kDigSfx[] = { { NOTE_E4, 12 } };
constexpr int kDigSfxLen = sizeof(kDigSfx) / sizeof(kDigSfx[0]);

// Player hit / life lost — descending "womp," same idiom as every other
// game's death cue here.
constexpr Note kPlayerHitSfx[] = {
    { NOTE_G4, 90 }, { NOTE_E4, 90 }, { NOTE_C4, 90 }, { NOTE_A2, 180 },
};
constexpr int kPlayerHitSfxLen = sizeof(kPlayerHitSfx) / sizeof(kPlayerHitSfx[0]);

// Game over.
constexpr Note kGameOverSfx[] = {
    { NOTE_C5, 120 }, { NOTE_A4, 120 }, { NOTE_E4, 120 }, { NOTE_A2, 300 },
};
constexpr int kGameOverSfxLen = sizeof(kGameOverSfx) / sizeof(kGameOverSfx[0]);

// Round cleared (all enemies gone).
constexpr Note kRoundClearSfx[] = {
    { NOTE_C5, 90 }, { NOTE_E5, 90 }, { NOTE_G5, 90 }, { NOTE_C6, 200 },
};
constexpr int kRoundClearSfxLen = sizeof(kRoundClearSfx) / sizeof(kRoundClearSfx[0]);

} // namespace digdug_pr32_audio
