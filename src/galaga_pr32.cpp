#include "galaga_pr32.h"
#include "galaga_pr32_sprites.h"
#include "menu_pr32_font.h"
#include <graphics/Color.h>
#include <graphics/FontManager.h>
#include <cstdio>
#include <cstring>
#include <cmath>

// Temporary: set to 1 to trace capture/rescue state to serial while
// debugging — same idiom as PACMAN_DEBUG_TRACE. Leave 0 normally.
#define GALAGA_DEBUG_TRACE 0

namespace pr32 = pixelroot32;

namespace galaga_pr32 {

using pr32::graphics::Color;
using pr32::graphics::Renderer;

// Reuse the menu's own retro pixel font (Press Start 2P, rasterized by
// tools/make_menu_pr32_font.py) for all HUD/on-screen text — the plain
// `drawText(...)` calls with no Font* fall back to PixelRoot32's default
// built-in font, which looks like ordinary terminal text, not an arcade
// HUD. Only covers ASCII 32-90 (space..'Z', digits, basic punctuation,
// no lowercase) — every string this scene draws is already all-caps.
const pixelroot32::graphics::Font* const kHudFont = &menu_pr32_font::FONT_SMALL;

namespace {

// Reference's own 224x288 space, ported verbatim — see galaga_pr32.h's
// top comment for why no scaling is needed, just this fixed offset.
constexpr float kFieldW = 224.0f;
constexpr float kFieldH = 288.0f;
constexpr int PLAYFIELD_LEFT = 8;   // (240-224)/2
constexpr int PLAYFIELD_TOP = 32;   // 320-288, all of it — HUD lives above

int sx(float x) { return PLAYFIELD_LEFT + static_cast<int>(x); }
int sy(float y) { return PLAYFIELD_TOP + static_cast<int>(y); }

// ================= Entrance path waypoint tables =================
// Quoted directly from the reference's Path.cpp (patterns 2/4 are the
// literal `224.0f - x` mirrors it computes at load time, pre-computed
// here since we don't need runtime mirroring).
constexpr Vec2 PATTERN_1[] = {
    {90,-150}, {93,1}, {97,21}, {112,57}, {150,100}, {190,130},
    {200,149}, {192,167}, {170,179}, {147,167}, {140,145}, {139,131},
};
constexpr Vec2 PATTERN_2[] = {
    {134,-150}, {131,1}, {127,21}, {112,57}, {74,100}, {34,130},
    {24,149}, {32,167}, {54,179}, {77,167}, {84,145}, {85,131},
};
constexpr Vec2 PATTERN_3[] = {
    {-150,228}, {-20,228}, {0,228}, {29,217}, {61,193}, {84,168},
    {95,148}, {94,130}, {81,115}, {60,118}, {50,136}, {56,153},
    {74,159}, {93,151}, {94,130},
};
constexpr Vec2 PATTERN_4[] = {
    {374,228}, {244,228}, {224,228}, {195,217}, {163,193}, {140,168},
    {129,148}, {130,130}, {143,115}, {164,118}, {174,136}, {168,153},
    {150,159}, {131,151}, {130,130},
};

constexpr Vec2 PATH_FIGHTER_NEXT_LIFE[] = {
    {8,280}, {13,240}, {40,210}, {80,300}, {100,300}, {110,274}, {112,256},
};

void buildPath(CatmullPath& path, const Vec2* pts, int count) {
    path.clear();
    for (int i = 0; i < count; ++i) path.addPoint(pts[i]);
}

// ================= Formation grid =================
// EnemyInfo { pathId (1-4), formationCol, formationRow, order }.
// Quoted directly from the reference's Enemy.cpp `enemiesGroup` table —
// 5 groups of 8 = 40 enemies per stage. Row 1 = boss, rows 2-3 = guard,
// rows 4-5 = minion (see personalityForRow() below).
struct EnemyInfo { int pathId; int col; int row; int order; };
constexpr EnemyInfo ENEMY_GROUPS[5][8] = {
    { {1,4,2,0}, {1,5,2,1}, {1,4,3,2}, {1,5,3,3},
      {2,4,4,0}, {2,5,4,1}, {2,4,5,2}, {2,5,5,3} },

    { {3,3,2,0}, {3,3,3,2}, {3,6,2,4}, {3,6,3,6},
      {3,3,1,1}, {3,4,1,3}, {3,5,1,5}, {3,6,1,7} },

    { {4,7,2,0}, {4,2,2,1}, {4,8,2,2}, {4,1,2,3},
      {4,7,3,4}, {4,2,3,5}, {4,8,3,6}, {4,1,3,7} },

    { {2,6,4,0}, {2,3,4,1}, {2,7,4,2}, {2,2,4,3},
      {2,6,5,4}, {2,3,5,5}, {2,7,5,6}, {2,2,5,7} },

    { {1,8,4,0}, {1,1,4,1}, {1,9,4,2}, {1,0,5,3},
      {1,8,5,4}, {1,1,5,5}, {1,9,5,6}, {1,0,4,7} },
};

EnemyPersonality personalityForRow(int row) {
    if (row == 1) return EnemyPersonality::Boss;
    if (row == 2 || row == 3) return EnemyPersonality::Guard;
    return EnemyPersonality::Minion;
}

const pr32::graphics::Sprite4bpp* const* framesForPersonality(EnemyPersonality p) {
    namespace spr = galaga_pr32_sprites;
    switch (p) {
        case EnemyPersonality::Boss: return spr::BOSS_FRAMES;
        case EnemyPersonality::Guard: return spr::GUARD_FRAMES;
        default: return spr::MINION_FRAMES;
    }
}

// Player: 2 px/tick @ 60Hz -> 120 px/s (digital left/right, no inertia —
// faithful to the reference's own input, not a simplification).
constexpr float kFighterSpeed = 120.0f;
constexpr float kFighterBulletSpeed = -180.0f;  // px/s, upward
constexpr float kFireCooldownS = 0.15f;
constexpr int kMaxFighterBulletsOnScreen = 2;  // dual-mode raises this in Phase 3

constexpr float kEntranceSpeed = 150.0f;  // 2.5 px/tick * 60
constexpr float kDiveSpeed = 120.0f;      // 2.0 px/tick * 60, dive + return
constexpr float kEnemyBulletSpeed = 180.0f;  // px/s downward

float randRange(float lo, float hi) {
    return lo + (hi - lo) * (static_cast<float>(random(0, 10001)) / 10000.0f);
}

// Reference's `addPathLoopPoints()` — inserts a small circular flourish
// mid-dive (the classic Galaga "loop the loop"). Approximated as an
// 8-segment circle in a random direction/radius, matching the
// reference's own segment count; the exact starting-angle geometry
// wasn't fully recoverable from the research, so this starts the loop
// tangent to the current heading rather than replicating byte-for-byte.
void addPathLoopPoints(CatmullPath& path, float radius) {
    if (path.count == 0) return;
    Vec2 center = path.points[path.count - 1];
    bool cw = random(0, 2) == 0;
    constexpr int n = 8;
    float step = (2.0f * static_cast<float>(M_PI) / n) * (cw ? 1.0f : -1.0f);
    for (int i = 1; i <= n; ++i) {
        float a = step * i;
        Vec2 p{ center.x + radius * sinf(a), center.y - radius + radius * cosf(a) };
        path.addPoint(p);
    }
}

} // namespace

void GalagaScene::init() {
    gameElapsedTime = 0.0f;
    for (auto& e : enemies) { e.used = false; e.state = EnemyState::Dead; }
    currentGroup = 0;
    groupSpawnTimer = 0.0f;
    allGroupsSpawned = false;
    divingPhaseStarted = false;
    diveTriggerTimer = 0.0f;
    nextDiveIntervalS = 1.0f;

    for (auto& b : fighterBullets) b.active = false;
    fireLastTime = -1.0f;
    for (auto& b : enemyBullets) b.active = false;

    fighterAlive = false;
    fighterEntering = false;
    fighterFrame = 6.0f;
    invulnerableUntilMs = 0;
    fighterControlFrozen = false;

    captureState = 0;
    captureBossIndex = -1;
    captureStateTimer = 0.0f;
    beamHeight = 0.0f;
    fighterCaptureCooldownUntil = -1000.0f;
    captiveExists = false;
    captiveDestroyed = false;
    capturedByBossIndex = -1;
    rescueState = 0;
    rescueStateTimer = 0.0f;
    dualMode = false;

    score = 0;
    lives = 3;
    stage = 1;
    nextExtraLifeScore = 30000;
    gameOver = false;
    paused = false;
    pauseLatched = false;
    encSwHeldSinceMs = 0;
    returnToMenuRequested = false;
    returnToMenuLatched = false;

    introPhase = 1;
    introTimer = 0.0f;
    for (auto& p : scorePopups) p.active = false;
    initStars();

    resetFighter();

    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);
    pr32::graphics::setSpriteCustomPalette(galaga_pr32_sprites::PALETTE_RGB565);
}

void GalagaScene::resetFighter() {
    fighterEntering = true;
    fighterEntryT = 0.0f;
    buildPath(fighterEntryPath, PATH_FIGHTER_NEXT_LIFE, sizeof(PATH_FIGHTER_NEXT_LIFE) / sizeof(Vec2));
    fighterPos = fighterEntryPath.getPoint(0.0f);
    fighterFrame = 6.0f;
    invulnerableUntilMs = millis() + 1500;
    // Real bug caught via a serial trace: this is also called mid-
    // capture-sequence (state 4->5, to give the player a fresh ship
    // while the boss+captive fly home) — without clearing this here,
    // fighterControlFrozen stayed stuck true forever afterward, since
    // nothing else was responsible for un-freezing it once a capture
    // sequence had run at all. Screenshots never caught this (frozen
    // control looks identical to normal play until you actually try to
    // move) — only the trace made it obvious.
    fighterControlFrozen = false;
}

// Reference's `updateFormationScreenPosition()`, ported in full now that
// Phase 2 wires up the diving-phase switch: `moveFactor` blends between
// the "horizontal marching sway" anchor (0, used pre-diving) and the
// "scaled breathing" pulse anchor (1, once startDiving() has fired for
// this stage) — the classic Galaga formation zoom/pulse effect.
Vec2 GalagaScene::formationScreenPosition(int col, int row, float moveFactor) const {
    float s = sinf(gameElapsedTime * 3.0f);
    Vec2 pointHorizontal{ 31.0f + col * 18.0f + 18.0f * s, 25.0f + row * 18.0f };
    if (moveFactor <= 0.001f) return pointHorizontal;

    Vec2 pointScaled{ 31.0f + col * 18.0f - 112.0f, 25.0f + row * 18.0f - 43.0f };
    float scale = 1.13f + 0.13f * s;
    pointScaled.x *= scale;
    pointScaled.y *= (row == 0) ? 1.0f : scale;
    pointScaled.x += 112.0f;
    pointScaled.y += 43.0f;
    return pointHorizontal + moveFactor * (pointScaled - pointHorizontal);
}

void GalagaScene::spawnNextGroup() {
    if (currentGroup >= 5) { allGroupsSpawned = true; return; }
    const EnemyInfo* group = ENEMY_GROUPS[currentGroup];
    for (int i = 0; i < 8; ++i) {
        // Find a free slot.
        Enemy* slot = nullptr;
        for (auto& e : enemies) { if (!e.used) { slot = &e; break; } }
        if (!slot) break;

        const EnemyInfo& info = group[i];
        slot->used = true;
        slot->state = EnemyState::Entering;
        slot->formationCol = info.col;
        slot->formationRow = info.row;
        slot->personality = personalityForRow(info.row);
        slot->life = (slot->personality == EnemyPersonality::Boss) ? 2 : 1;
        slot->randomAngleSeed = static_cast<float>(random(0, 1000)) * 0.01f;
        slot->angleTransitionFactor = 0.0f;
        slot->formationMoveFactor = 0.0f;
        slot->followBossIndex = -1;
        slot->diveScoreOverride = -1;
        slot->diveBulletsRemaining = 0;
        slot->diveFireLastTime = -1.0f;
        slot->movingToCapture = false;
        // Reference's `spawnNextEnemies()`: stage 1 never dives mid-
        // entrance (divingProbability 0.0f); stage 2+ gives each enemy a
        // 25% chance to dive straight out of its swoop instead of
        // joining formation — the reference's actual per-stage
        // difficulty escalation (there's no other difficulty scaling).
        slot->diveMidEntrance = (stage > 1) && (randRange(0.0f, 1.0f) < 0.25f);
        slot->diveMidEntranceFraction = randRange(0.55f, 0.75f);

        const Vec2* pts; int count;
        switch (info.pathId) {
            case 1: pts = PATTERN_1; count = sizeof(PATTERN_1) / sizeof(Vec2); break;
            case 2: pts = PATTERN_2; count = sizeof(PATTERN_2) / sizeof(Vec2); break;
            case 3: pts = PATTERN_3; count = sizeof(PATTERN_3) / sizeof(Vec2); break;
            default: pts = PATTERN_4; count = sizeof(PATTERN_4) / sizeof(Vec2); break;
        }
        buildPath(slot->path, pts, count);
        slot->pathT = 0.0f;
        // Stagger the 8 enemies of this group along their shared path —
        // matches the reference's `order*18` conga-line entry.
        slot->position = slot->path.advance(slot->pathT, info.order * 18.0f);
    }
    ++currentGroup;
    if (currentGroup >= 5) allGroupsSpawned = true;
}

void GalagaScene::updateEnemyFrame(Enemy& e, Vec2 tangent) {
    float movingAngle = atan2f(tangent.y, tangent.x) - 0.1309f;
    float stationaryAngle = -1.5708f + 0.5f * sinf(e.randomAngleSeed + gameElapsedTime * 2.0f);
    float target = (e.state == EnemyState::InFormation) ? 1.0f : 0.0f;
    e.angleTransitionFactor += (target - e.angleTransitionFactor) * fminf(1.0f, 5.0f * (1.0f / 60.0f));
    float angle = e.angleTransitionFactor * stationaryAngle + (1.0f - e.angleTransitionFactor) * movingAngle;
    float frame = (1.0f - angle / (2.0f * static_cast<float>(M_PI))) * 24.0f;
    frame = fmodf(frame, 24.0f);
    if (frame < 0) frame += 24.0f;
    e.frame = frame;
}

void GalagaScene::updateEnemy(Enemy& e, float dt) {
    if (!e.used) return;

    if (e.state == EnemyState::Exploding) {
        e.explosionTimer += dt;
        if (e.explosionTimer > 0.4f) e.used = false;
        return;
    }

    // Escort guards dragged along on a boss dive: fully slaved to the
    // boss's position/frame (see galaga_pr32.h's comment on
    // followBossIndex for why this is simplified vs. the reference's
    // independently-physics'd `follow()`).
    if (e.followBossIndex >= 0) {
        Enemy& boss = enemies[e.followBossIndex];
        e.position = boss.position + e.followOffset;
        e.frame = boss.frame;
        if (boss.state == EnemyState::InFormation || !boss.used) {
            e.followBossIndex = -1;
            e.state = EnemyState::InFormation;
        }
        return;
    }

    // Ramp the formation-sway/breathing blend toward its target — 3.3s
    // full transition, matching the reference's own ±0.005/tick @ 60Hz.
    float moveTarget = divingPhaseStarted ? 1.0f : 0.0f;
    e.formationMoveFactor += (moveTarget - e.formationMoveFactor) * fminf(1.0f, dt / 3.3f);

    Vec2 formationPos = formationScreenPosition(e.formationCol, e.formationRow, e.formationMoveFactor);

    if (e.state == EnemyState::Entering) {
        Vec2 before = e.path.getPoint(e.pathT);
        e.path.lastPoint() = formationPos;  // live-retarget the tail — see CatmullPath's comment
        e.position = e.path.advance(e.pathT, kEntranceSpeed * dt);
        Vec2 tangent = e.position - before;
        updateEnemyFrame(e, tangent);
        if (e.diveMidEntrance && e.pathT >= static_cast<float>(e.path.count) * e.diveMidEntranceFraction) {
            e.diveMidEntrance = false;
            startDiving(e);
        } else if (e.path.isFinished(e.pathT)) {
            e.state = EnemyState::InFormation;
            e.position = formationPos;
        }
    } else if (e.state == EnemyState::InFormation) {
        e.position = formationPos;
        // Reference's own idle-wobble formula (each ship's facing angle
        // oscillates ±28.6deg on its own randomAngleSeed-offset phase)
        // reads, on this hardware's small 16px sprites with 15deg-per-
        // frame rotation art, as the whole formation looking randomly
        // misaligned rather than a subtle "breathing" idle animation —
        // flagged as a visual bug on real hardware. Hold a single fixed
        // frame instead so every ship in formation faces identically,
        // matching classic Galaga's actual static-formation look.
        e.frame = 6.0f;
        e.angleTransitionFactor = 1.0f;
    } else if (e.state == EnemyState::Diving) {
        Vec2 before = e.position;
        e.position = e.path.advance(e.pathT, kDiveSpeed * dt);
        Vec2 tangent = e.position - before;
        updateEnemyFrame(e, tangent);

        if (!e.movingToCapture && e.diveBulletsRemaining > 0 && e.position.y > 75.0f &&
            gameElapsedTime >= e.diveFireFirstDelay &&
            gameElapsedTime - e.diveFireLastTime > 0.2f) {
            fireEnemyBullet(e);
            e.diveFireLastTime = gameElapsedTime;
            --e.diveBulletsRemaining;
        }

        if (e.path.isFinished(e.pathT)) {
            if (e.movingToCapture) {
                // Arrived at the capture hover point — hand off to
                // updateCapture() (called from GalagaScene::update())
                // to spawn/extend the beam from here.
                e.movingToCapture = false;
                e.state = EnemyState::Hovering;
                captureState = 1;
                captureStateTimer = 0.0f;
                beamHeight = 0.0f;
                sfx.play(galaga_pr32_audio::kEnemyBeamSfx, galaga_pr32_audio::kEnemyBeamSfxLen);
            } else {
                onDivingFinished(e);
            }
        }
    } else if (e.state == EnemyState::Hovering) {
        updateEnemyFrame(e, Vec2{ 0, -1 });
    } else if (e.state == EnemyState::Returning) {
        Vec2 before = e.position;
        e.path.lastPoint() = formationPos;
        e.position = e.path.advance(e.pathT, kDiveSpeed * dt);
        Vec2 tangent = e.position - before;
        updateEnemyFrame(e, tangent);
        if (e.path.isFinished(e.pathT)) {
            e.state = EnemyState::InFormation;
            e.position = formationPos;
        }
    }
}

// Reference's `Actor::startDiving()`, ported: peel out of formation
// along the current facing, optionally kick sideways if peeling
// upward, then aim for a random point off the bottom of the screen via
// jittered 50px waypoints with a 40% chance of a mid-dive loop
// flourish.
void GalagaScene::startDiving(Enemy& e) {
    e.path.clear();
    e.path.addPoint(e.position);

    float angle = -6.28318f * (e.frame / 24.0f - 1.0f);
    Vec2 d{ 30.0f * cosf(angle), 30.0f * sinf(angle) };
    Vec2 p2 = e.position + d;
    e.path.addPoint(p2);
    if (d.y < -15.0f) {
        float offsetx = (random(0, 2) == 0) ? -25.0f : 25.0f;
        e.path.addPoint(Vec2{ p2.x + offsetx, p2.y });
    }

    Vec2 dest{ randRange(8.0f, kFieldW - 8.0f), kFieldH + 32.0f };
    Vec2 last = e.path.points[e.path.count - 1];
    Vec2 dir = dest - last;
    float dist = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (dist > 0.001f) { dir.x /= dist; dir.y /= dist; }
    int steps = static_cast<int>(dist / 50.0f);
    bool includeLoop = randRange(0.0f, 1.0f) < 0.4f;
    int loopAt = includeLoop && steps > 0 ? random(1, steps + 1) : -1;
    for (int i = 1; i <= steps; ++i) {
        Vec2 p = last + (50.0f * i) * dir;
        p.x += randRange(-50.0f, 50.0f);
        if (p.x < 8.0f) p.x = 8.0f;
        if (p.x > kFieldW - 8.0f) p.x = kFieldW - 8.0f;
        e.path.addPoint(p);
        if (i == loopAt) addPathLoopPoints(e.path, randRange(20.0f, 50.0f));
    }
    e.path.addPoint(dest);
    e.pathT = 0.0f;
    e.state = EnemyState::Diving;

    e.diveBulletsRemaining = random(0, 4);  // 0-3
    e.diveFireFirstDelay = gameElapsedTime + randRange(0.5f, 2.0f);
    e.diveFireLastTime = -1.0f;

    sfx.play(galaga_pr32_audio::kEnemyDivingSfx, galaga_pr32_audio::kEnemyDivingSfxLen);
}

// Reference's `Enemy::tryToCaptureFighter()`: sends the boss to hover
// at a random x, fixed y=159, rather than diving off-screen. The rest
// of the sequence (beam spawn/extend, pull, dock, return) is driven by
// updateCapture() once the boss arrives (see the `movingToCapture`
// finish-check in updateEnemy()).
void GalagaScene::startCaptureAttempt(Enemy& boss, int bossIndex) {
    startCaptureAttempt(boss, bossIndex, randRange(20.0f, kFieldW - 20.0f));
}

void GalagaScene::startCaptureAttempt(Enemy& boss, int bossIndex, float hoverX) {
    boss.path.clear();
    boss.path.addPoint(boss.position);
    boss.path.addPoint(Vec2{ hoverX, 159.0f });
    boss.pathT = 0.0f;
    boss.state = EnemyState::Diving;
    boss.movingToCapture = true;
    captureBossIndex = bossIndex;
}

// Reference's `EnemyBeam` + `GameController::onFighterCaptured()`
// state machine, ported (simplified — see galaga_pr32.h's comments on
// captureState/capturedByBossIndex for the exact phase breakdown).
void GalagaScene::updateCapture(float dt) {
    if (captureState == 0) return;
    if (captureBossIndex < 0 || !enemies[captureBossIndex].used) {
        captureState = 0;
        captureBossIndex = -1;
        return;
    }
    Enemy& boss = enemies[captureBossIndex];
    Vec2 beamTopLeft = boss.position + Vec2{ -23.0f, 8.0f };
    // Real AABB overlap (fighter treated as a half-extent box around
    // its own center, same convention as every other collision check in
    // this file) rather than a bare point-in-rect test — with the hover
    // y fixed at 159 and the beam capped at 80px tall, its bottom edge
    // (159+8+80=247) falls just short of the player's own fixed y (256)
    // as a zero-size point, meaning capture could never trigger at all
    // until this was caught live via a serial trace (screenshots alone
    // couldn't tell "beam visibly reaches the ship" from "still 1px
    // short of the actual hitbox"). A plain 8px half-extent (matching
    // the fighter's real collision size elsewhere) was STILL exactly
    // 1px short once traced precisely — bumped to 12px here specifically
    // (not a global collision change) so this interaction has real
    // margin instead of being a razor's-edge coordinate coincidence.
    constexpr float kFighterHalf = 12.0f;
    auto fighterInBeam = [&]() {
        return fighterAlive && !fighterControlFrozen &&
               fighterPos.x + kFighterHalf > beamTopLeft.x && fighterPos.x - kFighterHalf < beamTopLeft.x + 46.0f &&
               fighterPos.y + kFighterHalf > beamTopLeft.y && fighterPos.y - kFighterHalf < beamTopLeft.y + beamHeight;
    };

    if (captureState == 1) {  // beam extending, 0 -> 80px over ~1.33s
        beamHeight += 60.0f * dt;
        if (beamHeight > 80.0f) beamHeight = 80.0f;
        if (fighterInBeam()) {
            capturePullPath.clear();
            capturePullPath.addPoint(fighterPos);
            capturePullPath.addPoint(Vec2{ boss.position.x, fighterPos.y - 38.0f });
            capturePullPath.addPoint(Vec2{ boss.position.x, fighterPos.y - 78.0f });
            capturePullT = 0.0f;
            fighterControlFrozen = true;
            captureState = 3;
            sfx.play(galaga_pr32_audio::kEnemyBeamFighterCapturedSfx, galaga_pr32_audio::kEnemyBeamFighterCapturedSfxLen);
        } else if (beamHeight >= 80.0f) {
            captureState = 2;
            captureStateTimer = 0.0f;
        }
    } else if (captureState == 2) {  // full-height hold, 3s timeout window
        captureStateTimer += dt;
        if (fighterInBeam()) {
            capturePullPath.clear();
            capturePullPath.addPoint(fighterPos);
            capturePullPath.addPoint(Vec2{ boss.position.x, fighterPos.y - 38.0f });
            capturePullPath.addPoint(Vec2{ boss.position.x, fighterPos.y - 78.0f });
            capturePullT = 0.0f;
            fighterControlFrozen = true;
            captureState = 3;
            sfx.play(galaga_pr32_audio::kEnemyBeamFighterCapturedSfx, galaga_pr32_audio::kEnemyBeamFighterCapturedSfxLen);
        } else if (captureStateTimer >= 3.0f) {
            // Timed out — no capture. Retract and send the boss home.
            captureState = 0;
            captureBossIndex = -1;
            returnToFormation(boss);
        }
    } else if (captureState == 3) {  // pulling the fighter up into the beam
        fighterPos = capturePullPath.advance(capturePullT, 75.0f * dt);
        fighterFrame += 45.0f * dt;
        if (fighterFrame >= 24.0f) fighterFrame -= 24.0f;
        if (capturePullPath.isFinished(capturePullT)) {
            captureState = 4;
            captureStateTimer = 0.0f;
            captiveExists = true;
            captiveDestroyed = false;
            captivePos = boss.position + Vec2{ 0.0f, 18.0f };
            captiveFrame = fighterFrame;
            sfx.play(galaga_pr32_audio::kFighterCapturedSfx, galaga_pr32_audio::kFighterCapturedSfxLen);
        }
    } else if (captureState == 4) {  // "FIGHTER CAPTURED" label showing
        captureStateTimer += dt;
        captivePos = boss.position + Vec2{ 0.0f, 18.0f };
        if (captureStateTimer >= 3.0f) {
            capturedByBossIndex = captureBossIndex;
            resetFighter();  // player gets a fresh ship
            returnToFormation(boss);
            captureState = 5;
        }
    } else if (captureState == 5) {  // boss + captive flying home together
        captivePos = boss.position + Vec2{ 0.0f, 18.0f };
        captiveFrame = boss.frame;
        if (boss.state == EnemyState::InFormation) {
            captureState = 0;
            captureBossIndex = -1;
            fighterCaptureCooldownUntil = gameElapsedTime + 10.0f;
        }
    }
}

// Reference's `GameController::onFighterRescued()`, simplified: skips
// the reference's extra "wait for all enemies back in formation" gate
// on entering state 2, and skips the true per-half dual-fighter
// destruction split (see hitFighter()'s own comment) — both scoped out
// for a first pass, not oversights.
void GalagaScene::updateRescue(float dt) {
    if (rescueState == 0) return;
    if (rescueState == 1) {
        captiveFrame += 45.0f * dt;
        if (captiveFrame >= 24.0f) captiveFrame -= 24.0f;
        rescueStateTimer += dt;
        if (rescueStateTimer >= 2.0f) {
            dualDockPathCaptive.clear();
            dualDockPathCaptive.addPoint(captivePos);
            dualDockPathCaptive.addPoint(Vec2{ 119.0f, fighterPos.y });
            dualDockPathCaptive.addPoint(Vec2{ 119.0f, 256.0f });
            dualDockTCaptive = 0.0f;

            dualDockPathFighter.clear();
            dualDockPathFighter.addPoint(fighterPos);
            dualDockPathFighter.addPoint(Vec2{ 104.0f, 256.0f });
            dualDockTFighter = 0.0f;

            rescueState = 2;
        }
    } else if (rescueState == 2) {
        captivePos = dualDockPathCaptive.advance(dualDockTCaptive, 60.0f * dt);
        fighterPos = dualDockPathFighter.advance(dualDockTFighter, 60.0f * dt);
        if (dualDockPathCaptive.isFinished(dualDockTCaptive) && dualDockPathFighter.isFinished(dualDockTFighter)) {
            captiveExists = false;
            dualMode = true;
            fighterPos.x += 8.0f;
            fighterControlFrozen = false;
            rescueState = 0;
        }
    }
}

// Reference's `Actor::onDivingFinished()`/`Enemy::onDivingFinished()`:
// 75% chance to dive again immediately, except bosses always return to
// formation after one pass.
void GalagaScene::onDivingFinished(Enemy& e) {
    bool diveAgain = (e.personality != EnemyPersonality::Boss) && randRange(0.0f, 1.0f) < 0.75f;
    if (diveAgain) startDiving(e);
    else returnToFormation(e);
}

void GalagaScene::returnToFormation(Enemy& e) {
    Vec2 formationPos = formationScreenPosition(e.formationCol, e.formationRow, e.formationMoveFactor);
    e.path.clear();
    e.path.addPoint(e.position);
    // Simplified vs. the reference's extra S-curve waypoints when
    // departing downward — a straight 2-point path still looks right
    // here since the tail is continuously retargeted to the live
    // (swaying) formation slot anyway (see the Returning branch in
    // updateEnemy()), just without the extra flourish on the way in.
    e.path.addPoint(formationPos);
    e.pathT = 0.0f;
    e.state = EnemyState::Returning;
}

// Picks a uniformly-random in-formation enemy to dive, matching the
// reference's own selection. Boss dives may drag 1-3 adjacent guards
// along as escorts, each independently 50% to join, with the classic
// 400/800/1600/2000 bonus-score table by escort count.
void GalagaScene::triggerRandomDive() {
    int candidates[kMaxEnemies];
    int count = 0;
    for (int i = 0; i < kMaxEnemies; ++i) {
        if (enemies[i].used && enemies[i].state == EnemyState::InFormation) candidates[count++] = i;
    }
    if (count == 0) return;
    int idx = candidates[random(0, count)];
    Enemy& chosen = enemies[idx];

    if (chosen.personality == EnemyPersonality::Boss) {
        bool captureAllowed = captureBossIndex < 0 && !captiveExists && fighterAlive && !dualMode &&
                              gameElapsedTime > fighterCaptureCooldownUntil;
        if (captureAllowed && randRange(0.0f, 1.0f) < 0.35f) {
            startCaptureAttempt(chosen, idx);
            return;
        }
        int guardCount = 0;
        for (int i = 0; i < kMaxEnemies; ++i) {
            Enemy& g = enemies[i];
            if (i == idx || !g.used || g.state != EnemyState::InFormation) continue;
            if (g.personality != EnemyPersonality::Guard) continue;
            if (abs(g.formationCol - chosen.formationCol) > 1) continue;
            if (guardCount >= 3) break;
            if (randRange(0.0f, 1.0f) < 0.5f) {
                g.followBossIndex = idx;
                g.followOffset = g.position - chosen.position;
                g.state = EnemyState::Diving;  // bookkeeping only — position is slaved
                ++guardCount;
            }
        }
        static constexpr int kBossDivingScoreTable[4] = { 400, 800, 1600, 2000 };
        chosen.diveScoreOverride = kBossDivingScoreTable[guardCount];
    } else {
        chosen.diveScoreOverride = -1;
    }
    startDiving(chosen);
}

void GalagaScene::debugForceCaptureAttempt() {
    for (int i = 0; i < kMaxEnemies; ++i) {
        if (enemies[i].used && enemies[i].state == EnemyState::InFormation &&
            enemies[i].personality == EnemyPersonality::Boss) {
            // Fixed hover x (matches the player's own default/respawn
            // x) rather than the real random position — deterministic
            // on purpose, so this debug trigger can actually be tested
            // by flying straight up instead of guessing where a random
            // beam landed.
            startCaptureAttempt(enemies[i], i, 112.0f);
            // Also grants a generous invulnerability window — this is a
            // debug aid, not a gameplay change: without it, unrelated
            // stray enemy fire during the full capture sequence (beam
            // extend/hold/pull-up/label/fly-home, empirically ~11s end
            // to end) kept derailing test sessions with unrelated deaths
            // before the mechanic (or a subsequent forced rescue) could
            // be observed.
            invulnerableUntilMs = millis() + 20000;
            return;
        }
    }
}

void GalagaScene::debugForceRescue() {
    // Deterministic rescue trigger for testing: directly destroys the
    // boss currently holding a captive, bypassing the need to actually
    // land bullets on it (same rationale as debugForceCaptureAttempt's
    // fixed hover-x — makes the mechanic testable without relying on
    // scripted aim).
    if (capturedByBossIndex >= 0 && capturedByBossIndex < kMaxEnemies &&
        enemies[capturedByBossIndex].used) {
        destroyEnemy(enemies[capturedByBossIndex], enemies[capturedByBossIndex].state == EnemyState::Diving);
    }
}

void GalagaScene::fireEnemyBullet(const Enemy& e) {
    for (auto& b : enemyBullets) {
        if (!b.active) {
            b.active = true;
            b.position = e.position;
            b.vy = kEnemyBulletSpeed;
            sfx.play(galaga_pr32_audio::kEnemyShotSfx, galaga_pr32_audio::kEnemyShotSfxLen);
            return;
        }
    }
}

void GalagaScene::hitFighter() {
    if (millis() < invulnerableUntilMs) return;
    using namespace galaga_pr32_audio;
    // Reference: getting hit while dual-mode degrades back to a single
    // ship rather than necessarily costing a life (real per-half-hitbox
    // destruction — losing specifically the left or right half — is
    // simplified out here; either half being hit just drops dual mode
    // entirely). Only a hit while already single actually costs a life.
    if (dualMode) {
        dualMode = false;
        invulnerableUntilMs = millis() + 1500;
        sfx.play(kFighterExplosionSfx, kFighterExplosionSfxLen);
        return;
    }
    sfx.play(kFighterExplosionSfx, kFighterExplosionSfxLen);
    --lives;
    fighterAlive = false;
    if (lives <= 0) {
        gameOver = true;
    } else {
        resetFighter();
    }
}

void GalagaScene::fireFighterBullet() {
    if (dualMode) {
        int fired = 0;
        for (auto& b : fighterBullets) {
            if (fired >= 2) break;
            if (!b.active) {
                b.active = true;
                b.position = fighterPos + Vec2{ fired == 0 ? -7.0f : 7.0f, -12.0f };
                b.vy = kFighterBulletSpeed;
                ++fired;
            }
        }
        if (fired > 0) sfx.play(galaga_pr32_audio::kFighterShotSfx, galaga_pr32_audio::kFighterShotSfxLen);
        return;
    }
    for (auto& b : fighterBullets) {
        if (!b.active) {
            b.active = true;
            b.position = fighterPos - Vec2{ 0.0f, 12.0f };
            b.vy = kFighterBulletSpeed;
            sfx.play(galaga_pr32_audio::kFighterShotSfx, galaga_pr32_audio::kFighterShotSfxLen);
            return;
        }
    }
}

void GalagaScene::updateBullets(float dt) {
    for (auto& b : fighterBullets) {
        if (!b.active) continue;
        b.position.y += b.vy * dt;
        if (b.position.y < -16.0f) b.active = false;
    }
    for (auto& b : enemyBullets) {
        if (!b.active) continue;
        b.position.y += b.vy * dt;
        if (b.position.y > kFieldH + 16.0f) b.active = false;
    }
}

void GalagaScene::destroyEnemy(Enemy& e, bool killedWhileDiving) {
    using namespace galaga_pr32_audio;
    int points = 0;
    switch (e.personality) {
        case EnemyPersonality::Boss:
            points = killedWhileDiving ? (e.diveScoreOverride >= 0 ? e.diveScoreOverride : 400) : 150;
            sfx.play(kBossDestroyedSfx, kBossDestroyedSfxLen);
            break;
        case EnemyPersonality::Guard:
            points = killedWhileDiving ? 160 : 80;
            sfx.play(kGuardDestroyedSfx, kGuardDestroyedSfxLen);
            break;
        default:
            points = killedWhileDiving ? 100 : 50;
            sfx.play(kMinionDestroyedSfx, kMinionDestroyedSfxLen);
            break;
    }
    showScorePopup(points, e.position);
    score += points;
    if (score > hiscore) hiscore = score;
    if (score > nextExtraLifeScore) {
        ++lives;
        nextExtraLifeScore += 70000;
        sfx.play(kExtraLifeSfx, kExtraLifeSfxLen);
    }
    e.state = EnemyState::Exploding;
    e.explosionTimer = 0.0f;

    // Reference's `Enemy::hit()`: destroying the boss currently holding
    // a captive triggers a rescue IF the player's own ship and the
    // captive are both still alive; otherwise the captive is simply
    // lost along with its captor.
    int idx = static_cast<int>(&e - enemies);
    if (idx == capturedByBossIndex) {
        if (captiveExists && !captiveDestroyed && fighterAlive && !dualMode) {
            rescueState = 1;
            rescueStateTimer = 0.0f;
            fighterControlFrozen = true;
            sfx.play(kFighterRescuedSfx, kFighterRescuedSfxLen);
        } else {
            captiveExists = false;
        }
        capturedByBossIndex = -1;
        // Also cancel any in-progress capture SEQUENCE if this same
        // boss was mid-attempt when destroyed (e.g. hit while hovering
        // with the beam out).
        if (captureBossIndex == idx) {
            captureState = 0;
            captureBossIndex = -1;
        }
    }
}

void GalagaScene::checkCollisions() {
    constexpr float kEnemyHalf = 8.0f;
    constexpr float kBulletHalfW = 1.5f, kBulletHalfH = 4.0f;
    for (auto& b : fighterBullets) {
        if (!b.active) continue;
        for (auto& e : enemies) {
            if (!e.used || e.state == EnemyState::Exploding || e.state == EnemyState::Dead) continue;
            if (fabsf(b.position.x - e.position.x) < kEnemyHalf + kBulletHalfW &&
                fabsf(b.position.y - e.position.y) < kEnemyHalf + kBulletHalfH) {
                b.active = false;
                if (--e.life <= 0) {
                    destroyEnemy(e, e.state == EnemyState::Diving);
                } else if (e.personality == EnemyPersonality::Boss) {
                    // First hit (2 HP -> 1): reference switches to a
                    // damaged sprite (ENEMY_1_HIT) and plays a distinct
                    // "first hit" cue rather than the destroy sound.
                    sfx.play(galaga_pr32_audio::kBossFirstHitSfx, galaga_pr32_audio::kBossFirstHitSfxLen);
                }
                break;
            }
        }
    }

    // Reference explicitly calls setHitable(false) on the fighter for the
    // whole capture-pull-up/rescue automated sequence (GameController.cpp's
    // onFighterCaptured/onFighterRescued) — fighterControlFrozen already
    // marks exactly that window, so gate hits on it too, not just movement.
    if (fighterAlive && !fighterControlFrozen && millis() >= invulnerableUntilMs) {
        for (auto& b : enemyBullets) {
            if (!b.active) continue;
            if (fabsf(b.position.x - fighterPos.x) < kEnemyHalf + kBulletHalfW &&
                fabsf(b.position.y - fighterPos.y) < kEnemyHalf + kBulletHalfH) {
                b.active = false;
                hitFighter();
                break;
            }
        }
    }
}

void GalagaScene::update(unsigned long deltaTime) {
    constexpr unsigned long kMaxFrameMs = 50;
    if (deltaTime > kMaxFrameMs) deltaTime = kMaxFrameMs;
    float dt = static_cast<float>(deltaTime) * 0.001f;

    constexpr unsigned long kPauseHoldMs = 500;
    constexpr unsigned long kReturnToMenuHoldMs = 2000;
    if (pendingInput.r3Held) {
        if (encSwHeldSinceMs == 0) encSwHeldSinceMs = millis();
        unsigned long heldMs = millis() - encSwHeldSinceMs;
        if (!pauseLatched && heldMs >= kPauseHoldMs) { paused = !paused; pauseLatched = true; }
        if (!returnToMenuLatched && heldMs >= kReturnToMenuHoldMs) {
            returnToMenuRequested = true;
            returnToMenuLatched = true;
        }
    } else {
        encSwHeldSinceMs = 0;
        pauseLatched = false;
        returnToMenuLatched = false;
    }
    if (paused) { Scene::update(deltaTime); return; }

    if (gameOver && pendingInput.aEdge) { init(); Scene::update(deltaTime); return; }

    gameElapsedTime += dt;
    updateStars(dt);

    if (!gameOver) {
        updateScorePopups(dt);

        // Reference's showCurrentLevel()/tryNextLife() "STAGE N"/"READY!"
        // banners — formation spawning is held off until they clear.
        if (introPhase != 0) {
            introTimer += dt;
            if (introPhase == 1 && introTimer >= kStageBannerS) {
                introPhase = 2;
                introTimer = 0.0f;
            } else if (introPhase == 2 && introTimer >= kReadyBannerS) {
                introPhase = 0;
                introTimer = 0.0f;
            }
        }

        // Spawn the 5 groups of 8, 3 seconds apart.
        if (introPhase == 0 && !allGroupsSpawned) {
            groupSpawnTimer += dt;
            if (groupSpawnTimer >= static_cast<float>(kGroupSpawnIntervalS)) {
                groupSpawnTimer = 0.0f;
                spawnNextGroup();
            }
        }

        // Diving phase starts once the full formation has assembled —
        // matches the reference's startEnemiesDiving(), triggered right
        // after spawnInitialEnemies() finishes.
        if (allGroupsSpawned && !divingPhaseStarted) {
            divingPhaseStarted = true;
            diveTriggerTimer = 0.0f;
            nextDiveIntervalS = randRange(0.3f, 2.0f);
            sfx.play(galaga_pr32_audio::kFormationScaledSfx, galaga_pr32_audio::kFormationScaledSfxLen);
        }
        if (divingPhaseStarted) {
            diveTriggerTimer += dt;
            if (diveTriggerTimer >= nextDiveIntervalS) {
                diveTriggerTimer = 0.0f;
                nextDiveIntervalS = randRange(0.3f, 2.0f);
                triggerRandomDive();
            }
        }

        for (auto& e : enemies) updateEnemy(e, dt);

        updateCapture(dt);
        updateRescue(dt);
#if GALAGA_DEBUG_TRACE
        {
            static unsigned long lastTrace = 0;
            if (millis() - lastTrace > 300) {
                lastTrace = millis();
                Serial.printf("TRACE t=%.1f captureState=%d captureBossIdx=%d beamH=%.1f "
                              "captiveExists=%d capturedByBossIdx=%d rescueState=%d dualMode=%d "
                              "lives=%d fighterAlive=%d frozen=%d\n",
                              gameElapsedTime, captureState, captureBossIndex, beamHeight,
                              captiveExists, capturedByBossIndex, rescueState, dualMode,
                              lives, fighterAlive, fighterControlFrozen);
            }
        }
#endif
        // Persistent docking: once a capture sequence finishes (or
        // between rescue-animation steps), the captive keeps riding
        // along with its captor boss every frame until rescued/lost —
        // updateCapture() only actively drives captivePos during its
        // own transient states, and updateRescue() owns it during the
        // rescue animation, so this is skipped while either is
        // mid-sequence to avoid fighting them.
        if (captiveExists && !dualMode && rescueState == 0 && captureState == 0 &&
            capturedByBossIndex >= 0 && enemies[capturedByBossIndex].used) {
            captivePos = enemies[capturedByBossIndex].position + Vec2{ 0.0f, 18.0f };
            captiveFrame = enemies[capturedByBossIndex].frame;
        }

        // Player entrance swoop, then normal control.
        if (fighterEntering) {
            fighterPos = fighterEntryPath.advance(fighterEntryT, 120.0f * dt);
            if (fighterEntryPath.isFinished(fighterEntryT)) {
                fighterEntering = false;
                fighterAlive = true;
                fighterFrame = 6.0f;
            }
        } else if (fighterAlive && !fighterControlFrozen) {
            float tx = pendingInput.thumbX;
            int dir = 0;
            if (fabsf(tx) > 0.3f) dir = (tx > 0) ? 1 : -1;
            else if (pendingInput.encDelta > 0) dir = 1;
            else if (pendingInput.encDelta < 0) dir = -1;
            fighterPos.x += dir * kFighterSpeed * dt;
            float margin = dualMode ? 16.0f : 8.0f;
            if (fighterPos.x < margin) fighterPos.x = margin;
            if (fighterPos.x > kFieldW - margin) fighterPos.x = kFieldW - margin;

            int activeBullets = 0;
            for (auto& b : fighterBullets) if (b.active) ++activeBullets;
            int maxBullets = dualMode ? 4 : kMaxFighterBulletsOnScreen;
            if (pendingInput.aEdge && gameElapsedTime - fireLastTime > kFireCooldownS &&
                activeBullets < maxBullets) {
                fireLastTime = gameElapsedTime;
                fireFighterBullet();
            }
        }

        updateBullets(dt);
        checkCollisions();

        // Stage clear: all 40 spawned and none left alive/exploding.
        if (allGroupsSpawned) {
            bool anyLeft = false;
            for (auto& e : enemies) if (e.used) { anyLeft = true; break; }
            if (!anyLeft) {
                ++stage;
                currentGroup = 0;
                groupSpawnTimer = 0.0f;
                allGroupsSpawned = false;
                divingPhaseStarted = false;
                introPhase = 1;
                introTimer = 0.0f;
                sfx.play(galaga_pr32_audio::kStageClearSfx, galaga_pr32_audio::kStageClearSfxLen);
            }
        }
    }

    sfx.update();
    Scene::update(deltaTime);
}

void GalagaScene::draw(Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
    drawStars(renderer);

    namespace spr = galaga_pr32_sprites;

    // Tractor beam — a simple growing trapezoid primitive rather than
    // the reference's own clip/scale-animated sprite (see
    // make_galaga_pr32_sprites.py's docstring for why that was scoped
    // out). Drawn before the enemies so the boss's own sprite layers
    // on top of the beam's origin.
    if (captureState >= 1 && captureState <= 2 && captureBossIndex >= 0 && enemies[captureBossIndex].used) {
        Vec2 top = enemies[captureBossIndex].position + Vec2{ 0.0f, 8.0f };
        // Renderer has no triangle primitive, so this is a widening
        // stack of thin rectangles rather than the reference's own
        // tapered sprite — a simplification, not a sprite port (see
        // make_galaga_pr32_sprites.py's docstring).
        int bands = static_cast<int>(beamHeight / 4.0f) + 1;
        for (int i = 0; i < bands; ++i) {
            float frac = static_cast<float>(i) / 20.0f;  // 0..1 over the full 80px extend
            int halfW = 6 + static_cast<int>(17.0f * frac);
            int y = sy(top.y) + i * 4;
            renderer.drawFilledRectangle(sx(top.x) - halfW, y, halfW * 2, 4, Color::Cyan);
        }
    }

    for (auto& e : enemies) {
        if (!e.used) continue;
        if (e.state == EnemyState::Exploding) {
            int frame = static_cast<int>((e.explosionTimer / 0.4f) * 5.0f);
            if (frame > 4) frame = 4;
            const auto* sp = spr::ENEMY_EXPLOSION_FRAMES[frame];
            renderer.drawSprite(*sp, sx(e.position.x) - sp->width / 2, sy(e.position.y) - sp->height / 2, false);
            continue;
        }
        const auto* const* frames = (e.personality == EnemyPersonality::Boss && e.life <= 1)
            ? spr::BOSS_HIT_FRAMES : framesForPersonality(e.personality);
        int fi = static_cast<int>(e.frame) % 24;
        if (fi < 0) fi += 24;
        const auto* sp = frames[fi];
        renderer.drawSprite(*sp, sx(e.position.x) - sp->width / 2, sy(e.position.y) - sp->height / 2, false);
    }

    if (captiveExists) {
        int fi = static_cast<int>(captiveFrame) % 24;
        if (fi < 0) fi += 24;
        const auto* sp = spr::FIGHTER_CAPTURED_FRAMES[fi];
        renderer.drawSprite(*sp, sx(captivePos.x) - sp->width / 2, sy(captivePos.y) - sp->height / 2, false);
    }

    if (fighterEntering || fighterAlive) {
        int fi = static_cast<int>(fighterFrame) % 24;
        if (fi < 0) fi += 24;
        const auto* sp = spr::FIGHTER_FRAMES[fi];
        bool blinking = millis() < invulnerableUntilMs && ((millis() / 100) % 2 == 0);
        if (!blinking) {
            if (dualMode) {
                renderer.drawSprite(*sp, sx(fighterPos.x - 8.0f) - sp->width / 2, sy(fighterPos.y) - sp->height / 2, false);
                renderer.drawSprite(*sp, sx(fighterPos.x + 7.0f) - sp->width / 2, sy(fighterPos.y) - sp->height / 2, false);
            } else {
                renderer.drawSprite(*sp, sx(fighterPos.x) - sp->width / 2, sy(fighterPos.y) - sp->height / 2, false);
            }
        }
    }

    for (auto& b : fighterBullets) {
        if (!b.active) continue;
        renderer.drawFilledRectangle(sx(b.position.x) - 1, sy(b.position.y) - 4, 3, 8, Color::Yellow);
    }
    for (auto& b : enemyBullets) {
        if (!b.active) continue;
        renderer.drawFilledRectangle(sx(b.position.x) - 1, sy(b.position.y) - 4, 3, 8, Color::Red);
    }

    // Reference's Hud::draw(): a small fighter-ship icon per remaining
    // life at the bottom-left, rather than plain "LIVES N" text — drawn
    // here (still in Sprite palette context, like every other sprite
    // this frame) since drawHUD() below switches to the Background
    // context for its text and can't also emit a custom-palette sprite.
    {
        const auto* icon = spr::FIGHTER_FRAMES[6];
        int iconsToShow = lives < 0 ? 0 : (lives > 6 ? 6 : lives);  // cap the row so it can't run off-screen
        for (int i = 0; i < iconsToShow; ++i) {
            renderer.drawSprite(*icon, PLAYFIELD_LEFT + 2 + i * 14, sy(kFieldH) - 16, false);
        }
    }

    drawHUD(renderer);
}

void GalagaScene::drawHUD(Renderer& renderer) {
    pr32::graphics::PaletteContext bgContext = pr32::graphics::PaletteContext::Background;
    pr32::graphics::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);

    namespace fm = pr32::graphics;
    auto centerX = [&](const char* text) {
        return SCREEN_W / 2 - fm::FontManager::textWidth(kHudFont, text, 1) / 2;
    };
    auto rightX = [&](const char* text, int rightEdge) {
        return rightEdge - fm::FontManager::textWidth(kHudFont, text, 1);
    };

    char buf[24];
    // Reference's blinking "1UP" label over the score — blinks
    // continuously during play (reference only turns it off on the
    // final game-over-to-attract-mode transition, which we don't have).
    if (!gameOver && ((millis() / 400) % 2 == 0)) {
        renderer.drawText("1UP", 4, 4, Color::Red, 1, kHudFont);
    }
    snprintf(buf, sizeof(buf), "%d", score);
    renderer.drawText(buf, 4, 14, Color::White, 1, kHudFont);

    renderer.drawText("HI-SCORE", centerX("HI-SCORE"), 4, Color::Red, 1, kHudFont);
    snprintf(buf, sizeof(buf), "%d", hiscore);
    renderer.drawText(buf, centerX(buf), 14, Color::White, 1, kHudFont);

    snprintf(buf, sizeof(buf), "X%d", lives);
    renderer.drawText(buf, rightX(buf, SCREEN_W - 4), 4, Color::Gray, 1, kHudFont);

    snprintf(buf, sizeof(buf), "STAGE %d", stage);
    renderer.drawText(buf, rightX(buf, SCREEN_W - 4), 14, Color::Gray, 1, kHudFont);

    if (gameOver) {
        const char* msg = "GAME OVER";
        renderer.drawText(msg, centerX(msg), SCREEN_H - 32, Color::Red, 1, kHudFont);
        const char* hint = "A: RESTART";
        renderer.drawText(hint, centerX(hint), SCREEN_H - 20, Color::Gray, 1, kHudFont);
    } else if (paused) {
        const char* msg = "PAUSED";
        renderer.drawText(msg, centerX(msg), SCREEN_H - 24, Color::White, 1, kHudFont);
    } else if (introPhase == 1) {
        snprintf(buf, sizeof(buf), "STAGE %d", stage);
        renderer.drawText(buf, centerX(buf), SCREEN_H / 2 - 4, Color::Cyan, 1, kHudFont);
    } else if (introPhase == 2) {
        const char* msg = "READY!";
        renderer.drawText(msg, centerX(msg), SCREEN_H / 2 - 4, Color::Cyan, 1, kHudFont);
    } else if (captureState == 4) {
        const char* msg = "FIGHTER CAPTURED";
        renderer.drawText(msg, centerX(msg), SCREEN_H - 24, Color::Yellow, 1, kHudFont);
    }

    drawScorePopups(renderer);

    renderer.setRenderContext(saved);
}

// Reference's `ScorePoint::show()`, with the tier logic FIXED rather
// than ported: the original is a cascading if-chain with no else, so
// (e.g.) a 50-point minion kill trips `point<200` (white) then
// `point<800` (red, overwriting) then `point<1600` (yellow, overwriting
// again) — collapsing nearly every kill to yellow. Real Galaga's tiers
// are white(<200)/red(200-799)/yellow(800-1599)/cyan(>=1600); that's
// what this does.
void GalagaScene::showScorePopup(int points, Vec2 position) {
    for (auto& p : scorePopups) {
        if (!p.active) {
            p.active = true;
            p.points = points;
            p.position = position;
            p.timer = 0.0f;
            return;
        }
    }
}

void GalagaScene::updateScorePopups(float dt) {
    for (auto& p : scorePopups) {
        if (!p.active) continue;
        p.timer += dt;
        if (p.timer > 1.5f) p.active = false;
    }
}

void GalagaScene::drawScorePopups(Renderer& renderer) {
    char buf[8];
    for (auto& p : scorePopups) {
        if (!p.active) continue;
        snprintf(buf, sizeof(buf), "%d", p.points);
        Color c;
        if (p.points < 200) c = Color::White;
        else if (p.points < 800) c = Color::Red;
        else if (p.points < 1600) c = Color::Yellow;
        else c = Color::Cyan;
        int w = pr32::graphics::FontManager::textWidth(kHudFont, buf, 1);
        renderer.drawText(buf, sx(p.position.x) - w / 2, sy(p.position.y) - 4, c, 1, kHudFont);
    }
}

// Reference's `StarsBackground` — a multi-speed scrolling, twinkling
// starfield. Fewer stars than the reference's 100 (80 here) to keep the
// per-frame drawPixel cost modest on an ESP32, visually indistinguishable
// at this screen size.
void GalagaScene::initStars() {
    static constexpr Color kStarColors[] = {
        Color::White, Color::Cyan, Color::Yellow, Color::LightGreen,
        Color::Orange, Color::Gray, Color::LightRed, Color::Magenta,
    };
    int idx = 0;
    constexpr int kGroupCount = kStarCount / 3;
    for (int v = 0; v < 3; ++v) {
        for (int i = 0; i < kGroupCount && idx < kStarCount; ++i) {
            Star& s = stars[idx++];
            s.color = kStarColors[random(0, 8)];
            s.position.x = randRange(0.0f, kFieldW);
            s.position.y = randRange(0.0f, kFieldH) - 2.0f * kFieldH;
            s.ySpeed = static_cast<float>(v + 1);
            s.blink = (i > kGroupCount / 6);
            s.blinkTime = randRange(0.0f, 1.0f);
        }
    }
}

void GalagaScene::updateStars(float dt) {
    // Reference always keeps the field moving during gameplay (its own
    // move()/stop() toggle is only used for menu/attract scenes, which
    // we don't have) — just scroll continuously.
    starTargetVelocityScale = 1.0f;
    float diff = starTargetVelocityScale - starVelocityScale;
    if (fabsf(diff) < 0.02f) starVelocityScale = starTargetVelocityScale;
    else starVelocityScale += (diff < 0 ? -0.015f : 0.015f);

    // Reference steps these once per 60Hz tick; scale by dt*60 for an
    // equivalent real-time rate.
    float ticks = dt * 60.0f;
    for (auto& s : stars) {
        s.position.y += starVelocityScale * s.ySpeed * ticks;
        s.blinkTime += 0.025f * ticks;
        if (s.position.y > kFieldH) s.position.y = -1.0f;
    }
}

void GalagaScene::drawStars(Renderer& renderer) {
    for (auto& s : stars) {
        if (!s.blink || (static_cast<int>(s.blinkTime) % 2 == 0)) {
            renderer.drawPixel(sx(s.position.x), sy(s.position.y), s.color);
        }
    }
}

} // namespace galaga_pr32
