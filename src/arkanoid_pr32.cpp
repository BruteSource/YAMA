#include "arkanoid_pr32.h"
#include "arkanoid_pr32_palette.h"
#include "arkanoid_pr32_bg.h"
#include "arkanoid_pr32_paddle.h"
#include "arkanoid_pr32_ship.h"
#include "arkanoid_pr32_boss.h"
#include "menu_pr32_font.h"
#include <graphics/FontManager.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace pr32 = pixelroot32;
using pixelroot32::graphics::Color;
using pixelroot32::graphics::Renderer;
namespace fm = pixelroot32::graphics;

namespace arkanoid_pr32 {

// Retro font shared with every other scene's HUD (see feedback memory on
// why this beats the engine's default font for an arcade look).
static const pr32::graphics::Font* const kHudFont = &menu_pr32_font::FONT_SMALL;
static const pr32::graphics::Font* const kLogoFont = &menu_pr32_font::FONT_LARGE;

void ArkanoidScene::initStars() {
    for (auto& s : stars) {
        s.x = rand() % SCREEN_W;
        s.y = rand() % SCREEN_H;
        s.color = (rand() % 3 == 0) ? Color::White : Color::Gray;
    }
}

Color ArkanoidScene::colorFor(BrickColor c) const {
    switch (c) {
        case BrickColor::Silver: return Color::Gray;
        case BrickColor::Gold:   return Color::Yellow;
        case BrickColor::Red:    return Color::Red;
        case BrickColor::Yellow: return Color::Yellow;
        case BrickColor::Blue:   return Color::Blue;
        case BrickColor::Green:  return Color::Green;
        case BrickColor::Cyan:   return Color::Cyan;
        case BrickColor::Orange: return Color::Orange;
        case BrickColor::Pink:   return Color::Magenta;
        default:                 return Color::White;
    }
}

Color ArkanoidScene::colorForEnemy(EnemyType t) const {
    switch (t) {
        case EnemyType::Cone:     return Color::Orange;
        case EnemyType::Pyramid:  return Color::Cyan;
        case EnemyType::Molecule: return Color::Magenta;
        case EnemyType::Cube:     return Color::Green;
        default:                  return Color::White;
    }
}

// One row-color theme per round, 1-5 — Round 1 is the reference's exact
// layout (arkanoid/rounds/round1.py); Rounds 2-5 are ported faithfully
// IN SPIRIT (each round's real color palette/theme, not its exact
// irregular per-brick coordinate pattern) — see round2.py-round5.py.
static const BrickColor kRoundRowColors[5][5] = {
    { BrickColor::Silver, BrickColor::Red, BrickColor::Yellow, BrickColor::Blue, BrickColor::Green },
    { BrickColor::White, BrickColor::Orange, BrickColor::Cyan, BrickColor::Green, BrickColor::Red },
    { BrickColor::Cyan, BrickColor::Blue, BrickColor::White, BrickColor::Pink, BrickColor::Yellow },
    { BrickColor::Orange, BrickColor::Cyan, BrickColor::Green, BrickColor::Silver, BrickColor::Blue },
    { BrickColor::Orange, BrickColor::Orange, BrickColor::Silver, BrickColor::Silver, BrickColor::Orange },
};

// Enemy type per round, matching each round's real enemy_type from the
// reference (round1.py=cone, round2.py=pyramid, round3.py=molecule,
// round4.py=cube, round5.py=cone).
static const EnemyType kRoundEnemyType[5] = {
    EnemyType::Cone, EnemyType::Pyramid, EnemyType::Molecule, EnemyType::Cube, EnemyType::Cone,
};

void ArkanoidScene::createRoundBricks() {
    int r = round;
    if (r < 1) r = 1;
    if (r > 5) r = 5;
    const BrickColor* rowColors = kRoundRowColors[r - 1];

    // Powerup distribution: not the reference's exact per-round hand-
    // placed indexes (round1.py etc. hardcode which brick index gets
    // which powerup) — instead each non-silver, non-indestructible brick
    // has a flat chance to carry a random one of the 6 ported powerups,
    // landing on a similar overall density without needing five separate
    // hand-transcribed index tables.
    static const PowerupType kPowerupPool[6] = {
        PowerupType::Catch, PowerupType::Expand, PowerupType::Laser,
        PowerupType::SlowBall, PowerupType::ExtraLife, PowerupType::Duplicate,
    };
    constexpr int kPowerupChancePercent = 10;

    bricksRemaining = 0;
    for (int row = 0; row < kBrickRows; ++row) {
        BrickColor c = rowColors[row];
        int baseValue = 0;
        switch (c) {
            case BrickColor::Red: baseValue = 90; break;
            case BrickColor::Yellow: baseValue = 120; break;
            case BrickColor::Blue: baseValue = 100; break;
            case BrickColor::Green: baseValue = 80; break;
            case BrickColor::Cyan: baseValue = 70; break;
            case BrickColor::Orange: baseValue = 60; break;
            case BrickColor::Pink: baseValue = 110; break;
            case BrickColor::White: baseValue = 40; break;
            case BrickColor::Silver: baseValue = 50 * round; break;
            default: break;
        }
        for (int col = 0; col < kBrickCols; ++col) {
            Brick& b = bricks[row][col];
            b.alive = true;
            b.color = c;
            b.value = baseValue;
            b.hitsLeft = (c == BrickColor::Silver) ? 2 : 1;
            b.x = kFieldLeft + col * kBrickW;
            b.y = kFieldTop + row * kBrickH;
            b.hitFlashTimer = 0.0f;
            b.powerup = (rand() % 100 < kPowerupChancePercent)
                ? kPowerupPool[rand() % 6] : PowerupType::None;
            ++bricksRemaining;
        }
    }

    roundEnemyType = kRoundEnemyType[r - 1];
    enemiesReleased = false;
    for (auto& e : enemies) e.active = false;
}

void ArkanoidScene::applyRoundTuning() {
    // Round 3's real tuning (round3.py): ball/paddle both slowed
    // slightly (tighter starting space near the bricks), and the ball
    // returns to base speed faster (normalisation_rate_adjust) so it
    // doesn't stay dangerously fast for long in that confined round.
    // Every other round uses the plain baseline.
    if (round == 3) {
        currentBallBaseSpeed = kBallBaseSpeed * 0.85f;
        currentPaddleSpeed = kPaddleSpeed * 0.85f;
        currentBallNormaliseRate = kBallNormaliseRate * 2.5f;
    } else {
        currentBallBaseSpeed = kBallBaseSpeed;
        currentPaddleSpeed = kPaddleSpeed;
        currentBallNormaliseRate = kBallNormaliseRate;
    }
}

void ArkanoidScene::resetRound() {
    createRoundBricks();
    applyRoundTuning();
    paddleX = SCREEN_W / 2.0f;
    paddleW = kPaddleWBase;
    paddleCatchingBall = false;
    currentPowerup = PowerupType::None;
    for (auto& fp : fallingPowerups) fp.active = false;
    for (auto& l : lasers) l.active = false;

    for (auto& b : balls) b.active = false;
    balls[0].active = true;
    balls[0].attached = true;
    balls[0].x = paddleX;
    balls[0].y = kPaddleY - kBallRadius - 1;
    balls[0].vx = 0.0f;
    balls[0].vy = 0.0f;
}

void ArkanoidScene::init() {
    // Install Arkanoid's own custom sprite palette every time this scene
    // is (re-)entered — see arkanoid_pr32_palette.h's comment for why
    // this is required (this is a global, not-reset-between-scenes
    // setting, so skipping it left colors riding on whatever a
    // PREVIOUSLY played scene had installed).
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_palette::PALETTE_RGB565);

    initStars();
    titlePhase = 0;
    titleTimer = 0.0f;
    score = 0;
    lives = 3;
    round = 1;
    gameOver = false;
    paused = false;
    pauseLatched = false;
    returnToMenuRequested = false;
    returnToMenuLatched = false;
    bossActive = false;
    for (auto& p : bossProjectiles) p.active = false;
    resetRound();
    Scene::init();
}

void ArkanoidScene::update(unsigned long deltaTime) {
    constexpr unsigned long kMaxFrameMs = 50;
    if (deltaTime > kMaxFrameMs) deltaTime = kMaxFrameMs;
    float dt = static_cast<float>(deltaTime) * 0.001f;

    // Phase 0: startup animation, replicated from the real 1986 arcade's
    // own attract sequence (title -> story/mothership -> "ROUND 1"
    // banner) — see drawTitleScreen()/drawStoryScreen()'s comments.
    // A skips ahead early, same convention as every other scene.
    if (titlePhase < 3) {
        titleTimer += dt;
        sfx.update();
        constexpr float kTitleDurationS = 5.0f;
        constexpr float kStoryDurationS = 5.0f;
        constexpr float kBannerDurationS = 1.5f;
        if (titlePhase == 0 && (pendingInput.aEdge || titleTimer >= kTitleDurationS)) {
            titlePhase = 1;
            titleTimer = 0.0f;
            sfx.play(arkanoid_pr32_audio::kIntroJingle, arkanoid_pr32_audio::kIntroJingleLen);
        } else if (titlePhase == 1 && (pendingInput.aEdge || titleTimer >= kStoryDurationS)) {
            titlePhase = 2;
            titleTimer = 0.0f;
        } else if (titlePhase == 2 && titleTimer >= kBannerDurationS) {
            titlePhase = 3;
        }
        Scene::update(deltaTime);
        return;
    }

    constexpr unsigned long kPauseHoldMs = 500;
    constexpr unsigned long kReturnToMenuHoldMs = 2000;
    if (pendingInput.r3Held) {
        if (encSwHeldSinceMs == 0) encSwHeldSinceMs = millis();
        unsigned long heldMs = millis() - encSwHeldSinceMs;
        if (!pauseLatched && heldMs >= kPauseHoldMs) {
            paused = !paused;
            pauseLatched = true;
        }
        if (!returnToMenuLatched && heldMs >= kReturnToMenuHoldMs) {
            returnToMenuRequested = true;
            returnToMenuLatched = true;
        }
    } else {
        encSwHeldSinceMs = 0;
        pauseLatched = false;
        returnToMenuLatched = false;
    }
    if (paused) {
        Scene::update(deltaTime);
        return;
    }

    if (score > hiscore) hiscore = score;
    sfx.update();

    if (gameOver) {
        if (pendingInput.aEdge) init();
        Scene::update(deltaTime);
        return;
    }

    updatePaddle(dt);
    updateBalls(dt);
    updateFallingPowerups(dt);
    updateLasers(dt);

    for (int row = 0; row < kBrickRows; ++row)
        for (int col = 0; col < kBrickCols; ++col)
            if (bricks[row][col].hitFlashTimer > 0.0f) bricks[row][col].hitFlashTimer -= dt;

    if (bossActive) {
        updateBoss(dt);
        updateBossProjectiles(dt);
        checkBossProjectileVsPaddle();
        for (auto& b : balls) if (b.active && !b.attached) checkBossCollision(b);
    } else {
        maybeReleaseEnemies();
        updateEnemies(dt);
        if (bricksRemaining <= 0) {
            sfx.play(arkanoid_pr32_audio::kRoundClearSfx, arkanoid_pr32_audio::kRoundClearSfxLen);
            if (round >= 5) {
                startBossEncounter();
            } else {
                ++round;
                resetRound();
            }
        }
    }

    Scene::update(deltaTime);
}

void ArkanoidScene::updatePaddle(float dt) {
    float tx = pendingInput.thumbX;
    float speed = 0.0f;
    if (fabsf(tx) > 0.3f) {
        speed = (tx > 0 ? 1.0f : -1.0f) * currentPaddleSpeed;
    } else if (pendingInput.encDelta != 0) {
        float mag = fabsf(static_cast<float>(pendingInput.encDelta));
        constexpr float kEncAccelPerTick = 0.8f;
        constexpr float kEncMaxMultiplier = 4.0f;
        float multiplier = 1.0f + mag * kEncAccelPerTick;
        if (multiplier > kEncMaxMultiplier) multiplier = kEncMaxMultiplier;
        speed = (pendingInput.encDelta > 0 ? 1.0f : -1.0f) * currentPaddleSpeed * multiplier;
    }
    paddleX += speed * dt;

    float halfW = paddleW / 2.0f;
    if (paddleX < kFieldLeft + halfW) paddleX = kFieldLeft + halfW;
    if (paddleX > kFieldRight - halfW) paddleX = kFieldRight - halfW;

    for (auto& b : balls) {
        if (!b.active || !b.attached) continue;
        b.x = paddleX;
        b.y = kPaddleY - kBallRadius - 1;
        if (pendingInput.aEdge) {
            b.attached = false;
            // Launch mostly-upward with a slight random horizontal kick,
            // same spirit as the reference's post-materialize launch.
            float angleDeg = 260.0f + (rand() % 21 - 10);  // 250..270
            float rad = angleDeg * (float)M_PI / 180.0f;
            b.vx = currentBallBaseSpeed * cosf(rad);
            b.vy = currentBallBaseSpeed * sinf(rad);
        }
    }

    // Laser fire — only while the Laser powerup is active.
    if (currentPowerup == PowerupType::Laser && pendingInput.rbEdge && laserCooldownTimer <= 0.0f) {
        int fired = 0;
        for (auto& l : lasers) {
            if (l.active) continue;
            l.active = true;
            l.x = paddleX + (fired == 0 ? -halfW * 0.6f : halfW * 0.6f);
            l.y = kPaddleY;
            ++fired;
            if (fired >= 2) break;
        }
        if (fired > 0) {
            laserCooldownTimer = 0.35f;
            sfx.play(arkanoid_pr32_audio::kLaserShotSfx, arkanoid_pr32_audio::kLaserShotSfxLen);
        }
    }
    if (laserCooldownTimer > 0.0f) laserCooldownTimer -= dt;
}

int ArkanoidScene::liveBallCount() const {
    int n = 0;
    for (const auto& b : balls) if (b.active) ++n;
    return n;
}

void ArkanoidScene::updateBalls(float dt) {
    for (auto& b : balls) {
        if (b.active) updateOneBall(b, dt);
    }
    // If every ball has fallen off the bottom, that's a lost life —
    // checked once per frame after all balls have had a chance to move,
    // not per-ball (so losing 2 of 3 multiballs doesn't cost extra lives).
    if (liveBallCount() == 0) {
        loseBall();
    }
}

void ArkanoidScene::updateOneBall(Ball& b, float dt) {
    if (b.attached) return;

    float speed = sqrtf(b.vx * b.vx + b.vy * b.vy);
    if (speed > currentBallBaseSpeed) {
        float newSpeed = speed - currentBallNormaliseRate * dt;
        if (newSpeed < currentBallBaseSpeed) newSpeed = currentBallBaseSpeed;
        float scale = newSpeed / speed;
        b.vx *= scale;
        b.vy *= scale;
    }

    b.x += b.vx * dt;
    b.y += b.vy * dt;

    checkWallCollisions(b);
    checkBrickCollisions(b);
    checkEnemyCollisions(b);

    float halfW = paddleW / 2.0f;
    if (b.vy > 0.0f &&
        b.y + kBallRadius >= kPaddleY && b.y - kBallRadius <= kPaddleY + kPaddleH &&
        b.x >= paddleX - halfW - kBallRadius && b.x <= paddleX + halfW + kBallRadius) {
        if (paddleCatchingBall) {
            // Catch powerup: the ball sticks to the paddle instead of
            // bouncing, released again on the next A press (handled
            // in updatePaddle()'s `attached` branch).
            b.attached = true;
            b.vx = 0.0f;
            b.vy = 0.0f;
            b.y = kPaddleY - kBallRadius - 1;
            sfx.play(arkanoid_pr32_audio::kPowerupCaughtSfx, arkanoid_pr32_audio::kPowerupCaughtSfxLen);
        } else {
            static const float kSegmentAngles[6] = { 220.0f, 245.0f, 260.0f, 280.0f, 295.0f, 320.0f };
            float rel = (b.x - (paddleX - halfW)) / (halfW * 2.0f);
            if (rel < 0.0f) rel = 0.0f;
            if (rel > 0.999f) rel = 0.999f;
            int seg = static_cast<int>(rel * 6.0f);
            float angleDeg = kSegmentAngles[seg];
            angleDeg += (rand() % 11 - 5) * 0.5f;
            float rad = angleDeg * (float)M_PI / 180.0f;
            float newSpeed = sqrtf(b.vx * b.vx + b.vy * b.vy) + kBallWallSpeedAdd;
            if (newSpeed > kBallTopSpeed) newSpeed = kBallTopSpeed;
            b.vx = newSpeed * cosf(rad);
            b.vy = newSpeed * sinf(rad);
            b.y = kPaddleY - kBallRadius - 0.5f;
            sfx.play(arkanoid_pr32_audio::kPaddleBounceSfx, arkanoid_pr32_audio::kPaddleBounceSfxLen);
        }
    }

    if (b.y - kBallRadius > kBallLostY) {
        b.active = false;
    }
}

void ArkanoidScene::checkWallCollisions(Ball& b) {
    bool hit = false;
    if (b.x - kBallRadius < kFieldLeft) {
        b.x = kFieldLeft + kBallRadius;
        b.vx = -b.vx;
        hit = true;
    } else if (b.x + kBallRadius > kFieldRight) {
        b.x = kFieldRight - kBallRadius;
        b.vx = -b.vx;
        hit = true;
    }
    if (b.y - kBallRadius < kTopWallY) {
        b.y = kTopWallY + kBallRadius;
        b.vy = -b.vy;
        hit = true;
    }
    if (hit) {
        float speed = sqrtf(b.vx * b.vx + b.vy * b.vy) + kBallWallSpeedAdd;
        if (speed > kBallTopSpeed) speed = kBallTopSpeed;
        float mag = sqrtf(b.vx * b.vx + b.vy * b.vy);
        if (mag > 0.01f) {
            float scale = speed / mag;
            b.vx *= scale;
            b.vy *= scale;
        }
        sfx.play(arkanoid_pr32_audio::kWallBounceSfx, arkanoid_pr32_audio::kWallBounceSfxLen);
    }
}

void ArkanoidScene::checkBrickCollisions(Ball& b) {
    for (int row = 0; row < kBrickRows; ++row) {
        for (int col = 0; col < kBrickCols; ++col) {
            Brick& brick = bricks[row][col];
            if (!brick.alive) continue;
            float bx0 = brick.x, by0 = brick.y, bx1 = brick.x + kBrickW, by1 = brick.y + kBrickH;
            if (b.x + kBallRadius < bx0 || b.x - kBallRadius > bx1 ||
                b.y + kBallRadius < by0 || b.y - kBallRadius > by1) {
                continue;
            }
            float overlapX = (b.x < (bx0 + bx1) / 2.0f) ? (b.x + kBallRadius - bx0) : (bx1 - (b.x - kBallRadius));
            float overlapY = (b.y < (by0 + by1) / 2.0f) ? (b.y + kBallRadius - by0) : (by1 - (b.y - kBallRadius));
            if (overlapX < overlapY) b.vx = -b.vx;
            else b.vy = -b.vy;

            float speed = sqrtf(b.vx * b.vx + b.vy * b.vy) + kBallBrickSpeedAdd;
            if (speed > kBallTopSpeed) speed = kBallTopSpeed;
            float mag = sqrtf(b.vx * b.vx + b.vy * b.vy);
            if (mag > 0.01f) {
                float scale = speed / mag;
                b.vx *= scale;
                b.vy *= scale;
            }

            --brick.hitsLeft;
            if (brick.hitsLeft <= 0) {
                brick.alive = false;
                score += brick.value;
                --bricksRemaining;
                if (brick.powerup != PowerupType::None) {
                    spawnFallingPowerup(brick.x + kBrickW / 2.0f, brick.y + kBrickH / 2.0f, brick.powerup);
                }
            } else {
                brick.hitFlashTimer = 0.08f;
            }
            sfx.play(arkanoid_pr32_audio::kBrickDestroyedSfx, arkanoid_pr32_audio::kBrickDestroyedSfxLen);
            return;  // one brick per frame is plenty at this ball speed
        }
    }
}

void ArkanoidScene::loseBall() {
    sfx.play(arkanoid_pr32_audio::kBallLostSfx, arkanoid_pr32_audio::kBallLostSfxLen);
    --lives;
    deactivateCurrentPowerup();
    if (lives <= 0) {
        gameOver = true;
        sfx.play(arkanoid_pr32_audio::kGameOverSfx, arkanoid_pr32_audio::kGameOverSfxLen);
        return;
    }
    for (auto& b : balls) b.active = false;
    balls[0].active = true;
    balls[0].attached = true;
    balls[0].x = paddleX;
    balls[0].y = kPaddleY - kBallRadius - 1;
    balls[0].vx = 0.0f;
    balls[0].vy = 0.0f;
}

// ============================================================ POWERUPS
void ArkanoidScene::spawnFallingPowerup(float x, float y, PowerupType type) {
    for (auto& fp : fallingPowerups) {
        if (fp.active) continue;
        fp.active = true;
        fp.type = type;
        fp.x = x;
        fp.y = y;
        return;
    }
}

void ArkanoidScene::updateFallingPowerups(float dt) {
    constexpr float kFallSpeed = 60.0f;
    float halfW = paddleW / 2.0f;
    for (auto& fp : fallingPowerups) {
        if (!fp.active) continue;
        fp.y += kFallSpeed * dt;
        if (fp.y > kBallLostY) {
            fp.active = false;
            continue;
        }
        if (fp.y >= kPaddleY - 2.0f && fp.y <= kPaddleY + kPaddleH &&
            fp.x >= paddleX - halfW && fp.x <= paddleX + halfW) {
            fp.active = false;
            activatePowerup(fp.type);
        }
    }
}

void ArkanoidScene::activatePowerup(PowerupType type) {
    switch (type) {
        case PowerupType::ExtraLife:
            ++lives;
            sfx.play(arkanoid_pr32_audio::kPowerupCaughtSfx, arkanoid_pr32_audio::kPowerupCaughtSfxLen);
            return;
        case PowerupType::Duplicate: {
            // Splits each currently active, launched ball into up to 3
            // total (matches the reference's DuplicatePowerUp: clone with
            // a small angle offset each way).
            int existing = liveBallCount();
            for (int i = 0; i < kMaxBalls && existing > 0; ++i) {
                if (balls[i].active) continue;
                // Find a live launched ball to clone from.
                Ball* source = nullptr;
                for (auto& b : balls) {
                    if (b.active && !b.attached) { source = &b; break; }
                }
                if (!source) break;
                float speed = sqrtf(source->vx * source->vx + source->vy * source->vy);
                if (speed < 1.0f) break;
                float baseAngle = atan2f(source->vy, source->vx);
                float offset = (i % 2 == 0) ? 0.4f : -0.4f;
                balls[i].active = true;
                balls[i].attached = false;
                balls[i].x = source->x;
                balls[i].y = source->y;
                balls[i].vx = speed * cosf(baseAngle + offset);
                balls[i].vy = speed * sinf(baseAngle + offset);
                --existing;
            }
            sfx.play(arkanoid_pr32_audio::kPowerupCaughtSfx, arkanoid_pr32_audio::kPowerupCaughtSfxLen);
            return;
        }
        default: break;
    }

    // Every other powerup is a persistent "current effect" — only one
    // active at a time, matching the reference's own active_powerup
    // concept (catching a new one deactivates whatever was running).
    deactivateCurrentPowerup();
    currentPowerup = type;
    switch (type) {
        case PowerupType::Catch:
            paddleCatchingBall = true;
            break;
        case PowerupType::Expand:
            paddleW = kPaddleWExpanded;
            break;
        case PowerupType::Laser:
            laserCooldownTimer = 0.0f;
            break;
        case PowerupType::SlowBall:
            currentBallBaseSpeed = kBallBaseSpeed * 0.65f;
            for (auto& b : balls) {
                if (!b.active || b.attached) continue;
                float speed = sqrtf(b.vx * b.vx + b.vy * b.vy);
                if (speed > 1.0f) {
                    float scale = currentBallBaseSpeed / speed;
                    b.vx *= scale;
                    b.vy *= scale;
                }
            }
            break;
        default: break;
    }
    sfx.play(arkanoid_pr32_audio::kPowerupCaughtSfx, arkanoid_pr32_audio::kPowerupCaughtSfxLen);
}

void ArkanoidScene::deactivateCurrentPowerup() {
    paddleW = kPaddleWBase;
    paddleCatchingBall = false;
    currentBallBaseSpeed = (round == 3) ? kBallBaseSpeed * 0.85f : kBallBaseSpeed;
    currentPowerup = PowerupType::None;
}

void ArkanoidScene::updateLasers(float dt) {
    constexpr float kLaserSpeed = -320.0f;  // upward
    for (auto& l : lasers) {
        if (!l.active) continue;
        l.y += kLaserSpeed * dt;
        if (l.y < kTopWallY) {
            l.active = false;
            continue;
        }
        bool consumed = false;
        for (int row = 0; row < kBrickRows && !consumed; ++row) {
            for (int col = 0; col < kBrickCols; ++col) {
                Brick& brick = bricks[row][col];
                if (!brick.alive) continue;
                if (l.x < brick.x || l.x > brick.x + kBrickW || l.y < brick.y || l.y > brick.y + kBrickH) continue;
                // Laser bolts destroy bricks outright, no score awarded
                // (matches the reference exactly).
                brick.alive = false;
                --bricksRemaining;
                if (brick.powerup != PowerupType::None) {
                    spawnFallingPowerup(brick.x + kBrickW / 2.0f, brick.y + kBrickH / 2.0f, brick.powerup);
                }
                l.active = false;
                consumed = true;
                sfx.play(arkanoid_pr32_audio::kBrickDestroyedSfx, arkanoid_pr32_audio::kBrickDestroyedSfxLen);
                break;
            }
        }
        if (consumed) continue;
        for (auto& e : enemies) {
            if (!e.active) continue;
            if (fabsf(l.x - e.x) < 8.0f && fabsf(l.y - e.y) < 8.0f) {
                e.active = false;
                score += 500;
                l.active = false;
                sfx.play(arkanoid_pr32_audio::kBrickDestroyedSfx, arkanoid_pr32_audio::kBrickDestroyedSfxLen);
                break;
            }
        }
    }
}

// ============================================================= ENEMIES
void ArkanoidScene::maybeReleaseEnemies() {
    if (enemiesReleased) return;
    bool canRelease;
    if (round == 1) {
        // Round 1 releases once 25% of its bricks are destroyed.
        int total = kBrickRows * kBrickCols;
        canRelease = (total - bricksRemaining) >= total / 4;
    } else {
        canRelease = true;  // every other round releases immediately
    }
    if (!canRelease) return;
    enemiesReleased = true;
    for (int i = 0; i < kMaxEnemies; ++i) {
        Enemy& e = enemies[i];
        e.active = true;
        e.type = roundEnemyType;
        e.x = static_cast<float>(doorX(i % 2));
        e.y = static_cast<float>(kTopWallY + 4);
        e.vx = 0.0f;
        e.vy = 40.0f;
        e.phase = 0;
        e.phaseTimer = 0.0f;
        e.dirChangeTimer = 0.0f;
    }
}

void ArkanoidScene::updateEnemies(float dt) {
    for (auto& e : enemies) {
        if (!e.active) continue;
        e.phaseTimer += dt;
        if (e.phase == 0) {
            // Straight down for a short beat after spawning.
            if (e.phaseTimer >= 0.6f) {
                e.phase = 1;
                e.phaseTimer = 0.0f;
                e.vx = (rand() % 2 == 0 ? 1.0f : -1.0f) * 30.0f;
                e.vy = 25.0f;
            }
        } else {
            e.dirChangeTimer -= dt;
            if (e.dirChangeTimer <= 0.0f) {
                float angle = (static_cast<float>(rand() % 360)) * (float)M_PI / 180.0f;
                e.vx = cosf(angle) * 35.0f;
                e.vy = fabsf(sinf(angle)) * 20.0f + 10.0f;  // gentle net downward drift
                e.dirChangeTimer = 0.5f + static_cast<float>(rand() % 6) * 0.1f;
            }
        }
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        if (e.x < kFieldLeft + 6) { e.x = kFieldLeft + 6; e.vx = fabsf(e.vx); }
        if (e.x > kFieldRight - 6) { e.x = kFieldRight - 6; e.vx = -fabsf(e.vx); }
        if (e.y > kBallLostY) {
            // Reached the bottom without being destroyed — respawn from
            // a door rather than tracking it as a "miss."
            e.active = false;
        }
    }
    // Respawn any inactive enemy slot once released, after a short
    // random delay, keeping up to kMaxEnemies in play — approximates
    // the reference's own release_enemy() self-respawn callback.
    if (enemiesReleased && !bossActive) {
        for (auto& e : enemies) {
            if (e.active) continue;
            if (rand() % 200 == 0) {  // low per-frame chance, spreads respawns out
                e.active = true;
                e.type = roundEnemyType;
                e.x = static_cast<float>(doorX(rand() % 2));
                e.y = static_cast<float>(kTopWallY + 4);
                e.vx = 0.0f;
                e.vy = 40.0f;
                e.phase = 0;
                e.phaseTimer = 0.0f;
            }
        }
    }
}

void ArkanoidScene::checkEnemyCollisions(Ball& b) {
    constexpr float kEnemyHalf = 7.0f;
    for (auto& e : enemies) {
        if (!e.active) continue;
        if (b.x + kBallRadius < e.x - kEnemyHalf || b.x - kBallRadius > e.x + kEnemyHalf ||
            b.y + kBallRadius < e.y - kEnemyHalf || b.y - kBallRadius > e.y + kEnemyHalf) {
            continue;
        }
        float overlapX = (b.x < e.x) ? (b.x + kBallRadius - (e.x - kEnemyHalf)) : ((e.x + kEnemyHalf) - (b.x - kBallRadius));
        float overlapY = (b.y < e.y) ? (b.y + kBallRadius - (e.y - kEnemyHalf)) : ((e.y + kEnemyHalf) - (b.y - kBallRadius));
        if (overlapX < overlapY) b.vx = -b.vx;
        else b.vy = -b.vy;
        e.active = false;
        score += 500;
        sfx.play(arkanoid_pr32_audio::kBrickDestroyedSfx, arkanoid_pr32_audio::kBrickDestroyedSfxLen);
        return;
    }
}

// ================================================================ BOSS
void ArkanoidScene::startBossEncounter() {
    bossActive = true;
    bossHp = kBossMaxHp;
    bossX = SCREEN_W / 2.0f;
    bossY = kTopWallY + 50.0f;
    bossDriftT = 0.0f;
    bossAnimFrame = 0;
    bossAnimTimer = 0.0f;
    bossHitFlashTimer = 0.0f;
    bossFireCooldownTimer = 2.5f;
    for (auto& p : bossProjectiles) p.active = false;
    for (auto& e : enemies) e.active = false;

    paddleX = SCREEN_W / 2.0f;
    for (auto& b : balls) b.active = false;
    balls[0].active = true;
    balls[0].attached = true;
    balls[0].x = paddleX;
    balls[0].y = kPaddleY - kBallRadius - 1;
    balls[0].vx = 0.0f;
    balls[0].vy = 0.0f;
}

void ArkanoidScene::updateBoss(float dt) {
    bossDriftT += dt;
    constexpr float kDriftAmplitude = 70.0f;
    constexpr float kDriftPeriodS = 3.5f;
    bossX = SCREEN_W / 2.0f + sinf(bossDriftT * (2.0f * static_cast<float>(M_PI) / kDriftPeriodS)) * kDriftAmplitude;

    bossAnimTimer += dt;
    if (bossAnimTimer >= 0.4f) {
        bossAnimTimer = 0.0f;
        bossAnimFrame = bossAnimFrame ? 0 : 1;
    }
    if (bossHitFlashTimer > 0.0f) bossHitFlashTimer -= dt;

    bossFireCooldownTimer -= dt;
    if (bossFireCooldownTimer <= 0.0f) {
        for (auto& p : bossProjectiles) {
            if (p.active) continue;
            p.active = true;
            p.x = bossX;
            p.y = bossY + kBossH / 2.0f;
            bossFireCooldownTimer = 2.5f + static_cast<float>(rand() % 15) * 0.1f;
            break;
        }
    }
}

void ArkanoidScene::updateBossProjectiles(float dt) {
    constexpr float kProjectileSpeed = 130.0f;
    for (auto& p : bossProjectiles) {
        if (!p.active) continue;
        p.y += kProjectileSpeed * dt;
        if (p.y > SCREEN_H) p.active = false;
    }
}

void ArkanoidScene::checkBossProjectileVsPaddle() {
    float halfW = paddleW / 2.0f;
    for (auto& p : bossProjectiles) {
        if (!p.active) continue;
        if (p.y + 3.0f < kPaddleY || p.y - 3.0f > kPaddleY + kPaddleH) continue;
        if (p.x < paddleX - halfW || p.x > paddleX + halfW) continue;
        p.active = false;
        loseBall();
    }
}

void ArkanoidScene::checkBossCollision(Ball& b) {
    if (b.attached) return;
    float halfW = kBossW / 2.0f;
    float halfH = kBossH / 2.0f;
    if (b.x + kBallRadius < bossX - halfW || b.x - kBallRadius > bossX + halfW ||
        b.y + kBallRadius < bossY - halfH || b.y - kBallRadius > bossY + halfH) {
        return;
    }
    float overlapX = (b.x < bossX) ? (b.x + kBallRadius - (bossX - halfW)) : ((bossX + halfW) - (b.x - kBallRadius));
    float overlapY = (b.y < bossY) ? (b.y + kBallRadius - (bossY - halfH)) : ((bossY + halfH) - (b.y - kBallRadius));
    if (overlapX < overlapY) b.vx = -b.vx;
    else b.vy = -b.vy;

    float speed = sqrtf(b.vx * b.vx + b.vy * b.vy) + kBallBrickSpeedAdd;
    if (speed > kBallTopSpeed) speed = kBallTopSpeed;
    float mag = sqrtf(b.vx * b.vx + b.vy * b.vy);
    if (mag > 0.01f) {
        float scale = speed / mag;
        b.vx *= scale;
        b.vy *= scale;
    }

    --bossHp;
    bossHitFlashTimer = kBossHitFlashDurationS;
    if (bossHp <= 0) {
        bossActive = false;
        score += 5000;
        sfx.play(arkanoid_pr32_audio::kRoundClearSfx, arkanoid_pr32_audio::kRoundClearSfxLen);
        round = 1;
        resetRound();
    } else {
        sfx.play(arkanoid_pr32_audio::kBrickDestroyedSfx, arkanoid_pr32_audio::kBrickDestroyedSfxLen);
    }
}

void ArkanoidScene::drawBoss(Renderer& renderer) {
    pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_boss::PALETTE_RGB565);
    const pr32::graphics::Sprite4bpp* frame;
    if (bossHitFlashTimer > 0.0f) {
        frame = &arkanoid_pr32_boss::BOSS_HIT;
    } else if (bossHp <= kBossMaxHp / 3) {
        frame = &arkanoid_pr32_boss::BOSS_LOW_HP;
    } else {
        frame = bossAnimFrame ? &arkanoid_pr32_boss::BOSS_IDLE_B : &arkanoid_pr32_boss::BOSS_IDLE_A;
    }
    renderer.drawSprite(*frame, static_cast<int>(bossX) - kBossW / 2, static_cast<int>(bossY) - kBossH / 2);
    pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_palette::PALETTE_RGB565);
}

void ArkanoidScene::drawBossProjectiles(Renderer& renderer) {
    for (const auto& p : bossProjectiles) {
        if (!p.active) continue;
        renderer.drawFilledCircle(static_cast<int>(p.x), static_cast<int>(p.y), 3, Color::Magenta);
        renderer.drawFilledCircle(static_cast<int>(p.x), static_cast<int>(p.y), 1, Color::White);
    }
}

// ================================================================= HUD
void ArkanoidScene::drawHUD(Renderer& renderer) {
    auto centerX = [&](const char* text) {
        return SCREEN_W / 2 - fm::FontManager::textWidth(kHudFont, text, 1) / 2;
    };
    char buf[16];
    renderer.drawText("1UP", 4, 4, Color::Red, 1, kHudFont);
    snprintf(buf, sizeof(buf), "%d", score);
    renderer.drawText(buf, 4, 14, Color::White, 1, kHudFont);

    renderer.drawText("HIGH SCORE", centerX("HIGH SCORE"), 4, Color::Red, 1, kHudFont);
    snprintf(buf, sizeof(buf), "%d", hiscore);
    renderer.drawText(buf, centerX(buf), 14, Color::White, 1, kHudFont);

    snprintf(buf, sizeof(buf), "L%d", lives);
    renderer.drawText(buf, SCREEN_W - 4 - fm::FontManager::textWidth(kHudFont, buf, 1), 4, Color::Gray, 1, kHudFont);
    if (bossActive) {
        snprintf(buf, sizeof(buf), "BOSS");
    } else {
        snprintf(buf, sizeof(buf), "R%d", round);
    }
    renderer.drawText(buf, SCREEN_W - 4 - fm::FontManager::textWidth(kHudFont, buf, 1), 14, Color::Gray, 1, kHudFont);

    if (gameOver) {
        const char* msg = "GAME OVER";
        renderer.drawText(msg, centerX(msg), SCREEN_H / 2 - 4, Color::Red, 1, kHudFont);
        const char* hint = "PRESS A";
        renderer.drawText(hint, centerX(hint), SCREEN_H / 2 + 8, Color::White, 1, kHudFont);
    } else if (paused) {
        const char* msg = "PAUSED";
        renderer.drawText(msg, centerX(msg), SCREEN_H / 2 - 4, Color::White, 1, kHudFont);
    }
}

// ============================================================ PLAYFIELD
void ArkanoidScene::drawPlayfield(Renderer& renderer) {
    const pr32::graphics::Sprite4bpp* bgTop = &arkanoid_pr32_bg::ROUND1_BG_TOP;
    const pr32::graphics::Sprite4bpp* bgBottom = &arkanoid_pr32_bg::ROUND1_BG_BOTTOM;
    const uint16_t* bgPalette = arkanoid_pr32_bg::ROUND1_PALETTE_RGB565;
    if (bossActive) {
        bgTop = &arkanoid_pr32_bg::BOSS_BG_TOP;
        bgBottom = &arkanoid_pr32_bg::BOSS_BG_BOTTOM;
        bgPalette = arkanoid_pr32_bg::BOSS_PALETTE_RGB565;
    } else {
        switch (round) {
            case 2:
                bgTop = &arkanoid_pr32_bg::ROUND2_BG_TOP; bgBottom = &arkanoid_pr32_bg::ROUND2_BG_BOTTOM;
                bgPalette = arkanoid_pr32_bg::ROUND2_PALETTE_RGB565;
                break;
            case 3:
                bgTop = &arkanoid_pr32_bg::ROUND3_BG_TOP; bgBottom = &arkanoid_pr32_bg::ROUND3_BG_BOTTOM;
                bgPalette = arkanoid_pr32_bg::ROUND3_PALETTE_RGB565;
                break;
            case 4:
                bgTop = &arkanoid_pr32_bg::ROUND4_BG_TOP; bgBottom = &arkanoid_pr32_bg::ROUND4_BG_BOTTOM;
                bgPalette = arkanoid_pr32_bg::ROUND4_PALETTE_RGB565;
                break;
            case 5:
                bgTop = &arkanoid_pr32_bg::ROUND5_BG_TOP; bgBottom = &arkanoid_pr32_bg::ROUND5_BG_BOTTOM;
                bgPalette = arkanoid_pr32_bg::ROUND5_PALETTE_RGB565;
                break;
            default: break;
        }
    }
    pr32::graphics::setSpriteCustomPalette(bgPalette);
    renderer.drawSprite(*bgTop, 0, kTopWallY);
    renderer.drawSprite(*bgBottom, 0, kTopWallY + bgTop->height);
    pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_palette::PALETTE_RGB565);

    if (bossActive) {
        drawBoss(renderer);
        drawBossProjectiles(renderer);
    }

    for (int row = 0; row < kBrickRows; ++row) {
        for (int col = 0; col < kBrickCols; ++col) {
            const Brick& b = bricks[row][col];
            if (!b.alive) continue;
            Color base = (b.hitFlashTimer > 0.0f) ? Color::White : colorFor(b.color);
            renderer.drawRectangle(b.x, b.y, kBrickW, kBrickH, Color::Black);
            renderer.drawFilledRectangle(b.x + 1, b.y + 1, kBrickW - 2, kBrickH - 2, base);
            if (b.color == BrickColor::Silver && b.hitsLeft == 2) {
                renderer.drawRectangle(b.x + 1, b.y + 1, kBrickW - 2, kBrickH - 2, Color::White);
            }
        }
    }

    if (!bossActive) drawEnemies(renderer);
    drawFallingPowerups(renderer);
    drawLasers(renderer);

    drawPaddleSprite(renderer);
    for (const auto& b : balls) {
        if (b.active) drawBallSprite(b, renderer);
    }
}

void ArkanoidScene::drawEnemies(Renderer& renderer) {
    for (const auto& e : enemies) {
        if (!e.active) continue;
        int x = static_cast<int>(e.x), y = static_cast<int>(e.y);
        Color c = colorForEnemy(e.type);
        switch (e.type) {
            case EnemyType::Cone:
                renderer.drawFilledRectangle(x - 1, y - 6, 2, 4, c);
                renderer.drawFilledRectangle(x - 5, y - 2, 10, 7, c);
                break;
            case EnemyType::Pyramid:
                renderer.drawFilledRectangle(x - 6, y + 4, 12, 2, c);
                renderer.drawFilledRectangle(x - 4, y, 8, 4, c);
                renderer.drawFilledRectangle(x - 2, y - 4, 4, 4, c);
                break;
            case EnemyType::Molecule:
                renderer.drawFilledCircle(x, y, 4, c);
                renderer.drawFilledCircle(x - 6, y - 3, 2, c);
                renderer.drawFilledCircle(x + 6, y - 3, 2, c);
                renderer.drawFilledCircle(x, y + 6, 2, c);
                break;
            case EnemyType::Cube:
            default:
                renderer.drawFilledRectangle(x - 6, y - 6, 12, 12, c);
                renderer.drawRectangle(x - 6, y - 6, 12, 12, Color::Black);
                break;
        }
    }
}

void ArkanoidScene::drawFallingPowerups(Renderer& renderer) {
    for (const auto& fp : fallingPowerups) {
        if (!fp.active) continue;
        const char* label = "?";
        Color c = Color::White;
        switch (fp.type) {
            case PowerupType::Catch:     label = "C"; c = Color::Green; break;
            case PowerupType::Expand:    label = "E"; c = Color::Cyan; break;
            case PowerupType::Laser:     label = "L"; c = Color::Red; break;
            case PowerupType::SlowBall:  label = "S"; c = Color::Yellow; break;
            case PowerupType::ExtraLife: label = "P"; c = Color::Magenta; break;
            case PowerupType::Duplicate: label = "D"; c = Color::Orange; break;
            default: break;
        }
        int x = static_cast<int>(fp.x) - 6, y = static_cast<int>(fp.y) - 5;
        renderer.drawFilledRectangle(x, y, 12, 10, c);
        renderer.drawRectangle(x, y, 12, 10, Color::Black);
        renderer.drawText(label, x + 3, y + 1, Color::Black, 1, kHudFont);
    }
}

void ArkanoidScene::drawLasers(Renderer& renderer) {
    for (const auto& l : lasers) {
        if (!l.active) continue;
        renderer.drawFilledRectangle(static_cast<int>(l.x) - 1, static_cast<int>(l.y) - 4, 2, 8, Color::Red);
    }
}

// The real Vaus paddle sprite (arkanoid_pr32_paddle.h) — has its own
// dedicated palette, same "switch, draw, switch back" dance as the
// background sprite. Drawn stretched to `paddleW` (not the sprite's own
// fixed width) so the Expand powerup visually widens it.
void ArkanoidScene::drawPaddleSprite(Renderer& renderer) {
    float halfW = paddleW / 2.0f;
    int x0 = static_cast<int>(paddleX - halfW);
    int y0 = static_cast<int>(kPaddleY);

    pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_paddle::PALETTE_RGB565);
    if (paddleW > kPaddleWBase + 0.5f) {
        // No scaled drawSprite for Sprite4bpp (see Renderer.h) — when
        // expanded, fall back to a simple filled bar in the paddle
        // sprite's own dominant gray so the widened hitbox still reads
        // clearly as "the paddle," rather than stretching/tiling the
        // fixed-size art in a way that would look broken.
        renderer.drawFilledRectangle(x0, y0, static_cast<int>(paddleW), static_cast<int>(kPaddleH), Color::Gray);
        renderer.drawFilledRectangle(x0, y0 + 3, static_cast<int>(paddleW), 2, Color::White);
        renderer.drawFilledRectangle(x0, y0, 6, static_cast<int>(kPaddleH), Color::Orange);
        renderer.drawFilledRectangle(x0 + static_cast<int>(paddleW) - 6, y0, 6, static_cast<int>(kPaddleH), Color::Orange);
    } else {
        renderer.drawSprite(arkanoid_pr32_paddle::PADDLE, x0, y0);
    }
    pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_palette::PALETTE_RGB565);

    renderer.drawFilledRectangle(x0, y0 + static_cast<int>(kPaddleH), static_cast<int>(paddleW), 1, Color::Navy);
}

void ArkanoidScene::drawBallSprite(const Ball& b, Renderer& renderer) {
    int cx = static_cast<int>(b.x), cy = static_cast<int>(b.y);
    int r = static_cast<int>(kBallRadius);
    renderer.drawFilledCircle(cx, cy, r, Color::White);
    renderer.drawCircle(cx, cy, r, Color::Navy);
}

// Real 1986 Taito arcade's own title screen (from a cabinet-recording
// reference the user linked) — deliberately excludes the copyright-
// warning line, same convention as Pac-Man's attract screen. "ARKANOID"
// is rendered via the shared retro font rather than a hand-drawn logo
// bitmap (see the plan's scoping note).
void ArkanoidScene::drawTitleScreen(Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);

    auto centerX = [&](const char* text, const pr32::graphics::Font* f, uint8_t size = 1) {
        return SCREEN_W / 2 - fm::FontManager::textWidth(f, text, size) / 2;
    };

    renderer.drawText("1UP", 24, 20, Color::White, 1, kHudFont);
    renderer.drawText("00", 26, 30, Color::White, 1, kHudFont);
    const char* hs = "HIGH SCORE";
    renderer.drawText(hs, centerX(hs, kHudFont), 20, Color::White, 1, kHudFont);
    char hbuf[12];
    snprintf(hbuf, sizeof(hbuf), "%d", hiscore);
    renderer.drawText(hbuf, centerX(hbuf, kHudFont), 30, Color::White, 1, kHudFont);

    {
        const char* title = "ARKANOID";
        constexpr uint8_t kTitleSize = 2;
        int tx = centerX(title, kLogoFont, kTitleSize);
        int ty = 90;
        renderer.drawText(title, tx + 2, ty + 2, Color::Black, kTitleSize, kLogoFont);
        Color pulse = (fmodf(titleTimer, 0.8f) < 0.4f) ? Color::Red : Color::Yellow;
        renderer.drawText(title, tx, ty, pulse, kTitleSize, kLogoFont);
    }

    {
        constexpr float kFrameDurationS = 0.1f;
        int frame = static_cast<int>(titleTimer / kFrameDurationS) % arkanoid_pr32_ship::SHIP_NUM_FRAMES;
        constexpr float kOscillationPeriodS = 3.0f;
        constexpr float kOscillationAmplitude = 70.0f;
        float centerXf = SCREEN_W / 2.0f +
            sinf(titleTimer * (2.0f * static_cast<float>(M_PI) / kOscillationPeriodS)) * kOscillationAmplitude;
        int shipX = static_cast<int>(centerXf) - 16;
        int shipY = 135;
        pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_ship::PALETTE_RGB565);
        renderer.drawSprite(*arkanoid_pr32_ship::SHIP_FRAMES[frame], shipX, shipY);
        pr32::graphics::setSpriteCustomPalette(arkanoid_pr32_palette::PALETTE_RGB565);
    }

    if (fmodf(titleTimer, 1.0f) < 0.7f) {
        const char* push = "PUSH ONLY 1 PLAYER BUTTON";
        renderer.drawText(push, centerX(push, kHudFont), 180, Color::White, 1, kHudFont);
    }
    renderer.drawText("CREDIT 1", 24, SCREEN_H - 24, Color::White, 1, kHudFont);
}

void ArkanoidScene::drawStoryScreen(Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
    for (auto& s : stars) renderer.drawFilledRectangle(s.x, s.y, 1, 1, s.color);

    int shipX = SCREEN_W / 2, shipY = 90;
    renderer.drawFilledRectangle(shipX - 14, shipY, 28, 5, Color::Cyan);
    renderer.drawFilledRectangle(shipX - 6, shipY - 3, 12, 3, Color::Cyan);

    static const char* kLines[] = {
        "AFTER THE MOTHERSHIP",
        "\"ARKANOID\" WAS DESTROYED,",
        "A SPACECRAFT \"VAUS\"",
        "SCRAMBLED AWAY FROM IT.",
    };
    constexpr int kNumLines = 4;
    constexpr float kCharsPerSec = 14.0f;
    int totalChars = static_cast<int>(titleTimer * kCharsPerSec);

    int y = 140;
    int charsShown = 0;
    for (int i = 0; i < kNumLines; ++i) {
        int lineLen = static_cast<int>(strlen(kLines[i]));
        int show = totalChars - charsShown;
        if (show > lineLen) show = lineLen;
        if (show > 0) {
            char buf[40];
            int n = show < 39 ? show : 39;
            memcpy(buf, kLines[i], n);
            buf[n] = '\0';
            int bw = fm::FontManager::textWidth(kHudFont, buf, 1);
            renderer.drawText(buf, SCREEN_W / 2 - bw / 2, y, Color::White, 1, kHudFont);
        }
        charsShown += lineLen;
        y += 14;
    }
}

void ArkanoidScene::drawRoundBanner(Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
    drawPlayfield(renderer);
    drawHUD(renderer);

    char buf[16];
    snprintf(buf, sizeof(buf), "ROUND %d", round);
    int w = fm::FontManager::textWidth(kHudFont, buf, 1);
    renderer.drawText(buf, SCREEN_W / 2 - w / 2, SCREEN_H / 2 - 4, Color::Yellow, 1, kHudFont);
}

void ArkanoidScene::debugSkipRound() {
    if (bossActive) return;
    if (round >= 5) {
        startBossEncounter();
    } else {
        ++round;
        resetRound();
    }
}

void ArkanoidScene::debugNearKillBoss() {
    if (bossActive) bossHp = 1;
}

void ArkanoidScene::debugForcePowerup() {
    static int cycle = 0;
    static const PowerupType kAll[6] = {
        PowerupType::Catch, PowerupType::Expand, PowerupType::Laser,
        PowerupType::SlowBall, PowerupType::ExtraLife, PowerupType::Duplicate,
    };
    spawnFallingPowerup(paddleX, kPaddleY - 20.0f, kAll[cycle % 6]);
    ++cycle;
}

void ArkanoidScene::draw(Renderer& renderer) {
    if (titlePhase == 0) { drawTitleScreen(renderer); }
    else if (titlePhase == 1) { drawStoryScreen(renderer); }
    else if (titlePhase == 2) { drawRoundBanner(renderer); }
    else {
        renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
        drawPlayfield(renderer);
        drawHUD(renderer);
    }
}

} // namespace arkanoid_pr32
