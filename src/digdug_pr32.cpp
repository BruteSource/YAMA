#include "digdug_pr32.h"
#include "digdug_pr32_sprites.h"
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace pr32 = pixelroot32;

namespace digdug_pr32 {

namespace {
Preferences prefs;

bool overlapsCircle(float ax, float ay, float ar, float bx, float by, float br) {
    float dx = ax - bx, dy = ay - by;
    float rr = ar + br;
    return (dx * dx + dy * dy) < (rr * rr);
}

int textWidthEstimate(const char* s, int size) { return static_cast<int>(strlen(s)) * 6 * size; }
} // namespace

void DigDugScene::loadHighScore() {
    prefs.begin("digdug", true);
    highScore = prefs.getInt("hi", 0);
    prefs.end();
}
void DigDugScene::saveHighScoreIfNeeded() {
    if (score > highScore) {
        highScore = score;
        prefs.begin("digdug", false);
        prefs.putInt("hi", highScore);
        prefs.end();
    }
}

void DigDugScene::init() {
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);
    pr32::graphics::setSpriteCustomPalette(digdug_pr32_sprites::PALETTE_RGB565);

    loadHighScore();
    resetGame();
}

void DigDugScene::resetGame() {
    score = 0;
    lives = 3;
    round = 1;
    startRound();
}

void DigDugScene::startRound() {
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
            dirt[r][c] = (r == 0) ? 0 : 1; // top row is open "surface"

    for (auto& rk : rocks) rk.active = false;
    for (auto& e : enemies) e.active = false;
    for (auto& p : particles) p.active = false;
    pumpTargetIdx = -1;
    pumpBeamTiles = 0;
    prevAHeld = false;

    playerX = colToPixelX(kCols / 2) + (kTileSize - kPlayerSize) / 2.0f;
    playerY = rowToPixelY(0) + (kTileSize - kPlayerSize) / 2.0f;
    playerFacing = Dir::Down;
    invulnerable = false;

    spawnRocks();
    spawnEnemy(EnemyType::Grub, 5, 3);
    spawnEnemy(EnemyType::Grub, 8, 11);
    spawnEnemy(EnemyType::Drake, 12, 6);
    if (round >= 2) spawnEnemy(EnemyType::Grub, 6, 8);
    if (round >= 3) spawnEnemy(EnemyType::Drake, 14, 2);

    state = GameState::Playing;
}

// ============================================================= TERRAIN ====
bool DigDugScene::isDirt(int row, int col) const {
    if (!inBounds(row, col)) return true; // out of bounds = solid
    return dirt[row][col] != 0;
}

void DigDugScene::digAt(int row, int col) {
    if (!inBounds(row, col)) return;
    if (dirt[row][col] != 0) {
        dirt[row][col] = 0;
        sfx.play(digdug_pr32_audio::kDigSfx, digdug_pr32_audio::kDigSfxLen);
    }
}

void DigDugScene::drawTerrain(pr32::graphics::Renderer& renderer) {
    namespace spr = digdug_pr32_sprites;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            if (dirt[r][c] == 0) continue;
            const pr32::graphics::Sprite4bpp& s = (r < kRows / 2) ? spr::DD_DIRT_A : spr::DD_DIRT_B;
            renderer.drawSprite(s, colToPixelX(c), rowToPixelY(r), false);
        }
    }
}

// =============================================================== ROCKS ====
void DigDugScene::spawnRocks() {
    struct { int row, col; } spots[kMaxRocks] = {
        {4, 2}, {4, 12}, {8, 5}, {8, 9}, {13, 3}, {13, 11},
    };
    for (int i = 0; i < kMaxRocks; ++i) {
        rocks[i] = { true, spots[i].col, spots[i].row, RockState::Embedded, millis() };
    }
}

bool DigDugScene::rockAt(int row, int col, int exceptIdx) const {
    for (int i = 0; i < kMaxRocks; ++i) {
        if (i == exceptIdx) continue;
        if (rocks[i].active && rocks[i].row == row && rocks[i].col == col) return true;
    }
    return false;
}

void DigDugScene::crushAt(int row, int col) {
    // Anything (player or enemy) whose current tile matches gets crushed.
    if (pixelToRow(playerCenterY()) == row && pixelToCol(playerCenterX()) == col) {
        killPlayer();
    }
    for (auto& e : enemies) {
        if (!e.active) continue;
        if (pixelToRow(e.y) == row && pixelToCol(e.x) == col) {
            score += (e.type == EnemyType::Grub) ? 100 : 150;
            spawnParticle(e.x, e.y);
            e.active = false;
        }
    }
}

void DigDugScene::updateRocks(float dt) {
    unsigned long now = millis();
    for (int i = 0; i < kMaxRocks; ++i) {
        Rock& rk = rocks[i];
        if (!rk.active) continue;
        switch (rk.state) {
            case RockState::Embedded: {
                bool supported = isDirt(rk.row + 1, rk.col) || rockAt(rk.row + 1, rk.col, i) ||
                                  !inBounds(rk.row + 1, rk.col);
                if (!supported) {
                    rk.state = RockState::Shaking;
                    rk.stateSinceMs = now;
                    sfx.play(digdug_pr32_audio::kRockShakeSfx, digdug_pr32_audio::kRockShakeSfxLen);
                }
                break;
            }
            case RockState::Shaking:
                if (now - rk.stateSinceMs >= kRockShakeMs) {
                    rk.state = RockState::Falling;
                    rk.stateSinceMs = now;
                }
                break;
            case RockState::Falling:
                if (now - rk.stateSinceMs >= kRockFallStepMs) {
                    rk.stateSinceMs = now;
                    int nextRow = rk.row + 1;
                    bool blocked = !inBounds(nextRow, rk.col) || isDirt(nextRow, rk.col) ||
                                    rockAt(nextRow, rk.col, i);
                    if (blocked) {
                        rk.state = RockState::Landed;
                        sfx.play(digdug_pr32_audio::kRockCrushSfx, digdug_pr32_audio::kRockCrushSfxLen);
                    } else {
                        rk.row = nextRow;
                        crushAt(rk.row, rk.col);
                    }
                }
                break;
            case RockState::Landed:
                break;
        }
    }
}

void DigDugScene::drawRocks(pr32::graphics::Renderer& renderer) {
    namespace spr = digdug_pr32_sprites;
    unsigned long now = millis();
    for (auto& rk : rocks) {
        if (!rk.active) continue;
        int x = colToPixelX(rk.col) + 1, y = rowToPixelY(rk.row) + 1;
        if (rk.state == RockState::Shaking) {
            int jitter = ((now / 40) % 2 == 0) ? -1 : 1;
            x += jitter;
        }
        renderer.drawSprite(spr::DD_ROCK, x, y, false);
    }
}

// ============================================================== PLAYER ====
void DigDugScene::updatePlayerMovement(float dt) {
    bool up = pendingInput.lbHeld || pendingInput.thumbY > 0.35f;
    bool down = pendingInput.rbHeld || pendingInput.thumbY < -0.35f;
    bool left = pendingInput.encDelta < 0 || pendingInput.thumbX < -0.35f;
    bool right = pendingInput.encDelta > 0 || pendingInput.thumbX > 0.35f;

    // Only one of the 4 directions moves per frame (grid-feel movement) —
    // vertical takes priority when both an up/down and left/right signal
    // land in the same frame, an arbitrary but deterministic tie-break.
    float dx = 0, dy = 0;
    if (up) { dy = -1; playerFacing = Dir::Up; }
    else if (down) { dy = 1; playerFacing = Dir::Down; }
    else if (left) { dx = -1; playerFacing = Dir::Left; }
    else if (right) { dx = 1; playerFacing = Dir::Right; }

    if (dx != 0 || dy != 0) {
        float oldX = playerX, oldY = playerY;
        playerX += dx * kPlayerSpeed * dt;
        playerY += dy * kPlayerSpeed * dt;
        if (playerX < 0) playerX = 0;
        if (playerX > SCREEN_W - kPlayerSize) playerX = SCREEN_W - kPlayerSize;
        if (playerY < PLAY_TOP) playerY = PLAY_TOP;
        if (playerY > SCREEN_H - kPlayerSize) playerY = SCREEN_H - kPlayerSize;

        int row = pixelToRow(playerCenterY()), col = pixelToCol(playerCenterX());
        if (rockAt(row, col)) {
            playerX = oldX;
            playerY = oldY;
        }
    }

    int row = pixelToRow(playerCenterY()), col = pixelToCol(playerCenterX());
    digAt(row, col);
}

void DigDugScene::killPlayer() {
    if (invulnerable) return;
    lives--;
    sfx.play(digdug_pr32_audio::kPlayerHitSfx, digdug_pr32_audio::kPlayerHitSfxLen);
    if (lives <= 0) {
        state = GameState::GameOver;
        saveHighScoreIfNeeded();
        sfx.play(digdug_pr32_audio::kGameOverSfx, digdug_pr32_audio::kGameOverSfxLen);
    } else {
        invulnerable = true;
        invulnUntilMs = millis() + 2000;
        playerX = colToPixelX(kCols / 2) + (kTileSize - kPlayerSize) / 2.0f;
        playerY = rowToPixelY(0) + (kTileSize - kPlayerSize) / 2.0f;
    }
}

// ================================================================ PUMP ====
void DigDugScene::updatePump(float dt) {
    bool aHeld = pendingInput.aHeld;
    if (aHeld) {
        int prow = pixelToRow(playerCenterY()), pcol = pixelToCol(playerCenterX());
        int dr = 0, dc = 0;
        switch (playerFacing) {
            case Dir::Down: dr = 1; break;
            case Dir::Up: dr = -1; break;
            case Dir::Left: dc = -1; break;
            case Dir::Right: dc = 1; break;
        }
        int foundIdx = -1;
        int reach = 0;
        int row = prow, col = pcol;
        for (int i = 1; i <= kPumpRangeTiles; ++i) {
            row += dr; col += dc;
            if (!inBounds(row, col)) break;
            if (isDirt(row, col)) break;
            if (rockAt(row, col)) break;
            reach = i;
            for (int ei = 0; ei < kMaxEnemies; ++ei) {
                Enemy& e = enemies[ei];
                if (!e.active) continue;
                if (pixelToRow(e.y) == row && pixelToCol(e.x) == col) { foundIdx = ei; break; }
            }
            if (foundIdx >= 0) break;
        }
        pumpBeamTiles = reach;

        if (!prevAHeld) sfx.play(digdug_pr32_audio::kPumpSfx, digdug_pr32_audio::kPumpSfxLen);

        if (foundIdx >= 0) {
            pumpTargetIdx = foundIdx;
            Enemy& e = enemies[foundIdx];
            unsigned long now = millis();
            if (now - e.lastInflateMs >= kInflateStepMs) {
                e.lastInflateMs = now;
                e.inflateStage++;
                if (e.inflateStage >= kMaxInflateStage) {
                    burstEnemy(e);
                    pumpTargetIdx = -1;
                } else {
                    sfx.play(digdug_pr32_audio::kInflateSfx, digdug_pr32_audio::kInflateSfxLen);
                }
            }
        } else {
            pumpTargetIdx = -1;
        }
    } else {
        pumpTargetIdx = -1;
        pumpBeamTiles = 0;
    }
    prevAHeld = aHeld;

    unsigned long now = millis();
    for (int i = 0; i < kMaxEnemies; ++i) {
        Enemy& e = enemies[i];
        if (!e.active || i == pumpTargetIdx) continue;
        if (e.inflateStage > 0 && now - e.lastDeflateMs >= kDeflateStepMs) {
            e.lastDeflateMs = now;
            e.inflateStage--;
        }
    }
}

void DigDugScene::drawPumpBeam(pr32::graphics::Renderer& renderer) {
    if (pumpBeamTiles <= 0) return;
    int dr = 0, dc = 0;
    switch (playerFacing) {
        case Dir::Down: dr = 1; break;
        case Dir::Up: dr = -1; break;
        case Dir::Left: dc = -1; break;
        case Dir::Right: dc = 1; break;
    }
    int x1 = (int)playerCenterX(), y1 = (int)playerCenterY();
    int x2 = x1 + dc * pumpBeamTiles * kTileSize;
    int y2 = y1 + dr * pumpBeamTiles * kTileSize;
    renderer.drawLine(x1, y1, x2, y2, pr32::graphics::Color::Yellow);
}

// ============================================================= ENEMIES ====
void DigDugScene::spawnEnemy(EnemyType type, int row, int col) {
    for (auto& e : enemies) {
        if (!e.active) {
            float cx = colToPixelX(col) + kTileSize / 2.0f;
            float cy = rowToPixelY(row) + kTileSize / 2.0f;
            e = Enemy{};
            e.active = true;
            e.type = type;
            e.mode = EnemyMode::Normal;
            e.x = e.targetX = cx;
            e.y = e.targetY = cy;
            unsigned long now = millis();
            e.lastMoveMs = now;
            // Both must start at "now", not the struct's default 0 — a
            // spawn-time value of 0 makes millis()-lastDeflateMs look like
            // a huge elapsed duration on the very first frame this enemy
            // isn't the pump target, firing an immediate spurious deflate
            // tick instead of waiting the real kDeflateStepMs.
            e.lastInflateMs = now;
            e.lastDeflateMs = now;
            e.modeUntilMs = now + kPhaseCooldownMinMs + (rand() % (kPhaseCooldownMaxMs - kPhaseCooldownMinMs));
            return;
        }
    }
}

int DigDugScene::enemiesRemaining() const {
    int n = 0;
    for (auto& e : enemies) if (e.active) n++;
    return n;
}

void DigDugScene::burstEnemy(Enemy& e) {
    score += (e.type == EnemyType::Grub) ? 200 : 300;
    spawnParticle(e.x, e.y);
    sfx.play(digdug_pr32_audio::kBurstSfx, digdug_pr32_audio::kBurstSfxLen);
    e.active = false;
}

void DigDugScene::updateEnemyNormal(Enemy& e, float dt) {
    // Move toward the current waypoint; pick a new one when arrived.
    float dx = e.targetX - e.x, dy = e.targetY - e.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.5f) {
        int row = pixelToRow(e.y), col = pixelToCol(e.x);
        int prow = pixelToRow(playerCenterY()), pcol = pixelToCol(playerCenterX());
        int rdiff = prow - row, cdiff = pcol - col;
        int dr = 0, dc = 0;
        bool tryVerticalFirst = abs(rdiff) >= abs(cdiff);
        auto tryDir = [&](int trdr, int trdc) -> bool {
            int nrow = row + trdr, ncol = col + trdc;
            if (!inBounds(nrow, ncol)) return false;
            if (rockAt(nrow, ncol)) return false;
            if (isDirt(nrow, ncol)) return false; // Normal mode can't dig
            dr = trdr; dc = trdc;
            return true;
        };
        bool moved = false;
        if (tryVerticalFirst) {
            if (rdiff != 0 && tryDir(rdiff > 0 ? 1 : -1, 0)) moved = true;
            else if (cdiff != 0 && tryDir(0, cdiff > 0 ? 1 : -1)) moved = true;
        } else {
            if (cdiff != 0 && tryDir(0, cdiff > 0 ? 1 : -1)) moved = true;
            else if (rdiff != 0 && tryDir(rdiff > 0 ? 1 : -1, 0)) moved = true;
        }
        if (!moved) {
            // Stuck — try any open neighbor so it doesn't freeze forever.
            static const int nd[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
            for (auto& d : nd) { if (tryDir(d[0], d[1])) { moved = true; break; } }
        }
        if (moved) {
            e.targetX = colToPixelX(col + dc) + kTileSize / 2.0f;
            e.targetY = rowToPixelY(row + dr) + kTileSize / 2.0f;
        }
    } else {
        float step = kEnemySpeed * dt;
        if (step > dist) step = dist;
        e.x += dx / dist * step;
        e.y += dy / dist * step;
    }
}

void DigDugScene::updateEnemyPhase(Enemy& e, float dt) {
    // Phase mode ignores dirt (moves straight through solid ground toward
    // the player) but still can't pass through a rock.
    float dx = e.targetX - e.x, dy = e.targetY - e.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.5f) {
        int row = pixelToRow(e.y), col = pixelToCol(e.x);
        int prow = pixelToRow(playerCenterY()), pcol = pixelToCol(playerCenterX());
        int rdiff = prow - row, cdiff = pcol - col;
        int dr = (rdiff != 0) ? (rdiff > 0 ? 1 : -1) : 0;
        int dc = (rdiff == 0 && cdiff != 0) ? (cdiff > 0 ? 1 : -1) : 0;
        int nrow = row + dr, ncol = col + dc;
        if (inBounds(nrow, ncol) && !rockAt(nrow, ncol)) {
            e.targetX = colToPixelX(ncol) + kTileSize / 2.0f;
            e.targetY = rowToPixelY(nrow) + kTileSize / 2.0f;
        }
    } else {
        float step = kEnemySpeed * 1.3f * dt;
        if (step > dist) step = dist;
        e.x += dx / dist * step;
        e.y += dy / dist * step;
    }
}

void DigDugScene::updateEnemies(float dt) {
    unsigned long now = millis();
    for (auto& e : enemies) {
        if (!e.active) continue;

        if (e.mode == EnemyMode::Normal && now >= e.modeUntilMs) {
            e.mode = EnemyMode::Phase;
            e.modeUntilMs = now + kPhaseDurationMs;
        } else if (e.mode == EnemyMode::Phase && now >= e.modeUntilMs) {
            e.mode = EnemyMode::Normal;
            e.modeUntilMs = now + kPhaseCooldownMinMs + (rand() % (kPhaseCooldownMaxMs - kPhaseCooldownMinMs));
            // Carve out wherever it ended up so it isn't embedded in solid
            // dirt now that it can't phase through it anymore.
            digAt(pixelToRow(e.y), pixelToCol(e.x));
            e.targetX = e.x;
            e.targetY = e.y;
        }

        if (e.mode == EnemyMode::Normal) updateEnemyNormal(e, dt);
        else updateEnemyPhase(e, dt);

        if (e.inflateStage <= 0) {
            float r = 7.0f;
            if (overlapsCircle(e.x, e.y, r, playerCenterX(), playerCenterY(), kPlayerSize / 2.0f)) {
                killPlayer();
            }
        }
    }
}

void DigDugScene::drawEnemies(pr32::graphics::Renderer& renderer) {
    namespace spr = digdug_pr32_sprites;
    unsigned long now = millis();
    for (auto& e : enemies) {
        if (!e.active) continue;
        // Phase mode blinks (every ~120ms) to read as "intangible."
        if (e.mode == EnemyMode::Phase && ((now / 120) % 2 == 0)) continue;

        const pr32::graphics::Sprite4bpp* s;
        if (e.type == EnemyType::Grub) {
            s = e.inflateStage == 0 ? &spr::DD_GRUB_0 : (e.inflateStage == 1 ? &spr::DD_GRUB_1 : &spr::DD_GRUB_2);
        } else {
            s = e.inflateStage == 0 ? &spr::DD_DRAKE_0 : (e.inflateStage == 1 ? &spr::DD_DRAKE_1 : &spr::DD_DRAKE_2);
        }
        renderer.drawSprite(*s, (int)e.x - s->width / 2, (int)e.y - s->height / 2, false);
    }
}

// =========================================================== PARTICLES ====
void DigDugScene::spawnParticle(float x, float y) {
    for (auto& p : particles) {
        if (!p.active) { p = { true, x, y, millis() }; return; }
    }
}

void DigDugScene::updateParticles(float dt) {
    unsigned long now = millis();
    for (auto& p : particles) {
        if (p.active && now - p.bornMs > kParticleLifeMs) p.active = false;
    }
}

void DigDugScene::drawParticles(pr32::graphics::Renderer& renderer) {
    namespace spr = digdug_pr32_sprites;
    for (auto& p : particles) {
        if (!p.active) continue;
        renderer.drawSprite(spr::DD_BURST, (int)p.x - spr::DD_BURST.width / 2, (int)p.y - spr::DD_BURST.height / 2, false);
    }
}

// ======================================================= PAUSE (R3) =======
// Same convention as every other game here: short hold (500ms) pauses,
// continuing to hold past 2000ms total force-returns to the menu
// regardless of pause state — this is also what the test rig's
// open_console() relies on to deterministically land at the menu no
// matter what state a previous connection left the game in (see
// tools/rig.py's hold_latch()/open_console() docstrings).
void DigDugScene::updateR3Control() {
    if (pendingInput.r3Held) {
        if (r3HeldSinceMs == 0) r3HeldSinceMs = millis();
        unsigned long heldMs = millis() - r3HeldSinceMs;
        if (!pauseLatched && heldMs >= kPauseHoldMs && heldMs < kReturnToMenuHoldMs && state == GameState::Playing) {
            pauseLatched = true;
            stateBeforePause = state;
            state = GameState::Paused;
        }
        if (!returnToMenuLatched && heldMs >= kReturnToMenuHoldMs) {
            returnToMenuLatched = true;
            returnToMenuRequested = true;
        }
    } else {
        r3HeldSinceMs = 0;
        pauseLatched = false;
        returnToMenuLatched = false;
    }
    prevR3Held = pendingInput.r3Held;
}

// ================================================================ HUD ======
void DigDugScene::drawHUD(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    renderer.drawFilledRectangle(0, 0, SCREEN_W, HUD_H, Color::Black);
    renderer.drawLine(0, HUD_H - 1, SCREEN_W, HUD_H - 1, Color::Gray);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %06d", score);
    renderer.drawText(buf, 2, 4, Color::White, 1);
    snprintf(buf, sizeof(buf), "RND %d", round);
    renderer.drawText(buf, 112, 4, Color::White, 1);
    snprintf(buf, sizeof(buf), "LIVES %d", lives);
    renderer.drawText(buf, 172, 4, Color::White, 1);
}

void DigDugScene::drawPauseOverlay(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    int x = SCREEN_W / 2 - 70, y = SCREEN_H / 2 - 26, w = 140, h = 52;
    renderer.drawFilledRectangle(x, y, w, h, Color::Black);
    renderer.drawRectangle(x, y, w, h, Color::White);
    const char* t1 = "PAUSED";
    renderer.drawText(t1, SCREEN_W / 2 - textWidthEstimate(t1, 2) / 2, SCREEN_H / 2 - 16, Color::White, 2);
    const char* t2 = "R3:RESUME  A:EXIT";
    renderer.drawText(t2, SCREEN_W / 2 - textWidthEstimate(t2, 1) / 2, SCREEN_H / 2 + 8, Color::White, 1);
}

void DigDugScene::drawGameOverOverlay(pr32::graphics::Renderer& renderer) {
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

void DigDugScene::drawRoundClearOverlay(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    int x = SCREEN_W / 2 - 100, y = SCREEN_H / 2 - 26, w = 200, h = 52;
    renderer.drawFilledRectangle(x, y, w, h, Color::Black);
    renderer.drawRectangle(x, y, w, h, Color::Green);
    char buf[24];
    snprintf(buf, sizeof(buf), "ROUND %d CLEAR", round);
    renderer.drawText(buf, SCREEN_W / 2 - textWidthEstimate(buf, 2) / 2, SCREEN_H / 2 - 16, Color::Green, 2);
    const char* t2 = "NEXT ROUND STARTING...";
    renderer.drawText(t2, SCREEN_W / 2 - textWidthEstimate(t2, 1) / 2, SCREEN_H / 2 + 8, Color::White, 1);
}

// Loops digdug_pr32_audio::kThemeMusic on the single shared buzzer
// voice, ducking out for one-shot SFX (see the header's comment on why
// — mirrors blockstack_pr32.cpp's updateMusic() exactly).
void DigDugScene::updateMusic() {
    if (!musicShouldPlay) {
        if (music.isPlaying()) music.stop();
        return;
    }
    if (sfx.isPlaying()) return;
    if (!music.isPlaying()) {
        music.play(digdug_pr32_audio::kThemeMusic, digdug_pr32_audio::kThemeMusicLen);
    } else {
        music.update();
    }
}

// ============================================================= UPDATE ======
void DigDugScene::update(unsigned long deltaTime) {
    float dt = deltaTime * 0.001f;
    if (dt > 0.05f) dt = 0.05f;

    // sfx was never actually ticked anywhere in this file before — every
    // SFX's FIRST note played, then just held forever (tone() never
    // advanced or stopped) until the next sfx.play() call overwrote it
    // with a different held tone, including the very-frequent per-tile
    // dig tick — reported live as sounding terrible. Every other game
    // here calls this every frame; this one simply never did.
    sfx.update();
    musicShouldPlay = (state == GameState::Playing);
    updateMusic();

    if (debugSpawnEnemyRequested) {
        debugSpawnEnemyRequested = false;
        int prow = pixelToRow(playerCenterY()), pcol = pixelToCol(playerCenterX());
        int row = prow + 2;
        if (row < kRows) {
            // Dig the whole corridor between the player and the spawn
            // point too, not just the spawn tile itself — otherwise the
            // pump beam (which stops at solid dirt) can never reach an
            // enemy spawned 2 tiles below through still-solid ground.
            for (int r = prow; r <= row; ++r) digAt(r, pcol);
            spawnEnemy(EnemyType::Grub, row, pcol);
        }
    }
    if (debugForceRockRequested) {
        debugForceRockRequested = false;
        int prow = pixelToRow(playerCenterY()), pcol = pixelToCol(playerCenterX());
        int bestIdx = -1, bestDist = 999999;
        for (int i = 0; i < kMaxRocks; ++i) {
            if (!rocks[i].active || rocks[i].state != RockState::Embedded) continue;
            int d = abs(rocks[i].row - prow) + abs(rocks[i].col - pcol);
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        if (bestIdx >= 0) {
            digAt(rocks[bestIdx].row + 1, rocks[bestIdx].col);
            rocks[bestIdx].state = RockState::Shaking;
            rocks[bestIdx].stateSinceMs = millis();
        }
    }

    switch (state) {
        case GameState::Playing: {
            updateR3Control();
            if (state != GameState::Playing) break;

            unsigned long now = millis();
            if (invulnerable) {
                if (now - lastBlinkMs > 110) { lastBlinkMs = now; playerBlinkOn = !playerBlinkOn; }
                if (now >= invulnUntilMs) invulnerable = false;
            } else {
                playerBlinkOn = true;
            }

            updatePlayerMovement(dt);
            updatePump(dt);
            updateRocks(dt);
            updateEnemies(dt);
            updateParticles(dt);

            if (state == GameState::Playing && enemiesRemaining() == 0) {
                score += 500;
                saveHighScoreIfNeeded();
                state = GameState::RoundClear;
                roundClearAtMs = millis();
                sfx.play(digdug_pr32_audio::kRoundClearSfx, digdug_pr32_audio::kRoundClearSfxLen);
            }
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
            if (pendingInput.r3Edge) resetGame();
            break;
        case GameState::RoundClear:
            if (millis() - roundClearAtMs >= kRoundClearBannerMs) {
                round++;
                startRound();
            }
            break;
    }

    Scene::update(deltaTime);
}

// =============================================================== DRAW ======
void DigDugScene::draw(pr32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    namespace spr = digdug_pr32_sprites;

    renderer.drawFilledRectangle(0, PLAY_TOP, SCREEN_W, SCREEN_H - PLAY_TOP, Color::Black);
    drawTerrain(renderer);
    drawRocks(renderer);
    drawEnemies(renderer);
    drawPumpBeam(renderer);
    drawParticles(renderer);

    bool playerVisible = !(invulnerable && !playerBlinkOn);
    if (playerVisible) {
        bool flip = playerFacing == Dir::Left;
        renderer.drawSprite(spr::DD_PLAYER, (int)playerX, (int)playerY, flip);
    }

    drawHUD(renderer);

    switch (state) {
        case GameState::Paused: drawPauseOverlay(renderer); break;
        case GameState::GameOver: drawGameOverOverlay(renderer); break;
        case GameState::RoundClear: drawRoundClearOverlay(renderer); break;
        default: break;
    }
}

void DigDugScene::debugSpawnEnemyNearPlayer() { debugSpawnEnemyRequested = true; }
void DigDugScene::debugForceNearestRockDrop() { debugForceRockRequested = true; }

} // namespace digdug_pr32
