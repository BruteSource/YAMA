// Bubble Bobble (NES-port-styled) single-screen platformer. Controls:
// thumbstick X + encoder both walk left/right (additive, like Arkanoid's
// paddle); thumbstick pushed up OR BTN1 jumps (mirrors the original
// arcade's real 4-way-joystick-up-to-jump control); KEY0/BTN2 blow a
// bubble (KEY0 = main action button, matching the original's one-button
// cabinet). ENC_SW hold pauses/resumes; KEY0 only returns to the menu from
// the pause/game-over screen — see bubblebobbleWantsExit().
#include "bubblebobble.h"
#include "hardware.h"
#include "palette.h"
#include "sound.h"
#include "bubblebobble_sprites.h"
#include <Preferences.h>

namespace {

Preferences prefs;

// ================= Grid / level layouts =================
const int COLS = 15;                 // 240 / 16
const int ROWS = 8;
const int TILE_W = 16;
const int ROW_SPACING = 36;
const int PLAYFIELD_TOP = 22;
const int HUD_H = 20;

int rowY(int r) { return PLAYFIELD_TOP + r * ROW_SPACING; }
int colX(int c) { return c * TILE_W; }

// The 3 layouts below are transcribed from the actual level data in
// JulianRijken/BubbleBobble's Assets/Levels.png (GPL-3.0, see
// tools/bb_ref_assets/SOURCE.md) — that file encodes each real level as a
// 32x28-tile color-coded image (decoded this session: blue=solid wall,
// green=one-way platform, per Game::ParseMaps/BlockSolidity in their
// source), one of Game::LEVELS' 3 real levels stacked per 28-row band.
// Reproduced here at this game's coarser 15x8 grid (structure/spirit
// matched, not a pixel-exact copy — the two grids don't share a scale)
// since the real designs are more interesting than anything hand-invented
// last pass: Level 0 is 3 identical wide rows with end stubs and a gap
// before a long center run; Level 1 has 4 differently-sized center-
// weighted platforms; Level 2 was a hollow "room" (its vertical side
// walls aren't reproducible here — this engine only has horizontal
// one-way platform rows — so it's approximated as 3 stacked two-segment
// "frame" rows plus the scattered floor row from the same real level).
const char* LAYOUT_1[ROWS] = {
  "...............",
  "...............",
  "111..11111..111",
  "...............",
  "111..11111..111",
  "...............",
  "111..11111..111",
  ".1111111111111.",
};
const char* LAYOUT_2[ROWS] = {
  "...............",
  "......111......",
  "...............",
  "..111.....111..",
  "...............",
  ".1111111111111.",
  "11..11...11..11",
  ".1111111111111.",
};
const char* LAYOUT_3[ROWS] = {
  "...............",
  "1.1111...1111.1",
  "...............",
  "1.1111...1111.1",
  "...............",
  "1.1111...1111.1",
  "...............",
  "11..11...11..11",
};
const char** LAYOUTS[] = { LAYOUT_1, LAYOUT_2, LAYOUT_3 };
const int NUM_LAYOUTS = 3;
const char** currentLayout = LAYOUT_1;

bool platformSolidAt(int row, float x0, float x1) {
  if (row < 0 || row >= ROWS) return false;
  int c0 = max(0, (int)x0 / TILE_W);
  int c1 = min(COLS - 1, (int)(x1 - 1) / TILE_W);
  for (int c = c0; c <= c1; c++) if (currentLayout[row][c] == '1') return true;
  return false;
}

void drawPlatforms() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (currentLayout[r][c] == '1')
        tft->drawRGBBitmap(colX(c), rowY(r), BB_SPR_PLATFORM, BB_SPR_PLATFORM_W, BB_SPR_PLATFORM_H);
}

// Decorative checkered border frame down the left/right edges of the play
// area — every real level has one; drawn first so a layout's own edge
// platforms (col 0 / col 14) composite on top of it looking integrated.
void drawWalls() {
  for (int y = PLAYFIELD_TOP; y < SCREEN_H; y += BB_SPR_WALL_H) {
    tft->drawRGBBitmap(0, y, BB_SPR_WALL, BB_SPR_WALL_W, BB_SPR_WALL_H);
    tft->drawRGBBitmap(SCREEN_W - BB_SPR_WALL_W, y, BB_SPR_WALL, BB_SPR_WALL_W, BB_SPR_WALL_H);
  }
}

// Erasing a moving entity's old position in THIS game can't just be a
// blind fillRect(BG) the way every other game on this console does it —
// Bubble Bobble has STATIC background art (platforms, the wall border)
// that entities constantly pass in front of, and a flat fill permanently
// wipes any of it that happened to be under the erased rect since nothing
// else ever redraws it on its own. The first fix for that (restoring
// overlapping platform/wall tiles from inside each entity's own erase
// call) traded that bug for a worse one: with several entities moving in
// the same frame, one entity's restore could redraw a tile *on top of*
// another entity that had already drawn there earlier in the same frame
// but wasn't due to move again for a while — a visible stomp, worse the
// more entities are on screen, which is exactly the flicker + slowdown
// just reported (the restore also re-scanned every tile on every single
// erase, which is where the slowdown came from). The actual fix: keep
// erasing cheap (plain BG fill) here, and do ONE unconditional full
// background-then-everything redraw pass per frame instead (redrawWorld(),
// defined after every entity type below) — since that happens once, after
// all erasing for the frame is done, there's no ordering hazard, and it's
// actually less total drawing work than the old per-entity restore.
void eraseRect(int x, int y, int w, int h) { tft->fillRect(x, y, w, h, BG); }

float randomFloat(float lo, float hi) { return lo + (hi - lo) * (random(0, 10001) / 10000.0f); }

// ================= Shared vertical physics (gravity + one-way platforms) =====
// Platforms only ever stop a *falling* entity from above (checked by
// scanning for a row crossed this frame while vy>0); jumping up passes
// straight through them, matching the original's one-way-ledge feel. The
// same landing scan re-confirms "still standing" every subsequent frame
// (gravity nudges vy slightly positive each frame, re-triggering the same
// row's landing check), so no separate isGrounded() is needed.
const float GRAVITY = 600.0f;
const float JUMP_VELOCITY = -230.0f;

void applyVerticalPhysics(float x, int w, float& y, float& vy, bool& onGround, float dt, int h) {
  vy += GRAVITY * dt;
  float footOld = y + h;
  float newY = y + vy * dt;
  float footNew = newY + h;
  onGround = false;

  if (vy > 0) {
    float bestRy = 1e9f;
    bool found = false;
    for (int r = 0; r < ROWS; r++) {
      float ry = (float)rowY(r);
      if (ry > footOld - 0.5f && ry <= footNew + 0.5f && platformSolidAt(r, x, x + w)) {
        if (ry < bestRy) { bestRy = ry; found = true; }
      }
    }
    if (found) {
      newY = bestRy - h;
      vy = 0;
      onGround = true;
    }
  } else if (vy < 0 && newY < PLAYFIELD_TOP) {
    newY = PLAYFIELD_TOP;
    vy = 0;
  }

  // Vertical wraparound: falling fully off the bottom reappears at the top.
  if (newY + h > SCREEN_H + h) {
    newY = PLAYFIELD_TOP - h + 2;
    vy = 0;
  }
  y = newY;
}

// ================= Particles (juice, same pattern as the other games) ======
struct Particle { bool active; float x, y, vx, vy; unsigned long bornMs; uint16_t color; };
const int MAX_PARTICLES = 24;
Particle particles[MAX_PARTICLES];
const unsigned long PARTICLE_LIFE_MS = 300;

void spawnParticles(float x, float y, uint16_t color, int count) {
  for (int n = 0; n < count; n++) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].active) {
        float ang = random(0, 360) * PI / 180.0f;
        float spd = random(25, 80);
        particles[i] = { true, x, y, spd * cosf(ang), spd * sinf(ang), millis(), color };
        break;
      }
    }
  }
}
void updateParticles(float dt) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!particles[i].active) continue;
    Particle& p = particles[i];
    eraseRect((int)p.x, (int)p.y, 2, 2);
    if (now - p.bornMs > PARTICLE_LIFE_MS) { p.active = false; continue; }
    p.x += p.vx * dt; p.y += p.vy * dt;
    if (p.x < 0 || p.x > SCREEN_W || p.y < PLAYFIELD_TOP || p.y > SCREEN_H) { p.active = false; continue; }
    tft->fillRect((int)p.x, (int)p.y, 2, 2, p.color);
  }
}
void resetParticles() { for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false; }

// ================= Player (Bub) =================
const int BUB_W = BB_SPR_BUB_WALK0_R_W, BUB_H = BB_SPR_BUB_WALK0_R_H;
float playerX, playerY, playerVY;
bool onGround;
int facing = 1; // +1 right, -1 left
int walkFrame = 0;
float walkAnimTimer = 0;
float lastDrawnPX = -1000, lastDrawnPY = -1000;
int lastDrawnFacing = 1, lastDrawnPose = 0; // 0=walk0 1=walk1 2=jump
bool thumbJumpLatched = false;
bool invulnerable = false;
unsigned long invulnerableUntil = 0;
bool blinkOn = true;
unsigned long nextBlinkMs = 0;
unsigned long lastShotMs = 0;
const unsigned long SHOT_COOLDOWN_MS = 350;
const float WALK_SPEED = 75.0f;
const float ENC_NUDGE = 3.0f;
const float THUMB_JUMP_TRIGGER = 0.55f;
const float THUMB_JUMP_RELEASE = 0.3f;
const unsigned long INVULN_MS = 1500;

const uint16_t* bubSprite(int pose, int facingDir) {
  bool right = facingDir >= 0;
  switch (pose) {
    case 2: return right ? BB_SPR_BUB_JUMP_R : BB_SPR_BUB_JUMP_L;
    case 1: return right ? BB_SPR_BUB_WALK1_R : BB_SPR_BUB_WALK1_L;
    default: return right ? BB_SPR_BUB_WALK0_R : BB_SPR_BUB_WALK0_L;
  }
}

void erasePlayerAt(float x, float y) {
  eraseRect((int)x - 1, (int)y - 1, BUB_W + 2, BUB_H + 2);
}
void drawPlayer() {
  if (invulnerable && !blinkOn) return;
  tft->drawRGBBitmap((int)playerX, (int)playerY, bubSprite(lastDrawnPose, facing), BUB_W, BUB_H);
}

// ================= Enemies (Zen-Chan) =================
// AI ported from JulianRijken/BubbleBobble's ZenChanBehaviour (GPL-3.0,
// see tools/bb_ref_assets/SOURCE.md): patrol until a wall raycast hits
// (cooldown between turns so it doesn't jitter at exactly the wall),
// periodically re-bias walking direction toward the player even without
// a wall, and specifically climb toward the player when it's directly
// below them and there's a platform within jump reach overhead — not a
// random idle hop. Constants below are the same *seconds* values as the
// reference (their engine's timestep is also real seconds, just a
// different position-unit scale, which doesn't affect timing constants).
struct Enemy {
  bool alive;      // walking around in the world right now
  bool trapped;    // currently held inside a bubble (not drawn/updated here)
  bool angry;      // escaped once already -> faster
  float x, y, vx, vy;
  bool onGround;
  int facing;      // +1 right, -1 left — which way it's currently patrolling
  int walkFrame;
  float animTimer;
  float timeSinceLastTurn;
  float timeSinceLastJump;
  float jumpInterval;
  float timeSinceLastFacePlayer;
  float facePlayerInterval;
  float lastDrawnX, lastDrawnY;
};
const int MAX_ENEMIES = 8;
Enemy enemies[MAX_ENEMIES];
int enemiesRemaining = 0;
const int ZC_W = BB_SPR_ZENCHAN_WALK0_R_W, ZC_H = BB_SPR_ZENCHAN_WALK0_R_H;
float enemySpeedBase = 40.0f;
const float ANGRY_MULT = 1.7f;

void eraseEnemyAt(float x, float y) { eraseRect((int)x - 1, (int)y - 1, ZC_W + 2, ZC_H + 2); }
const uint16_t* zenchanSprite(int walkFrame, int facing) {
  bool right = facing >= 0;
  if (walkFrame) return right ? BB_SPR_ZENCHAN_WALK1_R : BB_SPR_ZENCHAN_WALK1_L;
  return right ? BB_SPR_ZENCHAN_WALK0_R : BB_SPR_ZENCHAN_WALK0_L;
}
void drawEnemy(Enemy& e) {
  tft->drawRGBBitmap((int)e.x, (int)e.y, zenchanSprite(e.walkFrame, e.facing), ZC_W, ZC_H);
}

// ================= Bubbles =================
enum BubbleState { BUB_TRAVEL, BUB_FLOAT };
struct Bubble {
  bool active;
  BubbleState state;
  float x, y, vx, vy;
  unsigned long spawnMs, travelEndMs;
  int trappedEnemyIdx; // -1 = empty
  float lastDrawnX, lastDrawnY;
};
const int MAX_BUBBLES = 6;
Bubble bubbles[MAX_BUBBLES];
const int BUBBLE_W = BB_SPR_BUBBLE_W, BUBBLE_H = BB_SPR_BUBBLE_H;
const float BUBBLE_TRAVEL_SPEED = 95.0f;
const float BUBBLE_RISE_SPEED = -32.0f;
// Timings matched to the reference's CaptureBubble constants: ~1s grace
// before a bubble can be popped (DURATION_BEFORE_FLOATING), 8s total
// before it auto-releases its catch (MAX_TIME_TILL_POP).
const float BUBBLE_TRAVEL_TIME = 0.9f;
const unsigned long BUBBLE_LIFETIME_MS = 8000;
const float CHAIN_RADIUS = 34.0f;

// Erases at max(bubble size, trapped-enemy overlay size) regardless of
// whether this particular bubble is currently carrying anything — the
// enemy overlay sprite is bigger than the bubble sprite, and erasing only
// bubble-sized left a trailing streak of un-erased enemy-sprite pixels
// behind every rising captured bubble.
void eraseBubbleAt(float x, float y) {
  int ew = max(BUBBLE_W, ZC_W), eh = max(BUBBLE_H, ZC_H);
  int ex = (int)x + (BUBBLE_W - ew) / 2 - 1;
  int ey = (int)y + (BUBBLE_H - eh) / 2 - 1;
  eraseRect(ex, ey, ew + 2, eh + 2);
}
void drawBubble(Bubble& b) {
  tft->drawRGBBitmap((int)b.x, (int)b.y, BB_SPR_BUBBLE, BUBBLE_W, BUBBLE_H);
  if (b.trappedEnemyIdx >= 0) {
    int ex = (int)b.x + (BUBBLE_W - ZC_W) / 2;
    int ey = (int)b.y + (BUBBLE_H - ZC_H) / 2;
    tft->drawRGBBitmap(ex, ey, BB_SPR_ZENCHAN_WALK0_R, ZC_W, ZC_H);
  }
}

// ================= Food =================
// Watermelon/Fries + their point values (100/200) are the reference's
// actual two pickup types and Game::PICKUP_VALUES, not invented ones.
struct Food {
  bool active;
  float x, y, vy;
  bool onGround;
  unsigned long spawnMs;
  int spriteIdx; // 0=watermelon(100) 1=fries(200)
  float lastDrawnX, lastDrawnY;
};
const int MAX_FOOD = 4;
Food foods[MAX_FOOD];
const int FOOD_W = BB_SPR_FOOD_WATERMELON_W, FOOD_H = BB_SPR_FOOD_WATERMELON_H;
const unsigned long FOOD_LIFETIME_MS = 10000;
const int FOOD_VALUES[2] = { 100, 200 };

const uint16_t* foodSprite(int idx) { return idx == 0 ? BB_SPR_FOOD_WATERMELON : BB_SPR_FOOD_FRIES; }
void eraseFoodAt(float x, float y) { eraseRect((int)x - 1, (int)y - 1, FOOD_W + 2, FOOD_H + 2); }
void drawFood(Food& f) { tft->drawRGBBitmap((int)f.x, (int)f.y, foodSprite(f.spriteIdx), FOOD_W, FOOD_H); }

void spawnFood(float cx, float cy) {
  for (int i = 0; i < MAX_FOOD; i++) {
    if (!foods[i].active) {
      foods[i] = { true, cx - FOOD_W / 2.0f, cy - FOOD_H / 2.0f, 0, false, millis(), random(0, 2), cx - FOOD_W / 2.0f, cy - FOOD_H / 2.0f };
      return;
    }
  }
}

// ================= Game state =================
enum GameState { PLAYING, PAUSED, LEVEL_CLEARED, GAME_OVER };
GameState state = PLAYING;

int score = 0, lives = 3, level = 1, highScore = 0;
int lastDrawnScore = -1, lastDrawnLives = -1, lastDrawnLevel = -1;
unsigned long levelClearUntil = 0;
unsigned long encSwHeldSinceMs = 0;
bool pauseLatched = false;
bool prevEncSwHeld = false;
const unsigned long PAUSE_HOLD_MS = 500;

void loadHighScore() {
  prefs.begin("bubblebobble", true);
  highScore = prefs.getInt("hi", 0);
  prefs.end();
}
void saveHighScoreIfNeeded() {
  if (score > highScore) {
    highScore = score;
    prefs.begin("bubblebobble", false);
    prefs.putInt("hi", highScore);
    prefs.end();
  }
}

void drawHUD(bool force = false) {
  if (force || score != lastDrawnScore || level != lastDrawnLevel) {
    tft->fillRect(0, 2, 170, 16, BG);
    tft->setTextSize(1);
    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(4, 6);
    tft->printf("SCORE %04d  LV%d", score, level);
    lastDrawnScore = score;
    lastDrawnLevel = level;
  }
  if (force || lives != lastDrawnLives) {
    tft->fillRect(SCREEN_W - 70, 2, 70, 16, BG);
    uint16_t lifeColor = rgb565(90, 200, 70);
    for (int i = 0; i < lives; i++) {
      tft->fillCircle(SCREEN_W - 10 - i * 14, 9, 4, lifeColor);
    }
    lastDrawnLives = lives;
  }
}

// ---- spawning helpers ----
void resetBubbles() { for (int i = 0; i < MAX_BUBBLES; i++) bubbles[i].active = false; }
void resetFood() { for (int i = 0; i < MAX_FOOD; i++) foods[i].active = false; }

void spawnEnemyOnPlatform(int idx, int row, int col) {
  Enemy& e = enemies[idx];
  e.alive = true; e.trapped = false; e.angry = false;
  e.x = colX(col); e.y = rowY(row) - ZC_H;
  e.facing = (random(0, 2) == 0) ? -1 : 1;
  e.vx = e.facing * enemySpeedBase;
  e.vy = 0; e.onGround = true;
  e.walkFrame = 0; e.animTimer = 0;
  e.timeSinceLastTurn = 0;
  e.timeSinceLastJump = 0;
  e.jumpInterval = randomFloat(1.0f, 1.5f);
  e.timeSinceLastFacePlayer = 0;
  e.facePlayerInterval = randomFloat(2.0f, 3.5f);
  e.lastDrawnX = e.x; e.lastDrawnY = e.y;
}

void initLevel() {
  currentLayout = LAYOUTS[(level - 1) % NUM_LAYOUTS];
  enemySpeedBase = min(70.0f, 40.0f + (level - 1) * 3.0f);

  int count = min(3 + (level - 1), MAX_ENEMIES - 1);
  enemiesRemaining = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = false;
  int placed = 0, attempts = 0;
  while (placed < count && attempts < 60) {
    attempts++;
    int row = random(0, ROWS - 1); // keep off the very bottom row for spawn fairness
    int col = random(1, COLS - 1);
    if (currentLayout[row][col] != '1') continue;
    spawnEnemyOnPlatform(placed, row, col);
    placed++;
    enemiesRemaining++;
  }

  resetBubbles();
  resetFood();
  resetParticles();

  playerX = colX(COLS / 2) - BUB_W / 2;
  playerY = rowY(ROWS - 2) - BUB_H;
  playerVY = 0; onGround = true; facing = 1;
  invulnerable = false;
  lastDrawnPX = -1000; lastDrawnPY = -1000;
}

void resetGame() {
  score = 0; lives = 3; level = 1;
  initLevel();
}

void drawPlayfield() {
  tft->fillScreen(BG);
  tft->drawFastHLine(0, HUD_H, SCREEN_W, HUD_LINE);
  drawWalls();
  drawPlatforms();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].alive && !enemies[i].trapped) {
      drawEnemy(enemies[i]);
      enemies[i].lastDrawnX = enemies[i].x;
      enemies[i].lastDrawnY = enemies[i].y;
    }
  }
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (bubbles[i].active) {
      drawBubble(bubbles[i]);
      bubbles[i].lastDrawnX = bubbles[i].x;
      bubbles[i].lastDrawnY = bubbles[i].y;
    }
  }
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foods[i].active) {
      drawFood(foods[i]);
      foods[i].lastDrawnX = foods[i].x;
      foods[i].lastDrawnY = foods[i].y;
    }
  }
  drawPlayer();
  lastDrawnPX = playerX; lastDrawnPY = playerY;
  lastDrawnFacing = facing;
  drawHUD(true);
}

// Called once per frame after every entity has updated its own position
// (and erased its own old spot, if it moved) — draws the static
// background layer and then every currently-visible entity on top of it,
// in one uninterrupted pass. Doing this as a single combined step, after
// all erasing for the frame is already done, is what avoids the
// stomping-on-each-other bug a per-entity "erase old spot, redraw
// whatever background tile that overlapped" approach had (see eraseRect's
// comment above).
void redrawWorld() {
  drawWalls();
  drawPlatforms();
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].alive && !enemies[i].trapped) drawEnemy(enemies[i]);
  }
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (bubbles[i].active) drawBubble(bubbles[i]);
  }
  for (int i = 0; i < MAX_FOOD; i++) {
    if (foods[i].active) drawFood(foods[i]);
  }
  if (!(invulnerable && !blinkOn)) drawPlayer();
}

void drawGameOverScreen() {
  tft->fillScreen(BG);
  drawCentered("GAME OVER", 100, 3, WARN_COLOR);
  char buf[24];
  snprintf(buf, sizeof(buf), "SCORE: %04d", score);
  drawCentered(buf, 150, 2, TEXT_COLOR);
  if (score >= highScore && score > 0) {
    drawCentered("NEW HIGH SCORE!", 180, 1, TITLE_COLOR);
  } else {
    char hs[24];
    snprintf(hs, sizeof(hs), "HIGH SCORE: %04d", highScore);
    drawCentered(hs, 180, 1, TEXT_COLOR);
  }
  drawCentered("PRESS KNOB: PLAY AGAIN", 220, 1, TITLE_COLOR);
  drawCentered("KEY0: MENU", 238, 1, TEXT_COLOR);
}

void drawLevelClearOverlay() {
  drawCentered("LEVEL CLEAR!", 150, 2, TITLE_COLOR);
}

void triggerLevelClear() {
  score += 100;
  drawHUD();
  drawLevelClearOverlay();
  levelClearUntil = millis() + 1500;
  state = LEVEL_CLEARED;
  sfxLevelClear();
}

void loseLife() {
  lives--;
  if (lives <= 0) {
    saveHighScoreIfNeeded();
    state = GAME_OVER;
    drawGameOverScreen();
    sfxGameOver();
  } else {
    drawHUD();
    playerX = colX(COLS / 2) - BUB_W / 2;
    playerY = rowY(ROWS - 2) - BUB_H;
    playerVY = 0; onGround = true;
    invulnerable = true;
    invulnerableUntil = millis() + INVULN_MS;
    nextBlinkMs = millis();
    lastDrawnPX = -1000; lastDrawnPY = -1000;
    sfxBad();
  }
}

// ---- bubble pop / chain / enemy escape ----
void destroyTrappedEnemy(int enemyIdx, float atX, float atY) {
  enemies[enemyIdx].alive = false;
  enemies[enemyIdx].trapped = false;
  enemiesRemaining--;
  score += 100;
  drawHUD();
  spawnFood(atX, atY);
  spawnParticles(atX, atY, rgb565(245, 240, 225), 8);
  sfxBreak();
  if (enemiesRemaining <= 0) triggerLevelClear();
}

void popBubble(int idx, bool chainSource) {
  Bubble& b = bubbles[idx];
  if (!b.active) return;
  float cx = b.x + BUBBLE_W / 2.0f, cy = b.y + BUBBLE_H / 2.0f;
  eraseBubbleAt(b.lastDrawnX, b.lastDrawnY);
  bool hadEnemy = b.trappedEnemyIdx >= 0;
  b.active = false;
  if (hadEnemy) {
    destroyTrappedEnemy(b.trappedEnemyIdx, cx, cy);
    // Chain: pop any other trapped bubble close enough, once.
    if (!chainSource) {
      for (int i = 0; i < MAX_BUBBLES; i++) {
        if (i == idx || !bubbles[i].active || bubbles[i].trappedEnemyIdx < 0) continue;
        float ox = bubbles[i].x + BUBBLE_W / 2.0f, oy = bubbles[i].y + BUBBLE_H / 2.0f;
        if (fabsf(ox - cx) <= CHAIN_RADIUS && fabsf(oy - cy) <= CHAIN_RADIUS) {
          popBubble(i, true);
        }
      }
    }
  } else {
    spawnParticles(cx, cy, rgb565(150, 220, 235), 4);
  }
}

void spawnBubble() {
  for (int i = 0; i < MAX_BUBBLES; i++) {
    if (!bubbles[i].active) {
      Bubble& b = bubbles[i];
      b.active = true;
      b.state = BUB_TRAVEL;
      b.x = playerX + (facing > 0 ? BUB_W : -BUBBLE_W);
      b.y = playerY + (BUB_H - BUBBLE_H) / 2.0f;
      b.vx = facing * BUBBLE_TRAVEL_SPEED;
      b.vy = -14.0f;
      b.spawnMs = millis();
      b.travelEndMs = b.spawnMs + (unsigned long)(BUBBLE_TRAVEL_TIME * 1000);
      b.trappedEnemyIdx = -1;
      b.lastDrawnX = b.x; b.lastDrawnY = b.y;
      sfxUiMove();
      return;
    }
  }
}

bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// ================= Per-frame updates =================
void updatePlayer(const InputState& in, float dt) {
  playerX += in.thumbX * WALK_SPEED * dt;
  if (in.encDelta != 0) playerX += in.encDelta * ENC_NUDGE;
  if (fabsf(in.thumbX) > 0.15f) facing = in.thumbX > 0 ? 1 : -1;
  else if (in.encDelta != 0) facing = in.encDelta > 0 ? 1 : -1;
  playerX = constrain(playerX, 0.0f, (float)(SCREEN_W - BUB_W));

  if (fabsf(in.thumbY) < THUMB_JUMP_RELEASE) thumbJumpLatched = false;
  bool jumpTrigger = in.btn1Edge;
  if (!thumbJumpLatched && in.thumbY > THUMB_JUMP_TRIGGER) { thumbJumpLatched = true; jumpTrigger = true; }
  if (jumpTrigger && onGround) {
    playerVY = JUMP_VELOCITY;
    onGround = false;
    sfxHit();
  }

  applyVerticalPhysics(playerX, BUB_W, playerY, playerVY, onGround, dt, BUB_H);

  bool moving = fabsf(in.thumbX) > 0.1f || in.encDelta != 0;
  int pose;
  if (!onGround) pose = 2;
  else if (moving) {
    walkAnimTimer += dt;
    if (walkAnimTimer > 0.15f) { walkAnimTimer = 0; walkFrame ^= 1; }
    pose = walkFrame;
  } else pose = 0;
  lastDrawnPose = pose;

  if (invulnerable) {
    if (millis() >= invulnerableUntil) invulnerable = false;
    else if (millis() >= nextBlinkMs) { blinkOn = !blinkOn; nextBlinkMs = millis() + 100; }
  } else blinkOn = true;

  if ((in.key0Edge || in.btn2Edge) && millis() - lastShotMs >= SHOT_COOLDOWN_MS) {
    lastShotMs = millis();
    spawnBubble();
  }

  // Only erase the old spot here — redrawWorld() (called once per frame,
  // after every entity has had its turn) does the actual drawing, so one
  // entity's redraw can never stomp on another's before it gets its turn.
  bool visible = !(invulnerable && !blinkOn);
  static bool prevVisible = true;
  bool moved = fabsf(playerX - lastDrawnPX) >= 1.0f || fabsf(playerY - lastDrawnPY) >= 1.0f || facing != lastDrawnFacing;
  if (moved || visible != prevVisible) {
    erasePlayerAt(lastDrawnPX, lastDrawnPY);
  }
  lastDrawnPX = playerX; lastDrawnPY = playerY; lastDrawnFacing = facing;
  prevVisible = visible;
}

// One tier up from the enemy's current row, or -1 if the enemy isn't
// resting neatly on a row right now — used by the climb-toward-player
// check below, standing in for the reference's upward raycast within
// CLIMB_HEIGHT (our platform tiers are evenly spaced, so "one row up" is
// always exactly one jump's reach, same as CLIMB_HEIGHT was tuned for).
int rowAbove(float footY) {
  float rel = (footY - PLAYFIELD_TOP) / (float)ROW_SPACING;
  int r = (int)(rel + 0.5f);
  if (fabsf((float)rowY(r) - footY) > 2.0f) return -1;
  return r - 1;
}

void updateEnemies(float dt) {
  const float MIN_TIME_BETWEEN_WALL_TURN = 1.0f;
  const float FACE_PLAYER_MIN_DIST = 15.0f;

  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy& e = enemies[i];
    if (!e.alive || e.trapped) continue;

    float speed = enemySpeedBase * (e.angry ? ANGRY_MULT : 1.0f);
    e.timeSinceLastTurn += dt;
    e.timeSinceLastJump += dt;
    e.timeSinceLastFacePlayer += dt;

    float dx = playerX - e.x;
    float dy = playerY - e.y;
    bool walkingTowardPlayer = (e.facing > 0) == (dx > 0);

    // Turn around at a solid screen wall (cooldown avoids jitter right at
    // the wall) — same as a normal platformer enemy: it still walks
    // straight off the end of its own platform and falls to whatever is
    // below rather than being confined to one shelf.
    bool atWall = (e.facing > 0 && e.x + ZC_W >= SCREEN_W - 1) ||
                  (e.facing < 0 && e.x <= 1);
    if (atWall && e.timeSinceLastTurn >= MIN_TIME_BETWEEN_WALL_TURN) {
      e.facing = -e.facing;
      e.timeSinceLastTurn = 0;
      walkingTowardPlayer = !walkingTowardPlayer;
    }

    // Periodically re-bias toward the player even without hitting a wall
    // (ported from ZenChanBehaviour's "face player" timer) — keeps
    // patrols from reading as fully aimless without making them a direct
    // chase.
    if (e.timeSinceLastFacePlayer > e.facePlayerInterval && fabsf(dx) > FACE_PLAYER_MIN_DIST) {
      if (!walkingTowardPlayer) {
        e.facing = -e.facing;
        walkingTowardPlayer = true;
      }
      e.timeSinceLastFacePlayer = 0;
      e.facePlayerInterval = randomFloat(2.0f, 3.5f);
    }

    e.vx = e.facing * speed;
    float newX = e.x + e.vx * dt;
    newX = constrain(newX, 0.0f, (float)(SCREEN_W - ZC_W));
    e.x = newX;

    // Climb toward the player when directly below them with a platform
    // in reach overhead — purposeful, not a random idle hop (ported from
    // ZenChanBehaviour's jump condition).
    bool playerAbove = dy < -10.0f;
    bool platformAbove = e.onGround && rowAbove(e.y + ZC_H) >= 0 &&
                          platformSolidAt(rowAbove(e.y + ZC_H), e.x, e.x + ZC_W);
    if (e.onGround && playerAbove && platformAbove && walkingTowardPlayer &&
        e.timeSinceLastJump > e.jumpInterval) {
      e.vy = JUMP_VELOCITY;
      e.onGround = false;
      e.timeSinceLastJump = 0;
      e.jumpInterval = randomFloat(1.0f, 1.5f);
    }
    applyVerticalPhysics(e.x, ZC_W, e.y, e.vy, e.onGround, dt, ZC_H);

    e.animTimer += dt;
    if (e.animTimer > 0.12f) { e.animTimer = 0; e.walkFrame ^= 1; }

    if (fabsf(e.x - e.lastDrawnX) >= 1.0f || fabsf(e.y - e.lastDrawnY) >= 1.0f) {
      eraseEnemyAt(e.lastDrawnX, e.lastDrawnY);
    }
    e.lastDrawnX = e.x; e.lastDrawnY = e.y;

    if (!invulnerable && aabbOverlap(playerX, playerY, BUB_W, BUB_H, e.x, e.y, ZC_W, ZC_H)) {
      loseLife();
      return;
    }
  }
}

void updateBubbles(float dt) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_BUBBLES; i++) {
    Bubble& b = bubbles[i];
    if (!b.active) continue;

    if (b.state == BUB_TRAVEL && now >= b.travelEndMs) {
      b.state = BUB_FLOAT;
      b.vx = 0;
      b.vy = BUBBLE_RISE_SPEED;
    }

    b.x += b.vx * dt;
    b.y += b.vy * dt;
    b.x = constrain(b.x, 0.0f, (float)(SCREEN_W - BUBBLE_W));
    if (b.y < PLAYFIELD_TOP) { b.y = PLAYFIELD_TOP; b.vy = 0; }
    if (fabsf(b.x - b.lastDrawnX) >= 1.0f || fabsf(b.y - b.lastDrawnY) >= 1.0f) {
      eraseBubbleAt(b.lastDrawnX, b.lastDrawnY);
    }

    // Empty bubbles traveling/floating can catch an untrapped enemy.
    if (b.trappedEnemyIdx < 0) {
      for (int j = 0; j < MAX_ENEMIES; j++) {
        Enemy& e = enemies[j];
        if (!e.alive || e.trapped) continue;
        if (aabbOverlap(b.x, b.y, BUBBLE_W, BUBBLE_H, e.x, e.y, ZC_W, ZC_H)) {
          eraseEnemyAt(e.lastDrawnX, e.lastDrawnY);
          e.trapped = true;
          b.trappedEnemyIdx = j;
          b.vx = 0; b.vy = BUBBLE_RISE_SPEED;
          b.state = BUB_FLOAT;
          sfxHit();
          break;
        }
      }
    }

    if (now - b.spawnMs >= BUBBLE_LIFETIME_MS) {
      if (b.trappedEnemyIdx >= 0) {
        // Not popped in time -> the trapped monster escapes, angrier.
        Enemy& e = enemies[b.trappedEnemyIdx];
        e.trapped = false;
        e.angry = true;
        e.x = b.x + (BUBBLE_W - ZC_W) / 2.0f;
        e.y = b.y + (BUBBLE_H - ZC_H) / 2.0f;
        e.facing = (random(0, 2) == 0) ? -1 : 1;
        e.vx = e.facing * enemySpeedBase * ANGRY_MULT;
        e.vy = 0; e.onGround = false;
        e.lastDrawnX = e.x; e.lastDrawnY = e.y;
        sfxBad();
      }
      b.active = false;
      continue;
    }

    // Player walking/jumping into any bubble pops it — but not during the
    // initial travel arc (matches the reference's ~1s grace period right
    // after firing, before a capture bubble becomes poppable/floating).
    if (b.state == BUB_FLOAT &&
        aabbOverlap(playerX, playerY, BUB_W, BUB_H, b.x, b.y, BUBBLE_W, BUBBLE_H)) {
      popBubble(i, false);
      continue;
    }

    b.lastDrawnX = b.x; b.lastDrawnY = b.y;
  }
}

void updateFood(float dt) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_FOOD; i++) {
    Food& f = foods[i];
    if (!f.active) continue;

    if (now - f.spawnMs >= FOOD_LIFETIME_MS) {
      eraseFoodAt(f.lastDrawnX, f.lastDrawnY);
      f.active = false;
      continue;
    }

    applyVerticalPhysics(f.x, FOOD_W, f.y, f.vy, f.onGround, dt, FOOD_H);

    if (aabbOverlap(playerX, playerY, BUB_W, BUB_H, f.x, f.y, FOOD_W, FOOD_H)) {
      eraseFoodAt(f.lastDrawnX, f.lastDrawnY);
      f.active = false;
      score += FOOD_VALUES[f.spriteIdx];
      drawHUD();
      spawnParticles(f.x + FOOD_W / 2.0f, f.y + FOOD_H / 2.0f, rgb565(255, 210, 80), 5);
      sfxPowerup();
      continue;
    }

    if (fabsf(f.x - f.lastDrawnX) >= 1.0f || fabsf(f.y - f.lastDrawnY) >= 1.0f) {
      eraseFoodAt(f.lastDrawnX, f.lastDrawnY);
    }
    f.lastDrawnX = f.x; f.lastDrawnY = f.y;
  }
}

} // namespace

void bubblebobbleInit() {
  loadHighScore();
  randomSeed(micros());
  resetGame();
  state = PLAYING;
  drawPlayfield();
  Serial.printf("Bubble Bobble ready. High score: %d\n", highScore);
}

void bubblebobbleUpdate(const InputState& in, float dt) {
  switch (state) {
    case PLAYING:
      updatePlayer(in, dt);
      if (state == PLAYING) updateBubbles(dt);
      if (state == PLAYING) updateEnemies(dt);
      if (state == PLAYING) updateFood(dt);
      if (state == PLAYING) redrawWorld();
      if (state == PLAYING) updateParticles(dt);

      if (in.encSwHeld && !prevEncSwHeld) { encSwHeldSinceMs = millis(); pauseLatched = false; }
      if (state == PLAYING && in.encSwHeld && !pauseLatched && millis() - encSwHeldSinceMs >= PAUSE_HOLD_MS) {
        pauseLatched = true;
        state = PAUSED;
        drawCentered("PAUSED", 150, 2, TITLE_COLOR);
        drawCentered("ENC:RESUME  KEY0:EXIT", 180, 1, TEXT_COLOR);
        sfxUiMove();
      }
      prevEncSwHeld = in.encSwHeld;
      break;

    case PAUSED:
      prevEncSwHeld = in.encSwHeld;
      if (in.encSwEdge) {
        state = PLAYING;
        drawPlayfield();
        sfxUiMove();
      }
      break;

    case LEVEL_CLEARED:
      if (millis() >= levelClearUntil) {
        level++;
        initLevel();
        state = PLAYING;
        drawPlayfield();
        Serial.printf("Level %d\n", level);
      }
      break;

    case GAME_OVER:
      if (in.encSwEdge) {
        resetGame();
        state = PLAYING;
        drawPlayfield();
        sfxUiConfirm();
      }
      break;
  }
}

void bubblebobbleOnExit() {
  saveHighScoreIfNeeded();
}

bool bubblebobbleWantsExit(const InputState& in) {
  return (state == PAUSED || state == GAME_OVER) && in.key0Edge;
}

void bubblebobbleRenderSnapshot() {
  switch (state) {
    case GAME_OVER:
      drawGameOverScreen();
      break;
    case LEVEL_CLEARED:
      drawPlayfield();
      drawLevelClearOverlay();
      break;
    case PAUSED:
      drawPlayfield();
      drawCentered("PAUSED", 150, 2, TITLE_COLOR);
      drawCentered("ENC:RESUME  KEY0:EXIT", 180, 1, TEXT_COLOR);
      break;
    default:
      drawPlayfield();
      break;
  }
}
