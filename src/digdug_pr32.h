// Dig-Dug-style tunneling game on PixelRoot32 (7th game — see
// ~/.claude/plans/goofy-dancing-locket.md), built the same way as every
// other game here: a fresh Scene with the established init/update/draw +
// pendingInput/wantsReturnToMenu() interface.
//
// IP note: a reference implementation was consulted for GENERIC
// mechanics only — https://github.com/l-hackari/digdug (a C/Allegro
// remake with no license file that directly uses real Namco IP: title
// "Dig Dug", enemy names "Pooka"/"Fygar"). None of that repo's code or
// art is reused here. The menu title stays "DIG DUG" (user's explicit
// call — most of this project's other entries keep their real arcade
// titles too), but the characters get original names/designs: the round
// ground-crawler enemy is a "Grub", the fire-breathing type is a
// "Drake" — never "Pooka"/"Fygar" anywhere in this code. Only the
// generic mechanical shape carries over: grid-based tunnel digging, a
// pump attack that inflates a hit enemy over repeated pumps until it
// bursts (release early and it deflates back down), enemies that
// periodically phase through solid dirt, and rocks that fall once their
// support is dug out, crushing anything they land on — all standard
// conventions of this maze-digging genre, not unique creative
// expression tied to the real game. Sprite art (see
// make_digdug_pr32_sprites.py) is entirely freshly drawn for this
// project's palette-indexed Sprite4bpp pipeline.
//
// Same idiom as every other _pr32 game: update() only advances state,
// draw() redraws everything fresh from current state every frame
// (PixelRoot32's own renderer handles frame buffering).
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "digdug_pr32_audio.h"

namespace digdug_pr32 {

enum class GameState { Playing, Paused, GameOver, RoundClear };
enum class Dir { Down, Up, Left, Right };
enum class EnemyType { Grub, Drake };
enum class EnemyMode { Normal, Phase };
enum class RockState { Embedded, Shaking, Falling, Landed };

struct Enemy {
    bool active = false;
    EnemyType type = EnemyType::Grub;
    EnemyMode mode = EnemyMode::Normal;
    float x = 0, y = 0; // pixel position, center
    float targetX = 0, targetY = 0; // next tile-center waypoint being walked toward
    Dir facing = Dir::Down;
    unsigned long lastMoveMs = 0;
    unsigned long modeUntilMs = 0;
    int inflateStage = 0; // 0..kMaxInflateStage; kMaxInflateStage = burst
    unsigned long lastInflateMs = 0;
    unsigned long lastDeflateMs = 0;
};

struct Rock {
    bool active = false;
    int col = 0, row = 0;
    RockState state = RockState::Embedded;
    unsigned long stateSinceMs = 0;
};

struct Particle {
    bool active = false;
    float x = 0, y = 0;
    unsigned long bornMs = 0;
};

class DigDugScene : public pixelroot32::core::Scene {
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
    void debugSpawnEnemyNearPlayer();
    void debugForceNearestRockDrop();

private:
    Pr32Input pendingInput;

    static constexpr int HUD_H = 16;
    static constexpr int PLAY_TOP = HUD_H;
    static constexpr int kTileSize = 16;
    static constexpr int kCols = SCREEN_W / kTileSize;               // 15
    static constexpr int kRows = (SCREEN_H - HUD_H) / kTileSize;     // 19

    GameState state = GameState::Playing;
    GameState stateBeforePause = GameState::Playing;

    int score = 0;
    int highScore = 0;
    int lives = 3;
    int round = 1;
    unsigned long roundClearAtMs = 0;
    static constexpr unsigned long kRoundClearBannerMs = 2500;

    void loadHighScore();
    void saveHighScoreIfNeeded();
    void resetGame();
    void startRound();

    // --- Terrain grid: 0 = dug/tunnel, 1 = dirt ---
    uint8_t dirt[kRows][kCols];
    bool inBounds(int row, int col) const { return row >= 0 && row < kRows && col >= 0 && col < kCols; }
    bool isDirt(int row, int col) const;
    void digAt(int row, int col);
    int rowToPixelY(int row) const { return PLAY_TOP + row * kTileSize; }
    int colToPixelX(int col) const { return col * kTileSize; }
    int pixelToRow(float y) const { return (int)((y - PLAY_TOP) / kTileSize); }
    int pixelToCol(float x) const { return (int)(x / kTileSize); }
    void drawTerrain(pixelroot32::graphics::Renderer& renderer);

    // --- Rocks ---
    static constexpr int kMaxRocks = 6;
    Rock rocks[kMaxRocks];
    static constexpr unsigned long kRockShakeMs = 450;
    static constexpr unsigned long kRockFallStepMs = 90;
    bool rockAt(int row, int col, int exceptIdx = -1) const;
    void spawnRocks();
    void updateRocks(float dt);
    void drawRocks(pixelroot32::graphics::Renderer& renderer);
    void crushAt(int row, int col);

    // --- Player ---
    static constexpr float kPlayerSpeed = 62.0f;
    static constexpr int kPlayerSize = 14;
    float playerX = 0, playerY = 0; // top-left
    Dir playerFacing = Dir::Down;
    bool invulnerable = false;
    unsigned long invulnUntilMs = 0;
    bool playerBlinkOn = true;
    unsigned long lastBlinkMs = 0;
    void updatePlayerMovement(float dt);
    float playerCenterX() const { return playerX + kPlayerSize / 2.0f; }
    float playerCenterY() const { return playerY + kPlayerSize / 2.0f; }

    // --- Pump / inflate ---
    static constexpr int kMaxInflateStage = 3;
    static constexpr unsigned long kInflateStepMs = 350;
    static constexpr unsigned long kDeflateStepMs = 500;
    static constexpr int kPumpRangeTiles = 3;
    int pumpTargetIdx = -1;
    bool prevAHeld = false;
    void updatePump(float dt);
    void drawPumpBeam(pixelroot32::graphics::Renderer& renderer);
    int pumpBeamTiles = 0; // how many tiles the beam currently reaches, for drawing

    // --- Enemies ---
    static constexpr int kMaxEnemies = 5;
    Enemy enemies[kMaxEnemies];
    static constexpr float kEnemySpeed = 34.0f;
    static constexpr unsigned long kPhaseDurationMs = 2200;
    static constexpr unsigned long kPhaseCooldownMinMs = 3000;
    static constexpr unsigned long kPhaseCooldownMaxMs = 6000;
    // NOTE: Drake's fire-breath ranged attack from the plan is deferred —
    // v1 ships Grub/Drake as movement/contact-only (cosmetically distinct,
    // Drake worth more points), no projectile attack yet.
    void spawnEnemy(EnemyType type, int row, int col);
    void updateEnemies(float dt);
    void updateEnemyNormal(Enemy& e, float dt);
    void updateEnemyPhase(Enemy& e, float dt);
    void drawEnemies(pixelroot32::graphics::Renderer& renderer);
    void burstEnemy(Enemy& e);
    int enemiesRemaining() const;

    // --- Particles ---
    static constexpr int kMaxParticles = 4;
    static constexpr unsigned long kParticleLifeMs = 260;
    Particle particles[kMaxParticles];
    void spawnParticle(float x, float y);
    void updateParticles(float dt);
    void drawParticles(pixelroot32::graphics::Renderer& renderer);

    // --- Pause (R3) ---
    bool prevR3Held = false;
    unsigned long r3HeldSinceMs = 0;
    bool pauseLatched = false;
    bool returnToMenuLatched = false;
    static constexpr unsigned long kPauseHoldMs = 500;
    static constexpr unsigned long kReturnToMenuHoldMs = 2000;
    void updateR3Control();

    // --- Life / collision ---
    void killPlayer();

    // --- HUD / overlays ---
    void drawHUD(pixelroot32::graphics::Renderer& renderer);
    void drawPauseOverlay(pixelroot32::graphics::Renderer& renderer);
    void drawGameOverOverlay(pixelroot32::graphics::Renderer& renderer);
    void drawRoundClearOverlay(pixelroot32::graphics::Renderer& renderer);

    bool debugSpawnEnemyRequested = false;
    bool debugForceRockRequested = false;
    bool returnToMenuRequested = false;

    digdug_pr32_audio::NoteSequencer sfx;

    // Background music (see digdug_pr32_audio::kThemeMusic) — a separate
    // NoteSequencer from `sfx` since it loops independently of one-shot
    // SFX, but this hardware only has ONE buzzer voice, so the two can't
    // literally sound at once. Same duck-for-SFX arbitration as
    // blockstack_pr32.cpp's updateMusic(): a one-shot SFX always wins the
    // single voice for its own duration; music simply resumes from its
    // current note once the SFX ends.
    digdug_pr32_audio::NoteSequencer music;
    bool musicShouldPlay = false;
    void updateMusic();
};

} // namespace digdug_pr32
