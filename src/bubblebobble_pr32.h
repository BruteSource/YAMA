// Phase 2+3 of the PixelRoot32 rebuild (see ~/.claude/plans/goofy-dancing-locket.md).
// Level tilemap (hand-placed one-way StaticActor strips, reusing the exact
// row/column layout data already ported from JulianRijken/BubbleBobble in
// the old src/bubblebobble.cpp) + Bub as a KinematicActor with manual
// gravity/jump/walk, fed from thumbstick + encoder + RB exactly like the
// old game's control scheme, real 4bpp sprites, Zen-Chan AI enemies (also
// KinematicActor, same collision/gravity treatment as Bub), and a capture
// bubble mechanic (deliberately plain manual-physics data, not a
// SensorActor — see bubblebobble_pr32.cpp for why).
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include <physics/StaticActor.h>
#include <physics/KinematicActor.h>
#include "hardware.h"

namespace bubblebobble_pr32 {

// Pr32Input now lives in hardware.h (shared with the menu scene and
// Pac-Man, which need the identical shape) — this used to be its own
// struct declared right here, back when this was the only PixelRoot32
// game in the build.
using ::Pr32Input;

class BubActor : public pixelroot32::physics::KinematicActor {
public:
    BubActor(pixelroot32::math::Scalar x, pixelroot32::math::Scalar y, int w, int h);
    pixelroot32::core::Rect getHitBox() override { return {position, width, height}; }
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    // Set from BubbleBobbleScene::update() each frame — Bub itself has no
    // opinion on movement/AI, it just remembers enough to pick the right
    // sprite frame and advances its own walk-cycle timer in update().
    void setAnimState(bool facingRight, bool walking, bool airborne);
    bool getFacingRight() const { return facingRight; }
    void setVisible(bool v) { visible = v; }

private:
    bool facingRight = true;
    bool walking = false;
    bool airborne = false;
    bool visible = true;
    unsigned long animAccumulatorMs = 0;
    uint8_t walkFrame = 0;
};

// Also used for the level's solid side walls/floor (oneWay=false) — same
// StaticActor shape either way, just a different collision behavior and
// draw color to tell them apart visually.
class PlatformActor : public pixelroot32::physics::StaticActor {
public:
    PlatformActor(pixelroot32::math::Scalar x, pixelroot32::math::Scalar y, int w, int h,
                  bool oneWay = true, int levelTheme = 0);
    pixelroot32::core::Rect getHitBox() override { return {position, width, height}; }
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    bool oneWay = true;
    int levelTheme = 0;
};

// Zen-Chan enemy. AI decisions (wall-turn, face-player bias, climb) live in
// BubbleBobbleScene::updateEnemies() — same split as Bub/the scene — this
// class only owns its persistent per-enemy state, physics-actor mechanics,
// and drawing.
//
// Also doubles as Maita (the reference project's second enemy type,
// `Components/Character/Enemys/Maita.*`, GPL-3.0) via the `isMaita` flag,
// rather than a separate actor class: Maita shares ZenChan's exact walk/
// wall-turn/face-player physics in the reference source (its distinctive
// trait is a periodic boulder throw, handled in updateEnemies()/the
// Boulder struct below — not a different movement model), and it's drawn
// from the same Enemys.png sheet, just a different row. A second full
// KinematicActor subclass would duplicate all of that for zero behavioral
// difference where it actually matters.
class ZenChanActor : public pixelroot32::physics::KinematicActor {
public:
    ZenChanActor(pixelroot32::math::Scalar x, pixelroot32::math::Scalar y, int w, int h);
    pixelroot32::core::Rect getHitBox() override { return {position, width, height}; }
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    bool alive = false;
    bool trapped = false;
    bool angry = false;
    bool isMaita = false;
    int facing = 1;         // +1 right, -1 left
    float velocityY = 0.0f;
    float timeSinceLastTurn = 0.0f;
    float timeSinceLastJump = 0.0f;
    float jumpInterval = 1.0f;
    float timeSinceLastFacePlayer = 0.0f;
    float facePlayerInterval = 2.0f;
    // Maita-only: periodic boulder toss (BOULDER_THROW_INTERVAL in the
    // reference's Maita.h). Left at 0/unused for a plain Zen-Chan.
    float timeSinceLastThrow = 0.0f;
    float throwInterval = 4.0f;

private:
    unsigned long animAccumulatorMs = 0;
    uint8_t walkFrame = 0;
};

// Capture-bubble projectile. Plain manual-physics data (not an Actor/
// SensorActor) — see bubblebobble_pr32.cpp's CaptureBubble section for why
// this deliberately doesn't go through PixelRoot32's collision system.
struct CaptureBubble {
    bool active = false;
    bool floating = false;  // false = still in its initial travel arc
    float x = 0, y = 0, vx = 0, vy = 0;
    unsigned long spawnMs = 0;
    unsigned long travelEndMs = 0;
    int trappedEnemyIdx = -1;
};

// Food pickup (watermelon/fries), dropped when an enemy is destroyed.
// Plain manual-physics data, same rationale as CaptureBubble.
struct FoodItem {
    bool active = false;
    float x = 0, y = 0, vy = 0;
    bool onGround = false;
    unsigned long spawnMs = 0;
    int spriteIdx = 0;  // 0 = watermelon (100pts), 1 = fries (200pts)
};

// Maita's boulder toss. Plain manual-physics data, same rationale as
// CaptureBubble/FoodItem — a straight-line lobbed projectile toward
// wherever the player was standing when thrown needs no engine physics
// primitive, just position + velocity + a lifetime.
struct Boulder {
    bool active = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    unsigned long spawnMs = 0;
};

class BubbleBobbleScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    // Set (once) by update()'s pause-hold handling when R3 is held
    // well past the normal pause-toggle threshold (see PAUSE_HOLD_MS vs
    // kReturnToMenuHoldMs in the .cpp) — one continuous "hold longer to
    // back all the way out" gesture, reviving this console's old
    // "hold to force back to the menu" convention. main.cpp checks this
    // once per loop() and clears it via clearReturnToMenuRequest() after
    // acting on it.
    bool wantsReturnToMenu() const { return returnToMenuRequested; }
    void clearReturnToMenuRequest() { returnToMenuRequested = false; }

    // Debug-only level select (main.cpp's 'n'/'b' serial chars) — jumps
    // straight to a level, bypassing the normal bonus-round/transition
    // flow, so the 5 new levels (and their enemy mixes) can be reached
    // without having to actually clear every level in between. Remove
    // once the new levels are confirmed on hardware and no longer need
    // this shortcut.
    void debugNextLevel();
    void debugPrevLevel();

private:
    Pr32Input pendingInput;
    void buildLevel();
    void spawnEnemies();
    void updateEnemies(float dt);
    void updateBubbles(float dt);
    void spawnCaptureBubble();
    void popBubble(int idx, bool chainSource);
    void drawBubbles(pixelroot32::graphics::Renderer& renderer);
    void spawnFood(float cx, float cy);
    void updateFood(float dt);
    void drawFood(pixelroot32::graphics::Renderer& renderer);
    void spawnBoulder(float x, float y, float vx);
    void updateBoulders(float dt);
    void drawBoulders(pixelroot32::graphics::Renderer& renderer);
    void drawHUD(pixelroot32::graphics::Renderer& renderer);
    // Returns true if this call triggered a full level reset (ran out of
    // lives) — callers mid-loop over enemies/bubbles/food must stop using
    // any of those arrays/pointers immediately if so, since init() just
    // invalidated them.
    bool loseLife();
    // Called when updateEnemies() notices every enemy is gone — advances
    // to the next level (wrapping after the last real one) and rebuilds,
    // keeping score/lives. Same stale-pointer caution as loseLife().
    void advanceLevel();
    // Starts the bonus round: several fruits rain down from random spots
    // for the player to grab before advanceLevel() actually runs.
    void startBonusRound();
    // Builds a level's platforms as extra, throwaway entities offset
    // vertically by yOffset — used to preview the NEXT level one screen
    // below the current one during the descend-through-the-tower
    // transition, without touching the current level's own entities.
    // Cleaned up implicitly: the next real init() resets the whole arena.
    void buildLevelPreview(int levelIndex, float yOffset);

    BubActor* bub = nullptr;
    pixelroot32::math::Scalar velocityY{0};
    bool thumbJumpLatched = false;
    bool lastFacingRight = true;
    int stuckFrameCount = 0;
    unsigned long invulnerableUntilMs = 0;

    int score = 0;
    int lives = 3;
    bool paused = false;
    bool pauseLatched = false;
    unsigned long encSwHeldSinceMs = 0;
    bool returnToMenuRequested = false;
    bool returnToMenuLatched = false;
    // Debug level-select scrub (temporary — see debugNextLevel()'s
    // comment): raw encoder ticks accumulate here while paused, only
    // firing one level change per full detent's worth of ticks, not one
    // per raw tick (reported live as "the encoder is jumping way too far
    // with just one indent" — a single physical click generates several
    // interrupt-level ticks, not one).
    int debugLevelEncAccum = 0;
    int currentLevelIndex = 0;
    bool bonusRoundActive = false;
    unsigned long bonusRoundEndMs = 0;
    int enemiesKilledThisLevel = 0;

    // "Descend through the tower" level-transition scroll — see
    // buildLevelPreview()'s comment. Gameplay is frozen while this runs;
    // only the render offset animates and (harmlessly) entity idle/anim
    // timers via Scene::update().
    bool transitioning = false;
    unsigned long transitionStartMs = 0;
    static constexpr unsigned long kTransitionMs = 900;

    // Level 2's extended triple-banded box maze has many isolated 1-tile
    // rail segments (each side of each rail row is its own separate
    // horizontal run) — comfortably over 40 platform actors for that
    // level alone. Level 5's alternating-pillar design (each pillar is a
    // tall run of single-row 'S' segments, one actor per row since the
    // interior-solid scan works row-by-row, not merged vertically) pushed
    // this right up against the old 64 cap once floor/ceiling gap actors
    // were added on top (~64-65 actors just for that level). Bumped with
    // real headroom, not just enough for today's exact count.
    static constexpr int kMaxPlatforms = 96;
    PlatformActor* platforms[kMaxPlatforms] = {};
    int platformCount = 0;

    static constexpr int kMaxEnemies = 4;
    ZenChanActor* enemies[kMaxEnemies] = {};

    static constexpr int kMaxBubbles = 6;
    CaptureBubble bubbles[kMaxBubbles];

    static constexpr int kMaxFood = 8;
    FoodItem foods[kMaxFood];

    static constexpr int kMaxBoulders = 4;
    Boulder boulders[kMaxBoulders];
};

} // namespace bubblebobble_pr32
