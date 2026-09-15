#include "rtype_pr32.h"
#include "rtype_pr32_sprites.h"
#include "rtype_pr32_boss.h"
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace pr32 = pixelroot32;

namespace rtype_pr32 {

namespace {
Preferences prefs;

bool overlaps(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Adafruit_GFX's built-in default font is 6px/char wide at size=1 (this
// project's whole convention elsewhere uses either that or a custom
// Font*; this game keeps it simple with the built-in one, so this
// assumption is what centers text manually instead of going through
// FontManager::textWidth, which needs an explicit Font*).
int textWidthEstimate(const char* s, int size) { return static_cast<int>(strlen(s)) * 6 * size; }
} // namespace

void RTypeScene::loadHighScore() {
    prefs.begin("rtype", true);
    highScore = prefs.getInt("hi", 0);
    prefs.end();
}
void RTypeScene::saveHighScoreIfNeeded() {
    if (score > highScore) {
        highScore = score;
        prefs.begin("rtype", false);
        prefs.putInt("hi", highScore);
        prefs.end();
    }
}

void RTypeScene::init() {
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);
    pr32::graphics::setSpriteCustomPalette(rtype_pr32_sprites::PALETTE_RGB565);

    loadHighScore();
    resetGame();
}

void RTypeScene::resetGame() {
    shipX = SCREEN_W / 2.0f - kShipW / 2.0f;
    shipY = SCREEN_H - kShipH - 30.0f;
    lives = 3;
    score = 0;
    weaponType = 0;
    worldDistance = 0.0f;
    invulnerable = false;
    lastEncTickMs = 0;
    prevAHeld = false;
    prevR3Held = false;
    pauseLatched = false;
    for (auto& b : pbullets) b.active = false;
    for (auto& b : ebullets) b.active = false;
    for (auto& e : enemies) e.active = false;
    for (auto& d : debris) d.active = false;
    for (auto& c : capsules) c.active = false;
    for (auto& p : particles) p.active = false;
    for (auto& b : boss) b.active = false;
    bossHasSplit = false;
    bossSettled = false;
    initStars();
    lastEnemySpawnMs = millis();
    lastDebrisSpawnMs = millis();
    state = GameState::Playing;
}

// ============================================================ SHIP =====
void RTypeScene::updateShipMovement(float dt) {
    constexpr float kEncSensBase = 2.2f;
    constexpr float kEncSensMaxBonus = 5.0f;
    constexpr float kEncAccelScale = 0.035f;
    constexpr float kKeySpeedY = 150.0f;
    constexpr float kShipYMin = SCREEN_H * 0.45f;
    constexpr float kShipYMax = SCREEN_H - kShipH - 3;

    if (pendingInput.encDelta != 0) {
        unsigned long now = millis();
        float sens = kEncSensBase;
        if (lastEncTickMs != 0) {
            float intervalSec = (now - lastEncTickMs) / 1000.0f;
            if (intervalSec > 0.001f) {
                float rate = fabsf((float)pendingInput.encDelta) / intervalSec;
                float bonus = rate * kEncAccelScale;
                if (bonus < 0.0f) bonus = 0.0f;
                if (bonus > kEncSensMaxBonus) bonus = kEncSensMaxBonus;
                sens += bonus;
            }
        }
        lastEncTickMs = now;
        shipX += pendingInput.encDelta * sens;
    }
    if (pendingInput.rbHeld) shipY += kKeySpeedY * dt;
    if (pendingInput.lbHeld) shipY -= kKeySpeedY * dt;

    constexpr float kThumbSpeed = 190.0f;
    shipX += pendingInput.thumbX * kThumbSpeed * dt;
    shipY += -pendingInput.thumbY * kThumbSpeed * dt;

    if (shipX < kShipXMin) shipX = kShipXMin;
    if (shipX > kShipXMax) shipX = kShipXMax;
    if (shipY < kShipYMin) shipY = kShipYMin;
    if (shipY > kShipYMax) shipY = kShipYMax;
}

// ==================================================== PLAYER BULLETS ====
void RTypeScene::spawnPBullet(float x, float y, float vy, int kind, int pierce) {
    for (auto& b : pbullets) {
        if (!b.active) { b = { true, x, y, vy, kind, pierce }; return; }
    }
}

void RTypeScene::updatePBullets(float dt) {
    for (auto& b : pbullets) {
        if (!b.active) continue;
        b.y += b.vy * dt;
        if (b.y < -20) b.active = false;
    }
}

void RTypeScene::drawPBullets(pr32::graphics::Renderer& renderer) {
    namespace spr = rtype_pr32_sprites;
    for (auto& b : pbullets) {
        if (!b.active) continue;
        const pr32::graphics::Sprite4bpp* s = &spr::RT_BULLET_WEAK;
        if (b.kind == 1) s = &spr::RT_BULLET_CHARGED;
        else if (b.kind == 2) s = &spr::RT_BULLET_DRONE;
        renderer.drawSprite(*s, (int)b.x - s->width / 2, (int)b.y, false);
    }
}

// ===================================================== ENEMY BULLETS ====
void RTypeScene::spawnEBullet(float x, float y, float vx, float vy, bool homing) {
    for (auto& b : ebullets) {
        if (!b.active) { b = { true, x, y, vx, vy, homing }; return; }
    }
}

void RTypeScene::updateEBullets(float dt) {
    for (auto& b : ebullets) {
        if (!b.active) continue;
        if (b.homing) {
            float targetVx = (shipX - b.x) * 1.8f;
            if (targetVx < -70.0f) targetVx = -70.0f;
            if (targetVx > 70.0f) targetVx = 70.0f;
            float blend = dt * 3.0f;
            if (blend > 1.0f) blend = 1.0f;
            b.vx += (targetVx - b.vx) * blend;
        }
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.x < -10 || b.x > SCREEN_W + 10 || b.y > SCREEN_H + 10) b.active = false;
    }
}

void RTypeScene::drawEBullets(pr32::graphics::Renderer& renderer) {
    namespace spr = rtype_pr32_sprites;
    for (auto& b : ebullets) {
        if (!b.active) continue;
        renderer.drawSprite(spr::RT_ENEMY_BULLET, (int)b.x - spr::RT_ENEMY_BULLET.width / 2,
                             (int)b.y - spr::RT_ENEMY_BULLET.height / 2, false);
    }
}

// ========================================================= PARTICLES ====
void RTypeScene::spawnParticles(float x, float y, pr32::graphics::Color color, int count) {
    for (int n = 0; n < count; ++n) {
        for (auto& p : particles) {
            if (!p.active) {
                float ang = (rand() % 360) * 3.14159265f / 180.0f;
                float spd = 25.0f + (rand() % 55);
                p = { true, x, y, spd * cosf(ang), spd * sinf(ang), millis(), color };
                break;
            }
        }
    }
}

void RTypeScene::updateParticles(float dt) {
    unsigned long now = millis();
    for (auto& p : particles) {
        if (!p.active) continue;
        if (now - p.bornMs > kParticleLifeMs) { p.active = false; continue; }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.x < 0 || p.x > SCREEN_W || p.y < PLAY_TOP || p.y > SCREEN_H) p.active = false;
    }
}

void RTypeScene::drawParticles(pr32::graphics::Renderer& renderer) {
    for (auto& p : particles) {
        if (!p.active) continue;
        renderer.drawFilledRectangle((int)p.x, (int)p.y, 2, 2, p.color);
    }
}

// =========================================== DEBRIS + ENEMIES (spawn) ====
void RTypeScene::spawnDrifter() {
    for (auto& e : enemies) {
        if (!e.active) {
            float x = 14.0f + (rand() % (SCREEN_W - 28));
            e = { true, EnemyType::Drifter, x, -14.0f, x, (float)(rand() % 628) / 100.0f, 2, 0, -1 };
            return;
        }
    }
}

void RTypeScene::spawnDebrisWithMaybeTurret() {
    namespace spr = rtype_pr32_sprites;
    int slot = -1;
    for (int i = 0; i < kMaxDebris; ++i) if (!debris[i].active) { slot = i; break; }
    if (slot < 0) return;
    int variant = rand() % kDebrisVariantCount;
    int w = 0, h = 0;
    switch (variant) {
        case 0: w = spr::RT_DEBRIS_0.width; h = spr::RT_DEBRIS_0.height; break;
        case 1: w = spr::RT_DEBRIS_1.width; h = spr::RT_DEBRIS_1.height; break;
        default: w = spr::RT_DEBRIS_2.width; h = spr::RT_DEBRIS_2.height; break;
    }
    float x = 4.0f + (rand() % (SCREEN_W - w - 8));
    debris[slot] = { true, x, -(float)h - 10.0f, w, h, 4, -1, variant };

    if (rand() % 100 < 55) {
        for (int i = 0; i < kMaxEnemies; ++i) {
            if (!enemies[i].active) {
                enemies[i] = { true, EnemyType::Turret, x + w / 2.0f, debris[slot].y + h / 2.0f, x, 0, 3, millis(), slot };
                debris[slot].turretIdx = i;
                break;
            }
        }
    }
}

void RTypeScene::updateDebrisAndTurrets(float dt) {
    unsigned long now = millis();
    for (int i = 0; i < kMaxDebris; ++i) {
        if (!debris[i].active) continue;
        Debris& d = debris[i];
        d.y += kScrollSpeed * dt;
        if (d.turretIdx >= 0 && enemies[d.turretIdx].active) {
            Enemy& t = enemies[d.turretIdx];
            t.x = d.x + d.w / 2.0f;
            t.y = d.y + d.h / 2.0f;
            if (now - t.lastFireMs > 1700 && t.y < SCREEN_H && t.y > 0) {
                t.lastFireMs = now;
                float dx = shipX - t.x, dy = shipY - t.y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 1) spawnEBullet(t.x, t.y, dx / len * 90.0f, dy / len * 90.0f);
            }
        }
        if (d.y > SCREEN_H) {
            d.active = false;
            if (d.turretIdx >= 0) enemies[d.turretIdx].active = false;
        }
    }
}

void RTypeScene::updateDrifters(float dt) {
    for (auto& e : enemies) {
        if (!e.active || e.type != EnemyType::Drifter) continue;
        e.y += (kScrollSpeed + kEnemyExtraSpeed) * dt;
        e.phase += dt * 2.2f;
        e.x = e.baseX + sinf(e.phase) * 18.0f;
        if (e.y > SCREEN_H + 12) e.active = false;
    }
}

void RTypeScene::drawDebris(pr32::graphics::Renderer& renderer) {
    namespace spr = rtype_pr32_sprites;
    for (auto& d : debris) {
        if (!d.active) continue;
        const pr32::graphics::Sprite4bpp* s = &spr::RT_DEBRIS_0;
        if (d.variant == 1) s = &spr::RT_DEBRIS_1;
        else if (d.variant == 2) s = &spr::RT_DEBRIS_2;
        renderer.drawSprite(*s, (int)d.x, (int)d.y, false);
    }
}

void RTypeScene::drawEnemies(pr32::graphics::Renderer& renderer) {
    namespace spr = rtype_pr32_sprites;
    for (auto& e : enemies) {
        if (!e.active) continue;
        if (e.type == EnemyType::Drifter) {
            renderer.drawSprite(spr::RT_DRIFTER, (int)e.x - spr::RT_DRIFTER.width / 2,
                                 (int)e.y - spr::RT_DRIFTER.height / 2, false);
        } else {
            renderer.drawSprite(spr::RT_TURRET, (int)e.x - spr::RT_TURRET.width / 2,
                                 (int)e.y - spr::RT_TURRET.height / 2, false);
        }
    }
}

// ============================================================ CAPSULES ====
void RTypeScene::maybeDropCapsule(float x, float y) {
    if (rand() % 100 >= 22) return;
    for (auto& c : capsules) {
        if (!c.active) { c = { true, x, y, rand() % 2 }; return; }
    }
}

void RTypeScene::updateCapsules(float dt) {
    for (auto& c : capsules) {
        if (!c.active) continue;
        c.y += kScrollSpeed * dt;
        if (c.y > SCREEN_H + 12) c.active = false;
    }
}

void RTypeScene::drawCapsules(pr32::graphics::Renderer& renderer) {
    namespace spr = rtype_pr32_sprites;
    for (auto& c : capsules) {
        if (!c.active) continue;
        const pr32::graphics::Sprite4bpp& s = c.kind == 0 ? spr::RT_CAPSULE_SPEED : spr::RT_CAPSULE_WEAPON;
        renderer.drawSprite(s, (int)c.x, (int)c.y, false);
    }
}

// ================================================================ STARS ====
void RTypeScene::initStars() {
    for (auto& s : stars) {
        s.x = rand() % SCREEN_W;
        s.y = PLAY_TOP + rand() % (SCREEN_H - PLAY_TOP);
        bool near = rand() % 2;
        s.speed = near ? (28 + rand() % 14) : (8 + rand() % 10);
        s.color = near ? pr32::graphics::Color::White : pr32::graphics::Color::Gray;
    }
}

void RTypeScene::updateStars(float dt) {
    for (auto& s : stars) {
        s.y += s.speed * dt;
        if (s.y >= SCREEN_H) {
            s.y = PLAY_TOP;
            s.x = rand() % SCREEN_W;
        }
    }
}

void RTypeScene::drawStars(pr32::graphics::Renderer& renderer) {
    for (auto& s : stars) {
        renderer.drawPixel((int)s.x, (int)s.y, s.color);
    }
}

// ================================================== FIRE / CHARGE (A) ====
void RTypeScene::fireWeapon(bool charged) {
    float noseX = shipX + kShipW / 2.0f;
    float noseY = shipY - 2;
    if (charged) {
        spawnPBullet(noseX, noseY, -260.0f, 1, 3);
        sfx.play(rtype_pr32_audio::kFireChargedSfx, rtype_pr32_audio::kFireChargedSfxLen);
    } else {
        constexpr float kVy = -220.0f;
        if (weaponType == 0) {
            spawnPBullet(noseX, noseY, kVy, 0, 1);
        } else if (weaponType == 1) {
            spawnPBullet(noseX, noseY, kVy, 0, 1);
            spawnPBullet(noseX - 6, noseY, kVy, 0, 1);
            spawnPBullet(noseX + 6, noseY, kVy, 0, 1);
        } else {
            spawnPBullet(noseX, noseY, kVy * 1.3f, 0, 1);
        }
        sfx.play(rtype_pr32_audio::kFireWeakSfx, rtype_pr32_audio::kFireWeakSfxLen);
    }
}

void RTypeScene::updateFireControl() {
    if (pendingInput.aHeld && !prevAHeld) aHeldSinceMs = millis();
    if (!pendingInput.aHeld && prevAHeld) {
        unsigned long heldFor = millis() - aHeldSinceMs;
        fireWeapon(heldFor >= kChargeFullMs);
    }
    prevAHeld = pendingInput.aHeld;

    unsigned long now = millis();
    if (now - lastDroneFireMs >= kDroneFireIntervalMs) {
        lastDroneFireMs = now;
        spawnPBullet(shipX + kShipW / 2.0f, shipY + kDroneDockOffsetY - 2, -200.0f, 2, 1);
    }
}

// ======================================== WEAPON CYCLE / PAUSE (R3) ====
void RTypeScene::updateR3Control() {
    if (pendingInput.r3Held && !prevR3Held) { r3HeldSinceMs = millis(); pauseLatched = false; }
    if (pendingInput.r3Held && !pauseLatched && millis() - r3HeldSinceMs >= kPauseHoldMs
        && (state == GameState::Playing || state == GameState::BossFight)) {
        pauseLatched = true;
        stateBeforePause = state;
        state = GameState::Paused;
    }
    if (pendingInput.r3Edge && (state == GameState::Playing || state == GameState::BossFight)) {
        weaponType = (weaponType + 1) % 3;
        sfx.play(rtype_pr32_audio::kWeaponCycleSfx, rtype_pr32_audio::kWeaponCycleSfxLen);
    }
    prevR3Held = pendingInput.r3Held;
}

// ============================================================ COLLISION ====
void RTypeScene::killShip() {
    lives--;
    spawnParticles(shipX + kShipW / 2, shipY + kShipH / 2, pr32::graphics::Color::Red, 14);
    sfx.play(rtype_pr32_audio::kShipHitSfx, rtype_pr32_audio::kShipHitSfxLen);
    if (lives <= 0) {
        state = GameState::GameOver;
        saveHighScoreIfNeeded();
    } else {
        invulnerable = true;
        invulnUntilMs = millis() + 2000;
    }
}

void RTypeScene::checkCollisions() {
    for (auto& b : pbullets) {
        if (!b.active) continue;
        for (auto& e : enemies) {
            if (!e.active) continue;
            float r = (e.type == EnemyType::Drifter) ? 7.0f : 5.0f;
            if (overlaps(b.x - 2, b.y - 2, 4, 4, e.x - r, e.y - r, r * 2, r * 2)) {
                e.hp--;
                b.pierce--;
                if (e.hp <= 0) {
                    score += (e.type == EnemyType::Drifter) ? 100 : 150;
                    spawnParticles(e.x, e.y, pr32::graphics::Color::Purple, 10);
                    e.active = false;
                    if (e.debrisIdx >= 0 && debris[e.debrisIdx].active) debris[e.debrisIdx].turretIdx = -1;
                    maybeDropCapsule(e.x, e.y);
                    sfx.play(rtype_pr32_audio::kEnemyBreakSfx, rtype_pr32_audio::kEnemyBreakSfxLen);
                }
                if (b.pierce <= 0) { b.active = false; break; }
            }
        }
        if (!b.active) continue;
        for (auto& d : debris) {
            if (!d.active) continue;
            if (overlaps(b.x - 2, b.y - 2, 4, 4, d.x, d.y, (float)d.w, (float)d.h)) {
                d.hp--;
                b.pierce--;
                if (d.hp <= 0) {
                    score += 60;
                    spawnParticles(d.x + d.w / 2.0f, d.y + d.h / 2.0f, pr32::graphics::Color::Gray, 12);
                    if (d.turretIdx >= 0) enemies[d.turretIdx].active = false;
                    d.active = false;
                    sfx.play(rtype_pr32_audio::kEnemyBreakSfx, rtype_pr32_audio::kEnemyBreakSfxLen);
                }
                if (b.pierce <= 0) { b.active = false; break; }
            }
        }
    }

    if (invulnerable) {
        if (millis() >= invulnUntilMs) invulnerable = false;
        return;
    }

    float sx = shipX, sy = shipY, sw = kShipW, sh = kShipH;
    for (auto& e : enemies) {
        if (!e.active) continue;
        float r = (e.type == EnemyType::Drifter) ? 7.0f : 5.0f;
        if (overlaps(sx, sy, sw, sh, e.x - r, e.y - r, r * 2, r * 2)) { killShip(); return; }
    }
    for (auto& d : debris) {
        if (!d.active) continue;
        if (overlaps(sx, sy, sw, sh, d.x, d.y, (float)d.w, (float)d.h)) { killShip(); return; }
    }
    for (auto& b : ebullets) {
        if (!b.active) continue;
        if (overlaps(sx, sy, sw, sh, b.x - 2, b.y - 2, 4, 4)) {
            b.active = false;
            killShip();
            return;
        }
    }
    for (auto& c : capsules) {
        if (!c.active) continue;
        namespace spr = rtype_pr32_sprites;
        if (overlaps(sx, sy, sw, sh, c.x, c.y, (float)spr::RT_CAPSULE_SPEED.width, (float)spr::RT_CAPSULE_SPEED.height)) {
            c.active = false;
            score += 40;
            if (c.kind == 1) weaponType = (weaponType + 1) % 3;
            sfx.play(rtype_pr32_audio::kPickupSfx, rtype_pr32_audio::kPickupSfxLen);
        }
    }
}

// =============================================================== BOSS ====
int RTypeScene::bossHpRemaining() const {
    int hp = 0;
    for (auto& b : boss) if (b.active) hp += b.hp;
    return hp;
}
int RTypeScene::bossHpMax() const {
    return bossHasSplit ? kBossSplitHp * kMaxBossUnits : kBossMainHp;
}

void RTypeScene::spawnBoss() {
    for (auto& b : boss) b.active = false;
    boss[0] = { true, SCREEN_W / 2.0f, 60.0f, 18.0f, SCREEN_W / 2.0f, 0, kBossMainHp, kBossMainHp, 0, 13 };
    bossHasSplit = false;
    bossSettled = false;
    bossSettleToggleMs = millis() + 2600;
}

bool bossUnitIsMain(const BossUnit& b) { return b.radius > 10.0f; }

void RTypeScene::updateBossUnit(BossUnit& b, float dt, float speedMul, unsigned long fireIntervalMs, bool canSettle) {
    bool settled = canSettle && bossSettled;
    if (!settled) {
        b.y += b.vy * speedMul * dt;
        constexpr float kPatrolMin = PLAY_TOP + 30.0f, kPatrolMax = 110.0f;
        if (b.y < kPatrolMin) { b.y = kPatrolMin; b.vy = fabsf(b.vy); }
        if (b.y > kPatrolMax) { b.y = kPatrolMax; b.vy = -fabsf(b.vy); }
    } else {
        constexpr float kTargetY = 150.0f;
        float blend = dt * 2.0f;
        if (blend > 1.0f) blend = 1.0f;
        b.y += (kTargetY - b.y) * blend;
    }
    b.phase += dt * 1.6f;
    b.x = b.baseX + sinf(b.phase) * 44.0f;
    if (b.x < 14.0f) b.x = 14.0f;
    if (b.x > SCREEN_W - 14.0f) b.x = SCREEN_W - 14.0f;

    unsigned long now = millis();
    if (now - b.lastFireMs > fireIntervalMs) {
        b.lastFireMs = now;
        if (settled) {
            spawnEBullet(b.x, b.y + b.radius, 0.0f, 130.0f, true);
        } else {
            spawnEBullet(b.x, b.y + b.radius, -40.0f, 140.0f);
            spawnEBullet(b.x, b.y + b.radius, 0.0f, 150.0f);
            spawnEBullet(b.x, b.y + b.radius, 40.0f, 140.0f);
        }
    }
}

void RTypeScene::splitBoss() {
    float x = boss[0].x, y = boss[0].y;
    bool wasActive = boss[0].active;
    boss[0].active = false;
    if (!wasActive) return;
    float offsets[3] = { -34.0f, 0.0f, 34.0f };
    for (int i = 0; i < kMaxBossUnits; ++i) {
        boss[i] = { true, x + offsets[i], y, (i % 2 == 0) ? -60.0f : 60.0f, x + offsets[i], (float)i * 2.0f, kBossSplitHp, kBossSplitHp, 0, 8 };
    }
    bossHasSplit = true;
    spawnParticles(x, y, pr32::graphics::Color::Orange, 20);
    sfx.play(rtype_pr32_audio::kBossHitSfx, rtype_pr32_audio::kBossHitSfxLen);
}

void RTypeScene::updateBossFight(float dt) {
    if (!bossHasSplit) {
        if (boss[0].active) updateBossUnit(boss[0], dt, 1.0f, 1500, true);
    } else {
        for (auto& b : boss) if (b.active) updateBossUnit(b, dt, 1.6f, 1100, false);
    }

    if (bossSettled && millis() > bossSettleToggleMs) { bossSettled = false; bossSettleToggleMs = millis() + 2600; }
    if (!bossSettled && !bossHasSplit && millis() > bossSettleToggleMs) { bossSettled = true; bossSettleToggleMs = millis() + 2200; }

    updateStars(dt);
    updateEBullets(dt);
    updatePBullets(dt);
    updateParticles(dt);

    for (auto& pb : pbullets) {
        if (!pb.active) continue;
        for (auto& b : boss) {
            if (!b.active) continue;
            if (overlaps(pb.x - 2, pb.y - 2, 4, 4, b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2)) {
                b.hp--;
                pb.pierce--;
                if (b.hp <= 0) {
                    b.active = false;
                    spawnParticles(b.x, b.y, pr32::graphics::Color::Orange, 16);
                    sfx.play(rtype_pr32_audio::kBossHitSfx, rtype_pr32_audio::kBossHitSfxLen);
                }
                if (pb.pierce <= 0) { pb.active = false; break; }
            }
        }
    }

    if (!bossHasSplit && boss[0].hp > 0 && boss[0].hp <= kBossMainHp / 2) splitBoss();

    if (bossHpRemaining() <= 0) {
        score += 3000;
        saveHighScoreIfNeeded();
        state = GameState::StageClear;
        stageClearAtMs = millis();
        sfx.play(rtype_pr32_audio::kStageClearSfx, rtype_pr32_audio::kStageClearSfxLen);
        return;
    }

    if (invulnerable) {
        if (millis() >= invulnUntilMs) invulnerable = false;
    } else {
        for (auto& b : boss) {
            if (!b.active) continue;
            if (overlaps(shipX, shipY, kShipW, kShipH, b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2)) {
                killShip();
                return;
            }
        }
    }
}

void RTypeScene::drawBoss(pr32::graphics::Renderer& renderer) {
    namespace bspr = rtype_pr32_boss;
    pr32::graphics::setSpriteCustomPalette(bspr::PALETTE_RGB565);
    for (auto& b : boss) {
        if (!b.active) continue;
        bool main = bossUnitIsMain(b);
        const pr32::graphics::Sprite4bpp* frame;
        bool lowHp = b.hp <= b.maxHp / 3;
        if (main) {
            frame = lowHp ? &bspr::MAIN_LOW_HP : (bossAnimFrame ? &bspr::MAIN_IDLE_B : &bspr::MAIN_IDLE_A);
        } else {
            frame = lowHp ? &bspr::SPLIT_LOW_HP : (bossAnimFrame ? &bspr::SPLIT_IDLE_B : &bspr::SPLIT_IDLE_A);
        }
        renderer.drawSprite(*frame, (int)b.x - frame->width / 2, (int)b.y - frame->height / 2, false);
    }
    pr32::graphics::setSpriteCustomPalette(rtype_pr32_sprites::PALETTE_RGB565);
}

void RTypeScene::drawBossHpBar(pr32::graphics::Renderer& renderer) {
    int maxHp = bossHpMax();
    int hp = bossHpRemaining();
    int barW = 140, barH = 6;
    int x = SCREEN_W / 2 - barW / 2, y = HUD_H + 4;
    renderer.drawRectangle(x - 1, y - 1, barW + 2, barH + 2, pr32::graphics::Color::White);
    renderer.drawFilledRectangle(x, y, barW, barH, pr32::graphics::Color::Black);
    int fillW = maxHp > 0 ? (barW * hp) / maxHp : 0;
    if (fillW > 0) renderer.drawFilledRectangle(x, y, fillW, barH, pr32::graphics::Color::Red);
}

// ================================================================ HUD ====
void RTypeScene::drawHUD(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    renderer.drawFilledRectangle(0, 0, SCREEN_W, HUD_H, Color::Black);
    renderer.drawLine(0, HUD_H - 1, SCREEN_W, HUD_H - 1, Color::Gray);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %06d", score);
    renderer.drawText(buf, 2, 4, Color::White, 1);

    static const char* kWeaponNames[3] = { "NORMAL", "SPREAD", "LASER" };
    snprintf(buf, sizeof(buf), "WPN:%s", kWeaponNames[weaponType]);
    renderer.drawText(buf, 82, 4, Color::White, 1);

    snprintf(buf, sizeof(buf), "LIVES %d", lives);
    renderer.drawText(buf, 172, 4, Color::White, 1);
}

void RTypeScene::drawPauseOverlay(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    int x = SCREEN_W / 2 - 70, y = SCREEN_H / 2 - 26, w = 140, h = 52;
    renderer.drawFilledRectangle(x, y, w, h, Color::Black);
    renderer.drawRectangle(x, y, w, h, Color::White);
    const char* t1 = "PAUSED";
    renderer.drawText(t1, SCREEN_W / 2 - textWidthEstimate(t1, 2) / 2, SCREEN_H / 2 - 16, Color::White, 2);
    const char* t2 = "R3:RESUME  A:EXIT";
    renderer.drawText(t2, SCREEN_W / 2 - textWidthEstimate(t2, 1) / 2, SCREEN_H / 2 + 8, Color::White, 1);
}

void RTypeScene::drawGameOverOverlay(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    int x = SCREEN_W / 2 - 90, y = SCREEN_H / 2 - 30, w = 180, h = 60;
    renderer.drawFilledRectangle(x, y, w, h, Color::Black);
    renderer.drawRectangle(x, y, w, h, Color::Red);
    const char* t1 = "GAME OVER";
    renderer.drawText(t1, SCREEN_W / 2 - textWidthEstimate(t1, 2) / 2, SCREEN_H / 2 - 20, Color::Red, 2);
    char buf[28];
    snprintf(buf, sizeof(buf), "SCORE %06d HI %06d", score, highScore);
    renderer.drawText(buf, SCREEN_W / 2 - textWidthEstimate(buf, 1) / 2, SCREEN_H / 2 + 4, Color::White, 1);
    const char* t3 = "R3: RESTART";
    renderer.drawText(t3, SCREEN_W / 2 - textWidthEstimate(t3, 1) / 2, SCREEN_H / 2 + 18, Color::White, 1);
}

void RTypeScene::drawStageClearOverlay(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    int x = SCREEN_W / 2 - 100, y = SCREEN_H / 2 - 26, w = 200, h = 52;
    renderer.drawFilledRectangle(x, y, w, h, Color::Black);
    renderer.drawRectangle(x, y, w, h, Color::Green);
    const char* t1 = "STAGE 1 CLEAR";
    renderer.drawText(t1, SCREEN_W / 2 - textWidthEstimate(t1, 2) / 2, SCREEN_H / 2 - 16, Color::Green, 2);
    const char* t2 = "MORE STAGES COMING SOON";
    renderer.drawText(t2, SCREEN_W / 2 - textWidthEstimate(t2, 1) / 2, SCREEN_H / 2 + 8, Color::White, 1);
}

void RTypeScene::drawBossIntroBanner(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    renderer.drawFilledRectangle(0, PLAY_TOP, SCREEN_W, SCREEN_H - PLAY_TOP, Color::Black);
    const char* t1 = "SENTINEL CORE";
    renderer.drawText(t1, SCREEN_W / 2 - textWidthEstimate(t1, 2) / 2, SCREEN_H / 2 - 10, Color::Red, 2);
    const char* t2 = "STAGE BOSS APPROACHING";
    renderer.drawText(t2, SCREEN_W / 2 - textWidthEstimate(t2, 1) / 2, SCREEN_H / 2 + 18, Color::White, 1);
}

// Loops rtype_pr32_audio::kThemeMusic on the single shared buzzer
// voice, ducking out for one-shot SFX (see the header's comment on why
// — mirrors blockstack_pr32.cpp's updateMusic() exactly).
void RTypeScene::updateMusic() {
    if (!musicShouldPlay) {
        if (music.isPlaying()) music.stop();
        return;
    }
    if (sfx.isPlaying()) return;
    if (!music.isPlaying()) {
        music.play(rtype_pr32_audio::kThemeMusic, rtype_pr32_audio::kThemeMusicLen);
    } else {
        music.update();
    }
}

// ============================================================= UPDATE ====
void RTypeScene::update(unsigned long deltaTime) {
    float dt = deltaTime * 0.001f;
    if (dt > 0.05f) dt = 0.05f; // clamp a stall/hitch so physics doesn't jump

    // sfx was never actually ticked anywhere in this file before — every
    // SFX's FIRST note played, then just held forever (tone() never
    // advanced or stopped) until the next sfx.play() call overwrote it
    // with a different held tone. That's what read live as "the music
    // just changes tones when buttons are pushed" — there was never any
    // real timed playback happening at all. Every other game here calls
    // this every frame; this one simply never did.
    sfx.update();
    musicShouldPlay = (state == GameState::Playing || state == GameState::BossIntro ||
                        state == GameState::BossFight);
    updateMusic();

    if (debugSkipToBossRequested) {
        debugSkipToBossRequested = false;
        for (auto& e : enemies) e.active = false;
        for (auto& d : debris) d.active = false;
        for (auto& b : ebullets) b.active = false;
        // Force a clean, healthy state for the test regardless of what
        // was happening before (including a real GameOver — this is a
        // testing convenience, not meant to preserve a death in
        // progress; a live playtest without moving the ship hit exactly
        // this: real gameplay correctly reached GameOver, but this hook
        // used to stomp state back to BossIntro anyway, leaving a
        // "zombie" HUD showing LIVES 0 while play continued normally).
        lives = 3;
        invulnerable = false;
        state = GameState::BossIntro;
        bossIntroUntilMs = millis() + 200;
    }

    switch (state) {
        case GameState::Playing: {
            updateShipMovement(dt);
            updateFireControl();
            updateR3Control();
            if (state != GameState::Playing) break;

            unsigned long now = millis();
            if (invulnerable && now - lastBlinkMs > 110) { lastBlinkMs = now; shipBlinkOn = !shipBlinkOn; }
            else if (!invulnerable) shipBlinkOn = true;

            worldDistance += kScrollSpeed * dt;
            if (worldDistance >= kStageLength) {
                for (auto& e : enemies) e.active = false;
                for (auto& d : debris) d.active = false;
                for (auto& b : ebullets) b.active = false;
                state = GameState::BossIntro;
                bossIntroUntilMs = millis() + kBossIntroMs;
                break;
            }

            if (now - lastDebrisSpawnMs > 2200) { lastDebrisSpawnMs = now; spawnDebrisWithMaybeTurret(); }
            if (now - lastEnemySpawnMs > 1400) { lastEnemySpawnMs = now; if (rand() % 100 < 70) spawnDrifter(); }

            updateDebrisAndTurrets(dt);
            updateDrifters(dt);
            updateStars(dt);
            updateEBullets(dt);
            updatePBullets(dt);
            updateParticles(dt);
            updateCapsules(dt);
            checkCollisions();
            break;
        }
        case GameState::BossIntro:
            updateR3Control();
            if (state != GameState::BossIntro) break;
            if (millis() >= bossIntroUntilMs) {
                spawnBoss();
                state = GameState::BossFight;
            }
            break;
        case GameState::BossFight: {
            updateShipMovement(dt);
            updateFireControl();
            updateR3Control();
            if (state != GameState::BossFight) break;

            unsigned long now = millis();
            if (invulnerable && now - lastBlinkMs > 110) { lastBlinkMs = now; shipBlinkOn = !shipBlinkOn; }
            else if (!invulnerable) shipBlinkOn = true;

            bossAnimTimer += dt;
            if (bossAnimTimer > 0.3f) { bossAnimTimer = 0.0f; bossAnimFrame = !bossAnimFrame; }

            updateBossFight(dt);
            break;
        }
        case GameState::Paused:
            updateR3Control();
            if (pendingInput.r3Edge) {
                state = stateBeforePause;
                pauseLatched = false;
            } else if (pendingInput.aEdge) {
                returnToMenuRequested = true;
            }
            break;
        case GameState::GameOver:
            if (pendingInput.r3Edge) {
                resetGame();
            }
            break;
        case GameState::StageClear:
            if (millis() - stageClearAtMs >= kStageClearBannerMs) {
                returnToMenuRequested = true;
            }
            break;
    }

    Scene::update(deltaTime);
}

// =============================================================== DRAW ====
void RTypeScene::draw(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    namespace spr = rtype_pr32_sprites;

    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
    drawStars(renderer);

    if (state == GameState::BossIntro) {
        drawBossIntroBanner(renderer);
        drawHUD(renderer);
        return;
    }

    drawDebris(renderer);
    drawEnemies(renderer);
    drawCapsules(renderer);
    drawEBullets(renderer);
    drawPBullets(renderer);
    drawParticles(renderer);

    bool shipVisible = !(invulnerable && !shipBlinkOn);
    if (shipVisible) {
        renderer.drawSprite(spr::RT_SHIP, (int)shipX, (int)shipY, false);
        int midX = (int)shipX + kShipW / 2;
        renderer.drawSprite(spr::RT_DRONE, midX - spr::RT_DRONE.width / 2, (int)shipY + kDroneDockOffsetY, false);
    }

    if (state == GameState::BossFight) {
        drawBoss(renderer);
        drawBossHpBar(renderer);
    }

    drawHUD(renderer);

    switch (state) {
        case GameState::Paused: drawPauseOverlay(renderer); break;
        case GameState::GameOver: drawGameOverOverlay(renderer); break;
        case GameState::StageClear: drawStageClearOverlay(renderer); break;
        default: break;
    }
}

void RTypeScene::debugSkipToBoss() {
    debugSkipToBossRequested = true;
}

} // namespace rtype_pr32
