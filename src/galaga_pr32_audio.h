// Galaga's own note tables — sequencer mechanics (NoteSequencer) live in
// buzzer_audio.h, shared with Pac-Man and the menu. Same "no PCM
// hardware, approximate as tone blips" approach as those — see
// buzzer_audio.h's own top comment for why every tone() call goes
// through NoteSequencer rather than a raw duration-bearing tone() call.
#pragma once
#include <Arduino.h>
#include "buzzer_audio.h"

namespace galaga_pr32_audio {

using buzzer_audio::Note;
using buzzer_audio::NoteSequencer;

constexpr uint16_t NOTE_C4 = 262;
constexpr uint16_t NOTE_E4 = 330;
constexpr uint16_t NOTE_G4 = 392;
constexpr uint16_t NOTE_C5 = 523;
constexpr uint16_t NOTE_E5 = 659;
constexpr uint16_t NOTE_G5 = 784;
constexpr uint16_t NOTE_C6 = 1047;
constexpr uint16_t NOTE_A2 = 110;
constexpr uint16_t NOTE_A3 = 220;

// Fighter's own shot — a single quick high blip, fired every shot.
constexpr Note kFighterShotSfx[] = { { NOTE_C6, 35 } };
constexpr int kFighterShotSfxLen = sizeof(kFighterShotSfx) / sizeof(kFighterShotSfx[0]);

// Minion (bee) destroyed — short low-to-mid blip.
constexpr Note kMinionDestroyedSfx[] = { { NOTE_C4, 40 }, { NOTE_G4, 50 } };
constexpr int kMinionDestroyedSfxLen = sizeof(kMinionDestroyedSfx) / sizeof(kMinionDestroyedSfx[0]);

// Guard (butterfly) destroyed — slightly bigger blip than a minion.
constexpr Note kGuardDestroyedSfx[] = { { NOTE_E4, 40 }, { NOTE_C5, 60 } };
constexpr int kGuardDestroyedSfxLen = sizeof(kGuardDestroyedSfx) / sizeof(kGuardDestroyedSfx[0]);

// Boss destroyed — biggest of the three destruction cues.
constexpr Note kBossDestroyedSfx[] = { { NOTE_G4, 50 }, { NOTE_E5, 60 }, { NOTE_C6, 90 } };
constexpr int kBossDestroyedSfxLen = sizeof(kBossDestroyedSfx) / sizeof(kBossDestroyedSfx[0]);

// Boss takes its first hit (2 HP -> 1, not yet destroyed) — a single
// short blip, distinct from (and smaller than) the actual destroy cue.
constexpr Note kBossFirstHitSfx[] = { { NOTE_E5, 40 } };
constexpr int kBossFirstHitSfxLen = sizeof(kBossFirstHitSfx) / sizeof(kBossFirstHitSfx[0]);

// Fighter destroyed — descending "womp," same idiom as Pac-Man's death cue.
constexpr Note kFighterExplosionSfx[] = {
    { NOTE_G4, 100 }, { NOTE_E4, 100 }, { NOTE_C4, 100 }, { NOTE_A2, 200 },
};
constexpr int kFighterExplosionSfxLen = sizeof(kFighterExplosionSfx) / sizeof(kFighterExplosionSfx[0]);

// Stage cleared — short ascending fanfare.
constexpr Note kStageClearSfx[] = {
    { NOTE_C5, 90 }, { NOTE_E5, 90 }, { NOTE_G5, 90 }, { NOTE_C6, 180 },
};
constexpr int kStageClearSfxLen = sizeof(kStageClearSfx) / sizeof(kStageClearSfx[0]);

// A dive is triggered — quick descending blip.
constexpr Note kEnemyDivingSfx[] = { { NOTE_A3, 40 }, { NOTE_E4 - 40, 40 } };
constexpr int kEnemyDivingSfxLen = sizeof(kEnemyDivingSfx) / sizeof(kEnemyDivingSfx[0]);

// Enemy bullet fired.
constexpr Note kEnemyShotSfx[] = { { NOTE_A2, 30 } };
constexpr int kEnemyShotSfxLen = sizeof(kEnemyShotSfx) / sizeof(kEnemyShotSfx[0]);

// Tractor beam extends (reference's ENEMY_BEAM — plays once as the beam
// starts growing, distinct from the later capture/captured cues).
constexpr Note kEnemyBeamSfx[] = { { NOTE_A3, 25 }, { NOTE_A3 + 40, 25 }, { NOTE_A3 + 80, 25 } };
constexpr int kEnemyBeamSfxLen = sizeof(kEnemyBeamSfx) / sizeof(kEnemyBeamSfx[0]);

// The instant the beam actually touches the fighter (reference's
// ENEMY_BEAM_FIGHTER_CAPTURED — a quick zap, separate from the bigger
// "FIGHTER CAPTURED" stinger that plays once the pull-up finishes).
constexpr Note kEnemyBeamFighterCapturedSfx[] = { { NOTE_C5, 30 }, { NOTE_A3, 40 } };
constexpr int kEnemyBeamFighterCapturedSfxLen = sizeof(kEnemyBeamFighterCapturedSfx) / sizeof(kEnemyBeamFighterCapturedSfx[0]);

// Tractor beam captures the fighter — the bigger stinger, once the
// pull-up completes and the "FIGHTER CAPTURED" label appears.
constexpr Note kFighterCapturedSfx[] = {
    { NOTE_E4, 100 }, { NOTE_C4, 100 }, { NOTE_A2, 200 },
};
constexpr int kFighterCapturedSfxLen = sizeof(kFighterCapturedSfx) / sizeof(kFighterCapturedSfx[0]);

// Captured fighter rescued (boss carrying it destroyed).
constexpr Note kFighterRescuedSfx[] = {
    { NOTE_A2, 60 }, { NOTE_C4, 60 }, { NOTE_E4, 60 }, { NOTE_G4, 60 }, { NOTE_C5, 120 },
};
constexpr int kFighterRescuedSfxLen = sizeof(kFighterRescuedSfx) / sizeof(kFighterRescuedSfx[0]);

// Formation switches to the "scaled/breathing" movement once the diving
// phase begins (reference's FORMATION_SCALED — played once per stage,
// right as the formation starts its zoom-pulse and enemies start diving).
constexpr Note kFormationScaledSfx[] = { { NOTE_G4, 60 }, { NOTE_C5, 60 } };
constexpr int kFormationScaledSfxLen = sizeof(kFormationScaledSfx) / sizeof(kFormationScaledSfx[0]);

// Extra life awarded.
constexpr Note kExtraLifeSfx[] = {
    { NOTE_C5, 70 }, { NOTE_G5, 70 }, { NOTE_C6, 70 }, { NOTE_G5, 70 }, { NOTE_C6, 140 },
};
constexpr int kExtraLifeSfxLen = sizeof(kExtraLifeSfx) / sizeof(kExtraLifeSfx[0]);

} // namespace galaga_pr32_audio
