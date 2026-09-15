// R-Type-style shmup's own note tables — original composition, no
// existing melody to transcribe (the dormant pre-Scene version had no
// music at all, just generic single-tone beeps — see conversation).
// Sequencer mechanics (NoteSequencer) live in buzzer_audio.h, shared
// with every other scene — see that file's top comment for why every
// tone() call goes through NoteSequencer rather than a raw duration-
// bearing tone() call.
#pragma once
#include <Arduino.h>
#include "buzzer_audio.h"

namespace rtype_pr32_audio {

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

// Weak shot fired — short high blip.
constexpr Note kFireWeakSfx[] = { { NOTE_C6, 22 } };
constexpr int kFireWeakSfxLen = sizeof(kFireWeakSfx) / sizeof(kFireWeakSfx[0]);

// Charged shot released — slightly longer, lower-then-higher.
constexpr Note kFireChargedSfx[] = { { NOTE_G4, 30 }, { NOTE_C6, 45 } };
constexpr int kFireChargedSfxLen = sizeof(kFireChargedSfx) / sizeof(kFireChargedSfx[0]);

// Enemy/debris destroyed.
constexpr Note kEnemyBreakSfx[] = { { NOTE_A4, 30 }, { NOTE_E4, 40 } };
constexpr int kEnemyBreakSfxLen = sizeof(kEnemyBreakSfx) / sizeof(kEnemyBreakSfx[0]);

// Capsule picked up.
constexpr Note kPickupSfx[] = { { NOTE_C5, 45 }, { NOTE_E5, 45 }, { NOTE_G5, 70 } };
constexpr int kPickupSfxLen = sizeof(kPickupSfx) / sizeof(kPickupSfx[0]);

// Weapon cycled (ENC_SW / R3 tap).
constexpr Note kWeaponCycleSfx[] = { { NOTE_E5, 25 }, { NOTE_A5, 25 } };
constexpr int kWeaponCycleSfxLen = sizeof(kWeaponCycleSfx) / sizeof(kWeaponCycleSfx[0]);

// Ship hit / life lost — descending "womp," same idiom as every other
// game's death cue here.
constexpr Note kShipHitSfx[] = {
    { NOTE_G4, 90 }, { NOTE_E4, 90 }, { NOTE_C4, 90 }, { NOTE_A2, 180 },
};
constexpr int kShipHitSfxLen = sizeof(kShipHitSfx) / sizeof(kShipHitSfx[0]);

// Game over.
constexpr Note kGameOverSfx[] = {
    { NOTE_C5, 120 }, { NOTE_A4, 120 }, { NOTE_E4, 120 }, { NOTE_A2, 300 },
};
constexpr int kGameOverSfxLen = sizeof(kGameOverSfx) / sizeof(kGameOverSfx[0]);

// Boss unit destroyed (split or final kill).
constexpr Note kBossHitSfx[] = { { NOTE_A4, 40 }, { NOTE_E4, 60 } };
constexpr int kBossHitSfxLen = sizeof(kBossHitSfx) / sizeof(kBossHitSfx[0]);

// Boss fully defeated / stage clear — ascending fanfare.
constexpr Note kStageClearSfx[] = {
    { NOTE_C5, 90 }, { NOTE_E5, 90 }, { NOTE_G5, 90 }, { NOTE_C6, 200 },
};
constexpr int kStageClearSfxLen = sizeof(kStageClearSfx) / sizeof(kStageClearSfx[0]);

} // namespace rtype_pr32_audio
