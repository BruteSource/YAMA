// Galaga clone on PixelRoot32 (see ~/.claude/plans/goofy-dancing-locket.md).
// Ported from TheCoderRaman/CppGalaga (cloned to /tmp/galaga_ref this
// session, MIT-style hobby project) — a real, faithful clone whose
// source was read directly (formation math, entrance-swoop spline
// paths, diving AI, capture/rescue/dual-fighter mechanic, exact scoring
// table), not guessed at. See the plan file for the full research
// summary and phase breakdown; this header covers Phase 1 (formation +
// entrance + player + collision + scoring + lives/stage-repeat, no
// diving yet) with scaffolding left in place for Phases 2-3.
//
// Coordinate system: kept in the REFERENCE's own 224x288 space
// end-to-end (matches our screen almost exactly — see PLAYFIELD_LEFT/TOP
// below) so every waypoint/formation number from the reference ports
// with NO scaling math, only a fixed +8,+32 offset applied once at
// draw() time. Movement is entirely hand-managed (no PixelRoot32
// physics/Actors), same "manual data, not an engine entity" pattern as
// Bubble Bobble/Pac-Man.
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "galaga_pr32_audio.h"

namespace galaga_pr32 {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return { a.x + b.x, a.y + b.y }; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return { a.x - b.x, a.y - b.y }; }
inline Vec2 operator*(float s, Vec2 a) { return { a.x * s, a.y * s }; }

// Reference's `Path` class (Path.hpp/cpp), ported closely: a Catmull-Rom
// spline over hand-placed waypoints, walked at CONSTANT ARC-LENGTH speed
// (advance() steps t in small increments until the Euclidean distance
// traveled matches the requested speed) rather than constant-t-per-tick
// — this is what gives the entrance swoops their authentic even pacing.
// Fixed-capacity array instead of the reference's std::vector — this
// project avoids dynamic allocation, matching every other scene here.
constexpr int kMaxPathPoints = 20;
struct CatmullPath {
    Vec2 points[kMaxPathPoints];
    int count = 0;

    void clear() { count = 0; }
    void addPoint(Vec2 p) { if (count < kMaxPathPoints) points[count++] = p; }
    // The final waypoint is re-targeted every tick during entrance/return
    // moves (see moveToFormation()/returnToFormation() in the .cpp) so
    // the spline's tail keeps chasing the live (swaying) formation slot
    // instead of a fixed point — the key subtlety that makes the
    // entrance blend smoothly into the moving formation rather than
    // snapping. This accessor is how callers overwrite it each frame.
    Vec2& lastPoint() { return points[count - 1]; }

    Vec2 getControlPoint(int i) const {
        if (i < 0) i = 0;
        if (i >= count) i = count - 1;
        return points[i];
    }

    Vec2 getPoint(float t) const {
        int i = static_cast<int>(t);
        Vec2 a = getControlPoint(i - 1);
        Vec2 b = getControlPoint(i + 0);
        Vec2 c = getControlPoint(i + 1);
        Vec2 d = getControlPoint(i + 2);
        float tf = t - i;
        float tt = tf * tf;
        float ttt = tf * tt;
        return 0.5f * ((-ttt + 2 * tt - tf) * a + (3 * ttt - 5 * tt + 2) * b +
                        (-3 * ttt + 4 * tt + tf) * c + (ttt - tt) * d);
    }

    bool isFinished(float t) const { return t >= static_cast<float>(count - 1); }

    // Walks t forward in fixed micro-steps until the arc-length distance
    // traveled reaches `dist`; returns the new position. Matches the
    // reference's own 0.002f step size.
    Vec2 advance(float& t, float dist) {
        Vec2 pos = getPoint(t);
        float traveled = 0.0f;
        constexpr float kStep = 0.02f;  // coarser than the reference's 0.002 —
                                         // cheap enough for an ESP32 and not
                                         // visually different at this screen size
        while (traveled < dist && !isFinished(t)) {
            float nt = t + kStep;
            Vec2 npos = getPoint(nt);
            float dx = npos.x - pos.x, dy = npos.y - pos.y;
            traveled += sqrtf(dx * dx + dy * dy);
            t = nt;
            pos = npos;
        }
        if (isFinished(t)) t = static_cast<float>(count - 1);
        return getPoint(t);
    }
};

// Vec2 helper for operator* with a plain multiply on the left (needed by
// the Catmull-Rom blend expression above, since the coefficients are
// scalars multiplying Vec2 on the right in that formula).
inline Vec2 operator*(Vec2 a, float s) { return { a.x * s, a.y * s }; }

enum class EnemyPersonality { Boss, Guard, Minion };
enum class EnemyState { Dead, Entering, InFormation, Diving, Returning, Exploding, Hovering };

struct Enemy {
    bool used = false;  // slot occupied at all (including mid-explosion)
    EnemyState state = EnemyState::Dead;
    EnemyPersonality personality = EnemyPersonality::Minion;
    int formationCol = 0;
    int formationRow = 0;
    int life = 1;

    CatmullPath path;
    float pathT = 0.0f;
    Vec2 position;

    // Rotation-sheet frame selection (see updateEnemyFrame() in the
    // .cpp) — ported from the reference's path-tangent-driven facing.
    float frame = 0.0f;
    float randomAngleSeed = 0.0f;
    float angleTransitionFactor = 1.0f;  // 1 = idle wobble, 0 = path-tangent facing

    // Formation sway/breathing blend (see formationScreenPosition() in
    // the .cpp) — 0 = horizontal marching sway (entrance/idle), 1 =
    // scaled "breathing" pulse (once diving phase starts).
    float formationMoveFactor = 0.0f;

    float explosionTimer = 0.0f;  // counts up while state == Exploding

    // Diving (Phase 2)
    int diveBulletsRemaining = 0;   // random 0-3 budget rolled at dive start
    float diveFireFirstDelay = 0.0f; // gameElapsedTime threshold before first shot
    float diveFireLastTime = -1.0f;

    // Escort-drag (boss dives can pull 1-3 nearby guards along): when
    // >= 0, this enemy's position/frame are entirely slaved to
    // enemies[followBossIndex] (a fixed offset captured at join time)
    // rather than following its own path — simplified vs. the
    // reference's own `follow()` (which still runs independent physics
    // with an offset), but visually equivalent for a "flying escort."
    int followBossIndex = -1;
    Vec2 followOffset;

    // Boss-only: overrides the normal diving score (100/160/400) with
    // the escort-count bonus table (400/800/1600/2000) when >= 0.
    int diveScoreOverride = -1;

    // Phase 3: true while this boss is flying to its capture-attempt
    // hover point (a Diving-state path under the hood) — distinguishes
    // "arrived, now hover for the beam" from a normal dive's
    // dive-again/return-to-formation finish logic.
    bool movingToCapture = false;

    // Reference's stage>1 escalation (`GameController::spawnNextEnemies`'s
    // divingProbability): 25% of enemies spawned from stage 2 onward
    // dive straight out of their entrance swoop instead of joining
    // formation first. Rolled once at spawn time; diveMidEntranceFraction
    // (0.55-0.75, matching the reference's own random range) is how far
    // along the entrance path this happens.
    bool diveMidEntrance = false;
    float diveMidEntranceFraction = 0.65f;
};

// Reference's `ScorePoint`: a floating score number shown at the kill
// location for 1.5s, color-tiered by point value. The reference's own
// tier logic is a real bug (cascading `if`s with no `else`, so almost
// every kill ends up yellow) — ported with the tiers fixed, not copied,
// per the plan's "fix reference bugs, don't port them" rule.
struct ScorePopup {
    bool active = false;
    Vec2 position;
    int points = 0;
    float timer = 0.0f;
};

// Reference's `StarsBackground`: a multi-speed scrolling, twinkling
// starfield behind the action — one of Galaga's most recognizable visual
// signatures, so worth the modest per-frame drawPixel cost.
struct Star {
    Vec2 position;
    pixelroot32::graphics::Color color = pixelroot32::graphics::Color::White;
    float ySpeed = 1.0f;
    bool blink = false;
    float blinkTime = 0.0f;
};

struct Bullet {
    bool active = false;
    Vec2 position;
    float vy = 0.0f;  // px/sec, sign gives direction
};

class GalagaScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    // Same long-hold-R3-past-pause convention as every other scene.
    bool wantsReturnToMenu() const { return returnToMenuRequested; }
    void clearReturnToMenuRequest() { returnToMenuRequested = false; }

    // Debug-only: force-triggers a capture attempt on the first
    // in-formation boss found, bypassing the real 35%-chance/10s-
    // cooldown gate — for testing the capture/rescue/dual-fighter
    // sequence without waiting on the dice. Same "temporary same-
    // session verification aid" spirit as Pac-Man's own debug warps.
    void debugForceCaptureAttempt();
    void debugForceRescue();

private:
    Pr32Input pendingInput;

    void spawnNextGroup();
    void updateEnemy(Enemy& e, float dt);
    Vec2 formationScreenPosition(int col, int row, float moveFactor) const;
    void updateEnemyFrame(Enemy& e, Vec2 tangent);
    void fireFighterBullet();
    void fireEnemyBullet(const Enemy& e);
    void updateBullets(float dt);
    void checkCollisions();
    void destroyEnemy(Enemy& e, bool killedWhileDiving);
    void resetFighter();
    void drawHUD(pixelroot32::graphics::Renderer& renderer);

    void initStars();
    void updateStars(float dt);
    void drawStars(pixelroot32::graphics::Renderer& renderer);

    void showScorePopup(int points, Vec2 position);
    void updateScorePopups(float dt);
    void drawScorePopups(pixelroot32::graphics::Renderer& renderer);

    // Phase 2: diving.
    void triggerRandomDive();
    void startDiving(Enemy& e);
    void onDivingFinished(Enemy& e);
    void returnToFormation(Enemy& e);
    void hitFighter();

    // Phase 3: capture beam -> rescue -> dual fighter.
    void startCaptureAttempt(Enemy& boss, int bossIndex);
    void startCaptureAttempt(Enemy& boss, int bossIndex, float hoverX);
    void updateCapture(float dt);
    void updateRescue(float dt);

    float gameElapsedTime = 0.0f;

    static constexpr int kMaxEnemies = 40;
    Enemy enemies[kMaxEnemies];
    int currentGroup = 0;      // 0..4, which of the 5 groups of 8 has spawned
    static constexpr int kGroupSpawnIntervalS = 3;
    float groupSpawnTimer = 0.0f;
    bool allGroupsSpawned = false;

    // Diving phase: starts once the full formation has assembled; a
    // random in-formation enemy is picked to dive every
    // random(0.3,2.0)s, matching the reference's own cadence.
    bool divingPhaseStarted = false;
    float diveTriggerTimer = 0.0f;
    float nextDiveIntervalS = 1.0f;

    static constexpr int kMaxFighterBullets = 4;
    Bullet fighterBullets[kMaxFighterBullets];
    float fireLastTime = -1.0f;

    static constexpr int kMaxEnemyBullets = 12;
    Bullet enemyBullets[kMaxEnemyBullets];

    // Player
    Vec2 fighterPos{ 112.0f, 256.0f };
    bool fighterAlive = false;
    CatmullPath fighterEntryPath;
    float fighterEntryT = 0.0f;
    bool fighterEntering = false;
    float fighterFrame = 6.0f;  // idle "facing up" frame on the 24-frame sheet
    unsigned long invulnerableUntilMs = 0;

    int score = 0;
    int hiscore = 0;  // reference's HudInfo::hiscore — persists across
                       // restarts within this power-on session, only
                       // reset by an actual device reset (not init()).
    int lives = 3;
    int stage = 1;
    int nextExtraLifeScore = 30000;
    bool gameOver = false;

    // Reference's showCurrentLevel()/tryNextLife() "STAGE N"/"READY!"
    // banners — abbreviated (skips the long attract-mode "START" banner
    // + 7s music sting, since our menu already serves that role).
    // 0 = no banner (normal play), 1 = "STAGE N" showing, 2 = "READY!"
    // showing. Formation spawning/diving is held off until this clears.
    int introPhase = 1;
    float introTimer = 0.0f;
    static constexpr float kStageBannerS = 2.0f;
    static constexpr float kReadyBannerS = 1.5f;

    static constexpr int kMaxScorePopups = 6;
    ScorePopup scorePopups[kMaxScorePopups];

    static constexpr int kStarCount = 80;
    Star stars[kStarCount];
    float starVelocityScale = 1.0f;
    float starTargetVelocityScale = 1.0f;

    bool paused = false;
    bool pauseLatched = false;
    unsigned long encSwHeldSinceMs = 0;
    bool returnToMenuRequested = false;
    bool returnToMenuLatched = false;

    galaga_pr32_audio::NoteSequencer sfx;

    // Phase 3: capture beam. `captureState` 0=idle (no attempt active).
    // 1=beam extending, 2=beam holding at full height (timeout window),
    // 3=pulling the fighter up into the beam, 4=captured (label shown,
    // boss+captive flying back to formation together). Only one attempt
    // can be active at a time (captureBossIndex >= 0 while so).
    int captureState = 0;
    int captureBossIndex = -1;
    float captureStateTimer = 0.0f;
    float beamHeight = 0.0f;
    CatmullPath capturePullPath;
    float capturePullT = 0.0f;
    float fighterCaptureCooldownUntil = -1000.0f;  // gameElapsedTime units

    // A captive fighter, once docked, is tracked separately from the
    // enemy array (it isn't an Enemy) — its position/frame are slaved
    // to the capturing boss every frame (see updateCapture()/draw()).
    // `capturedByBossIndex` is the PERSISTENT docking relationship
    // (stays set from the moment of capture until rescue or the boss's
    // death, independent of `captureState`/`captureBossIndex` which
    // only track the transient capture SEQUENCE itself).
    bool captiveExists = false;
    bool captiveDestroyed = false;
    int capturedByBossIndex = -1;
    Vec2 captivePos;
    float captiveFrame = 0.0f;

    // Phase 3: rescue -> dual fighter. `rescueState` 0=idle, 1=rescue
    // animation playing, 2=both ships flying to the dock point.
    int rescueState = 0;
    float rescueStateTimer = 0.0f;
    CatmullPath dualDockPathFighter;
    float dualDockTFighter = 0.0f;
    CatmullPath dualDockPathCaptive;
    float dualDockTCaptive = 0.0f;
    bool dualMode = false;

    // True while the player has no direct control — during the capture
    // pull-up and the rescue/dual-dock animation.
    bool fighterControlFrozen = false;
};

} // namespace galaga_pr32
