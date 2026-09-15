// Arkanoid's own note tables — sequencer mechanics (NoteSequencer) live in
// buzzer_audio.h, shared with every other scene. See that file's top
// comment for why every tone() call goes through NoteSequencer rather
// than a raw duration-bearing tone() call.
#pragma once
#include <Arduino.h>
#include "buzzer_audio.h"

namespace arkanoid_pr32_audio {

using buzzer_audio::Note;
using buzzer_audio::NoteSequencer;

constexpr uint16_t NOTE_C4 = 262;
constexpr uint16_t NOTE_E4 = 330;
constexpr uint16_t NOTE_G4 = 392;
constexpr uint16_t NOTE_A4 = 440;
constexpr uint16_t NOTE_C5 = 523;
constexpr uint16_t NOTE_E5 = 659;
constexpr uint16_t NOTE_G5 = 784;
constexpr uint16_t NOTE_C6 = 1047;
constexpr uint16_t NOTE_A2 = 110;
constexpr uint16_t NOTE_A3 = 220;

// Ball bounces off the paddle — a single short blip.
constexpr Note kPaddleBounceSfx[] = { { NOTE_A4, 30 } };
constexpr int kPaddleBounceSfxLen = sizeof(kPaddleBounceSfx) / sizeof(kPaddleBounceSfx[0]);

// Ball bounces off a wall — slightly lower/duller than the paddle blip.
constexpr Note kWallBounceSfx[] = { { NOTE_E4, 25 } };
constexpr int kWallBounceSfxLen = sizeof(kWallBounceSfx) / sizeof(kWallBounceSfx[0]);

// A brick is destroyed — short mid blip, pitch doesn't vary by color
// (the reference doesn't do that either, keeps it simple/recognizable).
constexpr Note kBrickDestroyedSfx[] = { { NOTE_G4, 35 }, { NOTE_C5, 45 } };
constexpr int kBrickDestroyedSfxLen = sizeof(kBrickDestroyedSfx) / sizeof(kBrickDestroyedSfx[0]);

// Ball lost — descending "womp," same idiom as every other game's death cue.
constexpr Note kBallLostSfx[] = {
    { NOTE_G4, 100 }, { NOTE_E4, 100 }, { NOTE_C4, 100 }, { NOTE_A2, 200 },
};
constexpr int kBallLostSfxLen = sizeof(kBallLostSfx) / sizeof(kBallLostSfx[0]);

// A powerup capsule is caught by the paddle.
constexpr Note kPowerupCaughtSfx[] = { { NOTE_C5, 60 }, { NOTE_E5, 60 }, { NOTE_G5, 90 } };
constexpr int kPowerupCaughtSfxLen = sizeof(kPowerupCaughtSfx) / sizeof(kPowerupCaughtSfx[0]);

// Round cleared — short ascending fanfare.
constexpr Note kRoundClearSfx[] = {
    { NOTE_C5, 90 }, { NOTE_E5, 90 }, { NOTE_G5, 90 }, { NOTE_C6, 180 },
};
constexpr int kRoundClearSfxLen = sizeof(kRoundClearSfx) / sizeof(kRoundClearSfx[0]);

// Laser shot fired.
constexpr Note kLaserShotSfx[] = { { NOTE_C6, 25 } };
constexpr int kLaserShotSfxLen = sizeof(kLaserShotSfx) / sizeof(kLaserShotSfx[0]);

// Game over.
constexpr Note kGameOverSfx[] = {
    { NOTE_C5, 120 }, { NOTE_A4, 120 }, { NOTE_E4, 120 }, { NOTE_A2, 300 },
};
constexpr int kGameOverSfxLen = sizeof(kGameOverSfx) / sizeof(kGameOverSfx[0]);

// Intro jingle — played once as the title screen transitions to the story
// screen, same "short attract sting" role as Pac-Man's kIntroJingle.
constexpr Note kIntroJingle[] = {
    { NOTE_C4, 90 }, { NOTE_E4, 90 }, { NOTE_G4, 90 }, { NOTE_C5, 90 },
    { NOTE_G4, 90 }, { NOTE_C5, 180 },
};
constexpr int kIntroJingleLen = sizeof(kIntroJingle) / sizeof(kIntroJingle[0]);

} // namespace arkanoid_pr32_audio
