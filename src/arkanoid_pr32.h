// Arkanoid clone on PixelRoot32 (see ~/.claude/plans/goofy-dancing-locket.md).
// Ported from wkeeling/arkanoid (Python/pygame remake, cloned to
// /tmp/arkanoid_ref and read in full — ball physics, paddle bounce-angle
// table, brick colors/scores, all 7 powerups, enemy AI/door-spawn, and
// each round's real color theme/physics tuning all confirmed from
// source) and dawid-wolinski/arkanoid-game (cloned to /tmp/ark_ref2 —
// real background/wall/paddle art, and the boss encounter's HP/hit-
// flash/defeat mechanics). Its own 600x800 screen scales to our 240x320
// by an exact 0.4x factor; rather than replicate that arithmetic pixel-
// for-pixel, the reference's own SPEEDS and RATIOS are ported while
// actual pixel sizes use clean round numbers sized for this screen.
//
// Same "hand-rolled physics in a plain Scene, no Actors/CollisionSystem"
// pattern as Bubble Bobble/Pac-Man/Galaga. All 4 phases from the plan are
// now in: startup animation, paddle/ball/bricks/lives/HUD, all 7
// powerups (Warp excluded — the reference itself never finishes it
// either), enemies + door-spawn + 5-round progression + boss.
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "arkanoid_pr32_audio.h"

namespace arkanoid_pr32 {

enum class BrickColor { Silver, Gold, Red, Yellow, Blue, Green, Cyan, Orange, Pink, White };

// Warp deliberately excluded — the reference's own WarpPowerUp is an
// unfinished stub even in its source, not worth porting a half-feature.
enum class PowerupType { None, Catch, Expand, Laser, SlowBall, ExtraLife, Duplicate };

enum class EnemyType { Cone, Pyramid, Molecule, Cube };

struct Brick {
    bool alive = false;
    BrickColor color = BrickColor::Red;
    int hitsLeft = 1;
    int value = 0;
    int x = 0, y = 0;  // top-left, pixels
    PowerupType powerup = PowerupType::None;
    // Brief flash when struck but not destroyed (silver's first hit) —
    // matches the reference's Brick.animate() cue, simplified to a
    // plain white flash rather than a real multi-frame animation.
    float hitFlashTimer = 0.0f;
};

struct Ball {
    bool active = false;
    bool attached = false;  // sitting on the paddle (launch or Catch-release pending)
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
};

struct FallingPowerup {
    bool active = false;
    PowerupType type = PowerupType::None;
    float x = 0.0f, y = 0.0f;
};

struct LaserBolt {
    bool active = false;
    float x = 0.0f, y = 0.0f;
};

struct Enemy {
    bool active = false;
    EnemyType type = EnemyType::Cone;
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float phaseTimer = 0.0f;
    float dirChangeTimer = 0.0f;
    int phase = 0;  // 0 = moving down after spawn, 1 = erratic drift
};

class ArkanoidScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    bool wantsReturnToMenu() const { return returnToMenuRequested; }
    void clearReturnToMenuRequest() { returnToMenuRequested = false; }

    // Debug-only, same "temporary same-session verification aid, kept
    // around" convention as every other game's debug hooks here.
    void debugSkipRound();
    void debugNearKillBoss();
    // Cycles through each powerup type in turn and force-drops one
    // immediately above the paddle, for testing all 6 without relying
    // on the ~random brick-drop chance during a real playthrough.
    void debugForcePowerup();

private:
    Pr32Input pendingInput;

    // --- Phase 0: startup animation ---
    int titlePhase = 0;
    float titleTimer = 0.0f;
    void drawTitleScreen(pixelroot32::graphics::Renderer& renderer);
    void drawStoryScreen(pixelroot32::graphics::Renderer& renderer);
    void drawRoundBanner(pixelroot32::graphics::Renderer& renderer);

    static constexpr int kStarCount = 40;
    struct Star { int x, y; pixelroot32::graphics::Color color; };
    Star stars[kStarCount];
    void initStars();

    // --- Phase 1: playfield ---
    void resetRound();
    void createRoundBricks();  // reads `round` (1-5), each with its own row-color theme
    void applyRoundTuning();   // per-round ball/paddle speed + normalise-rate adjustments
    void updatePaddle(float dt);
    void updateBalls(float dt);
    void updateOneBall(Ball& b, float dt);
    void checkBrickCollisions(Ball& b);
    void checkWallCollisions(Ball& b);
    void loseBall();
    void drawHUD(pixelroot32::graphics::Renderer& renderer);
    void drawPlayfield(pixelroot32::graphics::Renderer& renderer);
    void drawPaddleSprite(pixelroot32::graphics::Renderer& renderer);
    void drawBallSprite(const Ball& b, pixelroot32::graphics::Renderer& renderer);
    pixelroot32::graphics::Color colorFor(BrickColor c) const;

    // --- Phase 2: powerups ---
    void spawnFallingPowerup(float x, float y, PowerupType type);
    void updateFallingPowerups(float dt);
    void drawFallingPowerups(pixelroot32::graphics::Renderer& renderer);
    void activatePowerup(PowerupType type);
    void deactivateCurrentPowerup();
    void updateLasers(float dt);
    void drawLasers(pixelroot32::graphics::Renderer& renderer);
    PowerupType currentPowerup = PowerupType::None;
    static constexpr int kMaxFallingPowerups = 3;
    FallingPowerup fallingPowerups[kMaxFallingPowerups];
    static constexpr int kMaxLasers = 3;
    LaserBolt lasers[kMaxLasers];
    float laserCooldownTimer = 0.0f;

    // --- Phase 3: enemies + door-spawn ---
    static constexpr int kMaxEnemies = 3;
    Enemy enemies[kMaxEnemies];
    EnemyType roundEnemyType = EnemyType::Cone;
    bool enemiesReleased = false;
    void maybeReleaseEnemies();
    void updateEnemies(float dt);
    void drawEnemies(pixelroot32::graphics::Renderer& renderer);
    void checkEnemyCollisions(Ball& b);
    pixelroot32::graphics::Color colorForEnemy(EnemyType t) const;
    // Two fixed door x-positions in the top wall enemies spawn from —
    // simplified from the reference's own animated open/close door
    // sprite (edge.py) to a plain spawn-at-position, same "faithful in
    // spirit, not every animation" scoping as the rest of this port.
    int doorX(int i) const { return i == 0 ? kFieldLeft + 30 : kFieldRight - 30; }

    // --- Boss encounter (after Round 5 clears) ---
    void startBossEncounter();
    void updateBoss(float dt);
    void drawBoss(pixelroot32::graphics::Renderer& renderer);
    void checkBossCollision(Ball& b);
    void updateBossProjectiles(float dt);
    void drawBossProjectiles(pixelroot32::graphics::Renderer& renderer);
    void checkBossProjectileVsPaddle();
    bool bossActive = false;
    int bossHp = 8;
    static constexpr int kBossMaxHp = 8;
    float bossX = 0.0f, bossY = 0.0f;
    float bossDriftT = 0.0f;
    int bossAnimFrame = 0;
    float bossAnimTimer = 0.0f;
    float bossHitFlashTimer = 0.0f;
    static constexpr float kBossHitFlashDurationS = 0.15f;
    static constexpr int kBossW = 48;
    static constexpr int kBossH = 40;
    struct BossProjectile { bool active = false; float x = 0.0f, y = 0.0f; };
    static constexpr int kMaxBossProjectiles = 2;
    BossProjectile bossProjectiles[kMaxBossProjectiles];
    float bossFireCooldownTimer = 3.0f;

    static constexpr int kBrickCols = 13;
    static constexpr int kBrickRows = 5;
    static constexpr int kBrickW = 17;
    static constexpr int kBrickH = 8;
    static constexpr int kFieldLeft = 9;
    static constexpr int kFieldTop = 40;
    static constexpr int kFieldRight = kFieldLeft + kBrickCols * kBrickW;  // 230
    static constexpr int kWallThickness = 9;
    static constexpr int kHudTop = 0;
    static constexpr int kTopWallY = 32;
    static constexpr int kBallLostY = 312;

    Brick bricks[kBrickRows][kBrickCols];
    int bricksRemaining = 0;

    float paddleX = 112.0f;
    static constexpr float kPaddleY = 292.0f;
    // Sized relative to a brick tile per explicit user spec: ~2.2x a
    // brick's width, ~1.2x its height (kBrickW=17, kBrickH=8). `paddleW`
    // is now variable (the Expand powerup widens it; back to base when
    // any other powerup deactivates it).
    static constexpr float kPaddleWBase = 37.0f;
    static constexpr float kPaddleWExpanded = 55.0f;
    float paddleW = kPaddleWBase;
    static constexpr float kPaddleH = 10.0f;
    static constexpr float kPaddleSpeed = 220.0f;  // px/s baseline, see applyRoundTuning()
    float currentPaddleSpeed = kPaddleSpeed;
    bool paddleCatchingBall = false;  // true while the Catch powerup is active

    static constexpr int kMaxBalls = 3;
    Ball balls[kMaxBalls];
    int liveBallCount() const;

    static constexpr float kBallRadius = 3.0f;
    static constexpr float kBallBaseSpeed = 190.0f;
    static constexpr float kBallTopSpeed = 360.0f;
    static constexpr float kBallBrickSpeedAdd = 12.0f;
    static constexpr float kBallWallSpeedAdd = 5.0f;
    static constexpr float kBallNormaliseRate = 35.0f;
    float currentBallBaseSpeed = kBallBaseSpeed;
    float currentBallNormaliseRate = kBallNormaliseRate;

    int score = 0;
    int hiscore = 0;
    int lives = 3;
    int round = 1;
    bool gameOver = false;

    bool paused = false;
    bool pauseLatched = false;
    unsigned long encSwHeldSinceMs = 0;
    bool returnToMenuRequested = false;
    bool returnToMenuLatched = false;

    arkanoid_pr32_audio::NoteSequencer sfx;
};

} // namespace arkanoid_pr32
