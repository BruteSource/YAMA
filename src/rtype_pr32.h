// R-Type-style vertical shmup on PixelRoot32 (see
// ~/.claude/plans/goofy-dancing-locket.md). Rebuilt from the dormant
// pre-Scene-rebuild src/rtype.cpp/rtype.h (excluded from the build via
// platformio.ini's src_filter) onto this project's Scene architecture,
// same treatment as Bubble Bobble/Pac-Man/Galaga/Arkanoid/Block Stack.
//
// IP note: the dormant version named real Irem R-Type property directly
// — boss "Guard Ray" (a real R-Type III boss), stage "Catapult
// Dimension", ship modeled on "the real R-9", pod mechanic called
// "Force", enemies described as "Bydo-style" — and documented the boss
// fight as a close homage to that specific real fight. None of that
// naming survives here: the boss is an original design (own sprite set,
// rtype_pr32_boss.h) under an original name, the pod is called a
// "Drone", and no stage/faction name is used at all. Only the generic
// mechanical shape carries over — charge-shot weapon, a small auto-
// firing support drone, drifting/turret enemy roster, and a 3-phase
// boss fight (patrol/lob -> settle-and-home -> split at low HP) — all
// standard shmup genre convention, not unique creative expression.
// Sprite art (ship/drifter/turret/debris/bullets/capsules) reuses the
// dormant version's own hand-drawn silhouettes/colors (confirmed
// original work, not traced from real R-Type sprites) re-authored flat-
// shaded for PixelRoot32's palette-indexed Sprite4bpp instead of the
// old raw-RGB565-bitmap approach — see make_rtype_pr32_sprites.py.
//
// Unlike the old version's manual erase-then-redraw-per-entity drawing
// (direct tft-> calls, no double buffering), this Scene's update()
// only advances state — no drawing — and draw() redraws everything
// fresh from current state every frame, same idiom as every other game
// here (PixelRoot32's own renderer handles the frame buffering).
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "rtype_pr32_audio.h"

namespace rtype_pr32 {

enum class GameState { Playing, BossIntro, BossFight, Paused, GameOver, StageClear };

// weaponType: 0=normal (single shot), 1=spread (3-way), 2=laser (faster single)
struct PBullet { bool active = false; float x = 0, y = 0, vy = 0; int kind = 0; int pierce = 0; };
struct EBullet { bool active = false; float x = 0, y = 0, vx = 0, vy = 0; bool homing = false; };
struct Particle { bool active = false; float x = 0, y = 0, vx = 0, vy = 0; unsigned long bornMs = 0; pixelroot32::graphics::Color color = pixelroot32::graphics::Color::White; };
struct Debris { bool active = false; float x = 0, y = 0; int w = 0, h = 0, hp = 0; int turretIdx = -1; int variant = 0; };
enum class EnemyType { Drifter, Turret };
struct Enemy { bool active = false; EnemyType type = EnemyType::Drifter; float x = 0, y = 0, baseX = 0, phase = 0; int hp = 0; unsigned long lastFireMs = 0; int debrisIdx = -1; };
struct Capsule { bool active = false; float x = 0, y = 0; int kind = 0; }; // 0=score bonus, 1=weapon cycle
struct Star { float x = 0, y = 0, speed = 0; pixelroot32::graphics::Color color = pixelroot32::graphics::Color::White; };
struct BossUnit { bool active = false; float x = 0, y = 0, vy = 0, baseX = 0, phase = 0; int hp = 0, maxHp = 0; unsigned long lastFireMs = 0; float radius = 0; };

class RTypeScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    bool wantsReturnToMenu() const { return returnToMenuRequested; }
    void clearReturnToMenuRequest() { returnToMenuRequested = false; }

    // Debug-only, same "kept around" convention as every other game's
    // debug hooks in this project.
    void debugSkipToBoss();

private:
    Pr32Input pendingInput;

    static constexpr int HUD_H = 16;
    static constexpr int PLAY_TOP = HUD_H;

    GameState state = GameState::Playing;
    GameState stateBeforePause = GameState::Playing;

    int score = 0;
    int highScore = 0;
    int lives = 3;
    int weaponType = 0;
    float worldDistance = 0.0f;
    static constexpr float kStageLength = 4200.0f;
    unsigned long stageClearAtMs = 0;
    static constexpr unsigned long kStageClearBannerMs = 3500;
    unsigned long bossIntroUntilMs = 0;
    static constexpr unsigned long kBossIntroMs = 2200;

    void loadHighScore();
    void saveHighScoreIfNeeded();

    // --- Ship ---
    static constexpr int kShipW = 18;
    static constexpr int kShipH = 32;
    static constexpr int kShipXMin = 4;
    static constexpr int kShipXMax = SCREEN_W - kShipW - 4;
    static constexpr int kDroneDockOffsetY = -10;
    float shipX = 0, shipY = 0;
    unsigned long lastEncTickMs = 0;
    bool invulnerable = false;
    unsigned long invulnUntilMs = 0;
    bool shipBlinkOn = true;
    unsigned long lastBlinkMs = 0;
    void updateShipMovement(float dt);

    // --- Player bullets ---
    static constexpr int kMaxPBullets = 16;
    PBullet pbullets[kMaxPBullets];
    void spawnPBullet(float x, float y, float vy, int kind, int pierce);
    void updatePBullets(float dt);
    void drawPBullets(pixelroot32::graphics::Renderer& renderer);

    // --- Enemy bullets ---
    static constexpr int kMaxEBullets = 16;
    EBullet ebullets[kMaxEBullets];
    void spawnEBullet(float x, float y, float vx, float vy, bool homing = false);
    void updateEBullets(float dt);
    void drawEBullets(pixelroot32::graphics::Renderer& renderer);

    // --- Particles ---
    static constexpr int kMaxParticles = 30;
    Particle particles[kMaxParticles];
    static constexpr unsigned long kParticleLifeMs = 300;
    void spawnParticles(float x, float y, pixelroot32::graphics::Color color, int count);
    void updateParticles(float dt);
    void drawParticles(pixelroot32::graphics::Renderer& renderer);

    // --- Debris + enemies ---
    static constexpr int kMaxDebris = 6;
    Debris debris[kMaxDebris];
    static constexpr int kDebrisVariantCount = 3;
    static constexpr int kMaxEnemies = 10;
    Enemy enemies[kMaxEnemies];
    unsigned long lastEnemySpawnMs = 0, lastDebrisSpawnMs = 0;
    static constexpr float kScrollSpeed = 55.0f;
    static constexpr float kEnemyExtraSpeed = 12.0f;
    void spawnDrifter();
    void spawnDebrisWithMaybeTurret();
    void updateDebrisAndTurrets(float dt);
    void updateDrifters(float dt);
    void drawDebris(pixelroot32::graphics::Renderer& renderer);
    void drawEnemies(pixelroot32::graphics::Renderer& renderer);

    // --- Capsules ---
    static constexpr int kMaxCapsules = 4;
    Capsule capsules[kMaxCapsules];
    void maybeDropCapsule(float x, float y);
    void updateCapsules(float dt);
    void drawCapsules(pixelroot32::graphics::Renderer& renderer);

    // --- Stars (parallax backdrop) ---
    static constexpr int kMaxStars = 36;
    Star stars[kMaxStars];
    void initStars();
    void updateStars(float dt);
    void drawStars(pixelroot32::graphics::Renderer& renderer);

    // --- Fire / charge (A) ---
    bool prevAHeld = false;
    unsigned long aHeldSinceMs = 0;
    static constexpr unsigned long kChargeFullMs = 900;
    unsigned long lastDroneFireMs = 0;
    static constexpr unsigned long kDroneFireIntervalMs = 350;
    void fireWeapon(bool charged);
    void updateFireControl();

    // --- Weapon cycle / pause (R3) ---
    bool prevR3Held = false;
    unsigned long r3HeldSinceMs = 0;
    bool pauseLatched = false;
    static constexpr unsigned long kPauseHoldMs = 500;
    void updateR3Control();

    // --- Collision / life ---
    void killShip();
    void checkCollisions();

    // --- Boss ---
    static constexpr int kMaxBossUnits = 3;
    BossUnit boss[kMaxBossUnits];
    bool bossHasSplit = false;
    bool bossSettled = false;
    unsigned long bossSettleToggleMs = 0;
    static constexpr int kBossMainHp = 36;
    static constexpr int kBossSplitHp = 10;
    int bossHpRemaining() const;
    int bossHpMax() const;
    void spawnBoss();
    void updateBossUnit(BossUnit& b, float dt, float speedMul, unsigned long fireIntervalMs, bool canSettle);
    void splitBoss();
    void updateBossFight(float dt);
    void drawBoss(pixelroot32::graphics::Renderer& renderer);
    void drawBossHpBar(pixelroot32::graphics::Renderer& renderer);
    int bossAnimFrame = 0;
    float bossAnimTimer = 0.0f;

    // --- HUD / overlays / reset ---
    void resetGame();
    void drawHUD(pixelroot32::graphics::Renderer& renderer);
    void drawPauseOverlay(pixelroot32::graphics::Renderer& renderer);
    void drawGameOverOverlay(pixelroot32::graphics::Renderer& renderer);
    void drawStageClearOverlay(pixelroot32::graphics::Renderer& renderer);
    void drawBossIntroBanner(pixelroot32::graphics::Renderer& renderer);

    bool debugSkipToBossRequested = false;
    bool returnToMenuRequested = false;

    rtype_pr32_audio::NoteSequencer sfx;

    // Background music (see rtype_pr32_audio::kThemeMusic) — a separate
    // NoteSequencer from `sfx` since it loops independently of one-shot
    // SFX, but this hardware only has ONE buzzer voice, so the two can't
    // literally sound at once. Same duck-for-SFX arbitration as
    // blockstack_pr32.cpp's updateMusic(): a one-shot SFX always wins the
    // single voice for its own duration; music simply resumes from its
    // current note once the SFX ends.
    rtype_pr32_audio::NoteSequencer music;
    bool musicShouldPlay = false;
    void updateMusic();
};

} // namespace rtype_pr32
