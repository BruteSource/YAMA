// Arkanoid-style breakout. Controls: encoder AND thumbstick X both move
// the paddle (additive), BTN1/BTN2 = digital paddle nudge, KEY0 = launch
// ball (main action button), ENC_SW tap = pause/resume/restart. KEY0 only
// returns to the menu from the pause screen — see arkanoidWantsExit().
#include "arkanoid.h"
#include "hardware.h"
#include "palette.h"
#include "sound.h"
#include "arkanoid_sprites.h"
#include <Preferences.h>

namespace {

Preferences prefs;

// ---- Palette (Arkanoid-specific; BG/TEXT_COLOR/TITLE_COLOR/HUD_LINE shared) ----
// Most of the paddle/ball/brick/powerup look now comes from bitmap sprites
// (arkanoid_sprites.h) rather than these — the ones left here are still
// used for particle-effect tinting and the paddle-catch flash, which stay
// simple flat-color "juice" rather than sprites.
uint16_t PADDLE_FLASH;
uint16_t STEEL_COLOR, SILVER_FULL;
uint16_t rowColors[6];

void initGamePalette() {
  PADDLE_FLASH = rgb565(255, 255, 255);
  STEEL_COLOR    = rgb565(70, 75, 90);
  SILVER_FULL    = rgb565(205, 210, 220);

  rowColors[0] = rgb565(255, 70, 90);   // red
  rowColors[1] = rgb565(255, 150, 50);  // orange
  rowColors[2] = rgb565(255, 225, 60);  // yellow
  rowColors[3] = rgb565(100, 220, 100); // green
  rowColors[4] = rgb565(70, 170, 255);  // blue
  rowColors[5] = rgb565(210, 100, 240); // purple
}

// ================= Bricks =================
const int BRICK_ROWS = 6;
const int BRICK_COLS = 8;
const int BRICK_W = 28, BRICK_H = 14;
const int BRICK_GAP_X = 2, BRICK_GAP_Y = 3;
const int BRICK_LEFT = (SCREEN_W - (BRICK_COLS * BRICK_W + (BRICK_COLS - 1) * BRICK_GAP_X)) / 2;
const int BRICK_TOP = 30;

enum BrickType { BRICK_NORMAL, BRICK_TOUGH, BRICK_STEEL };
struct Brick { bool alive; BrickType type; int hp; };
Brick bricks[BRICK_ROWS][BRICK_COLS];
int bricksRemaining = 0;

int brickX(int c) { return BRICK_LEFT + c * (BRICK_W + BRICK_GAP_X); }
int brickY(int r) { return BRICK_TOP + r * (BRICK_H + BRICK_GAP_Y); }

// Fixed level layouts. '.'=empty '1'=normal '2'=tough(2hp) '#'=steel(indestructible)
const char* LAYOUT_FULL[BRICK_ROWS] = {
  "11111111", "11111111", "11111111", "11111111", "11111111", "11111111"
};
const char* LAYOUT_CHECKER[BRICK_ROWS] = {
  "1.1.1.1.", ".1.1.1.1", "1.1.1.1.", ".1.1.1.1", "1.1.1.1.", ".1.1.1.1"
};
const char* LAYOUT_DIAMOND[BRICK_ROWS] = {
  "...11...", "..1111..", ".111111.", ".222222.", "..2222..", "...22..."
};
const char* LAYOUT_FRAME[BRICK_ROWS] = {
  "22222222", "2......2", "2......2", "2......2", "2......2", "22222222"
};
const char* LAYOUT_PILLARS[BRICK_ROWS] = {
  "11.##.11", "11.##.11", "11.##.11", "11.##.11", "11.##.11", "11.##.11"
};
const char** LAYOUTS[] = { LAYOUT_FULL, LAYOUT_CHECKER, LAYOUT_DIAMOND, LAYOUT_FRAME, LAYOUT_PILLARS };
const int NUM_LAYOUTS = 5;

// Row-index -> bitmap, matching rowColors' order (index 0 = top row).
const uint16_t* const BRICK_SPRITES[6] = {
  AK_SPR_BRICK_RED, AK_SPR_BRICK_ORANGE, AK_SPR_BRICK_YELLOW,
  AK_SPR_BRICK_GREEN, AK_SPR_BRICK_BLUE, AK_SPR_BRICK_VIOLET,
};

void drawBrick(int r, int c) {
  if (!bricks[r][c].alive) return;
  Brick& brick = bricks[r][c];
  int x = brickX(c), y = brickY(r);
  const uint16_t* spr;
  if (brick.type == BRICK_STEEL) spr = AK_SPR_BRICK_STEEL;
  else if (brick.type == BRICK_TOUGH) spr = (brick.hp >= 2) ? AK_SPR_BRICK_SILVER_FULL : AK_SPR_BRICK_SILVER_CRACKED;
  else spr = BRICK_SPRITES[r];
  tft->drawRGBBitmap(x, y, spr, BRICK_W, BRICK_H);
}

void eraseBrick(int r, int c) {
  tft->fillRect(brickX(c), brickY(r), BRICK_W, BRICK_H, BG);
}

void loadLayout(const char** layout) {
  bricksRemaining = 0;
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      char ch = layout[r][c];
      Brick& brick = bricks[r][c];
      if (ch == '.') {
        brick.alive = false;
      } else if (ch == '#') {
        brick.alive = true; brick.type = BRICK_STEEL; brick.hp = 999;
      } else if (ch == '2') {
        brick.alive = true; brick.type = BRICK_TOUGH; brick.hp = 2;
        bricksRemaining++;
      } else {
        brick.alive = true; brick.type = BRICK_NORMAL; brick.hp = 1;
        bricksRemaining++;
      }
    }
  }
}

void loadRandomLayout() {
  int attempts = 0;
  do {
    bricksRemaining = 0;
    for (int r = 0; r < BRICK_ROWS; r++) {
      for (int c = 0; c < BRICK_COLS; c++) {
        Brick& brick = bricks[r][c];
        int roll = random(100);
        if (roll < 65) {
          brick.alive = true; brick.type = BRICK_NORMAL; brick.hp = 1;
          bricksRemaining++;
        } else if (roll < 78) {
          brick.alive = true; brick.type = BRICK_TOUGH; brick.hp = 2;
          bricksRemaining++;
        } else {
          brick.alive = false;
        }
      }
    }
    attempts++;
  } while (bricksRemaining < 12 && attempts < 8);
}

void loadLevelBricks(int levelIdx) {
  int slot = (levelIdx - 1) % (NUM_LAYOUTS + 1);
  if (slot == NUM_LAYOUTS) loadRandomLayout();
  else loadLayout(LAYOUTS[slot]);
}

// ================= Playfield boundaries =================
const int PLAYFIELD_TOP = 24;
const int HUD_H = 20;

// ================= Paddle =================
float paddleX;
int paddleY = 298;
int paddleH = 8;
int paddleW = 46;
const int PADDLE_W_DEFAULT = 46;
const int PADDLE_W_MIN = 26;
const int PADDLE_W_MAX = 70;
float lastDrawnPaddleX = -1000;
unsigned long paddleFlashUntil = 0;
bool lastPaddleFlashState = false;
unsigned long lastEncTickMs = 0;

// The "Vaus" paddle is built from 3 bitmap slices (red-flank cap, silver
// center tile repeated, mirrored red-flank cap) rather than one fixed
// bitmap, since paddleW changes continuously with power-ups (26-70px) —
// see tools/make_arkanoid_sprites.py.
void drawPaddle() {
  int x = (int)paddleX, y = paddleY;
  bool flashing = millis() < paddleFlashUntil;
  if (flashing) {
    tft->fillRoundRect(x, y, paddleW, paddleH, 2, PADDLE_FLASH);
  } else {
    tft->drawRGBBitmap(x, y, AK_SPR_PADDLE_LEFT, AK_SPR_PADDLE_LEFT_W, AK_SPR_PADDLE_LEFT_H);
    int midEnd = x + paddleW - AK_SPR_PADDLE_RIGHT_W;
    int midX = x + AK_SPR_PADDLE_LEFT_W;
    while (midX < midEnd) {
      tft->drawRGBBitmap(midX, y, AK_SPR_PADDLE_MID, AK_SPR_PADDLE_MID_W, AK_SPR_PADDLE_MID_H);
      midX += AK_SPR_PADDLE_MID_W;
    }
    tft->drawRGBBitmap(x + paddleW - AK_SPR_PADDLE_RIGHT_W, y, AK_SPR_PADDLE_RIGHT, AK_SPR_PADDLE_RIGHT_W, AK_SPR_PADDLE_RIGHT_H);
  }
  lastPaddleFlashState = flashing;
}
void erasePaddleAt(float x) {
  tft->fillRect((int)x - 1, paddleY - 1, paddleW + 2, paddleH + 2, BG);
}
// Full-width strip erase, used when paddle width itself changes (power-ups).
void forceRedrawPaddle() {
  tft->fillRect(0, paddleY - 1, SCREEN_W, paddleH + 2, BG);
  drawPaddle();
  lastDrawnPaddleX = paddleX;
}

// ================= Balls (multiball support) =================
struct Ball {
  bool active;
  float x, y, vx, vy;
  float lastDrawnX, lastDrawnY;
};
const int MAX_BALLS = 4;
Ball balls[MAX_BALLS];
int activeBallCount = 0;
const float BALL_RADIUS = 4;
float ballSpeed;

bool slowActive = false;
unsigned long slowUntil = 0;
const float SLOW_FACTOR = 0.65f;
const unsigned long SLOW_DURATION_MS = 8000;

void drawBallAt(float x, float y) {
  tft->drawRGBBitmap((int)x - AK_SPR_BALL_W / 2, (int)y - AK_SPR_BALL_H / 2, AK_SPR_BALL, AK_SPR_BALL_W, AK_SPR_BALL_H);
}
void eraseBallAt(float x, float y) {
  tft->fillRect((int)x - AK_SPR_BALL_W / 2 - 1, (int)y - AK_SPR_BALL_H / 2 - 1, AK_SPR_BALL_W + 2, AK_SPR_BALL_H + 2, BG);
}

void spawnBall(float x, float y, float vx, float vy) {
  for (int i = 0; i < MAX_BALLS; i++) {
    if (!balls[i].active) {
      balls[i].active = true;
      balls[i].x = x; balls[i].y = y;
      balls[i].vx = vx; balls[i].vy = vy;
      balls[i].lastDrawnX = x; balls[i].lastDrawnY = y;
      activeBallCount++;
      return;
    }
  }
}

void resetBalls() {
  for (int i = 0; i < MAX_BALLS; i++) balls[i].active = false;
  activeBallCount = 0;
}

// ================= Particles & score popups (juice) =================
struct Particle { bool active; float x, y, vx, vy; unsigned long bornMs; uint16_t color; };
const int MAX_PARTICLES = 40;
Particle particles[MAX_PARTICLES];
const unsigned long PARTICLE_LIFE_MS = 320;

void spawnParticles(float x, float y, uint16_t color, int count) {
  for (int n = 0; n < count; n++) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].active) {
        float ang = random(0, 360) * PI / 180.0f;
        float spd = random(30, 90);
        particles[i].active = true;
        particles[i].x = x; particles[i].y = y;
        particles[i].vx = spd * cosf(ang); particles[i].vy = spd * sinf(ang);
        particles[i].bornMs = millis();
        particles[i].color = color;
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
    tft->fillRect((int)p.x, (int)p.y, 2, 2, BG);
    if (now - p.bornMs > PARTICLE_LIFE_MS) { p.active = false; continue; }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.x < 0 || p.x > SCREEN_W || p.y < PLAYFIELD_TOP || p.y > SCREEN_H) { p.active = false; continue; }
    tft->fillRect((int)p.x, (int)p.y, 2, 2, p.color);
  }
}

struct ScorePopup { bool active; int x, y, w, h; unsigned long bornMs; char text[8]; uint16_t color; };
const int MAX_POPUPS = 6;
ScorePopup popups[MAX_POPUPS];
const unsigned long POPUP_LIFE_MS = 500;

void spawnPopup(int x, int y, int pts, uint16_t color) {
  for (int i = 0; i < MAX_POPUPS; i++) {
    if (!popups[i].active) {
      ScorePopup& p = popups[i];
      p.active = true;
      snprintf(p.text, sizeof(p.text), "+%d", pts);
      int16_t x1, y1; uint16_t w, h;
      tft->setTextSize(1);
      tft->getTextBounds(p.text, 0, 0, &x1, &y1, &w, &h);
      p.x = x; p.y = y; p.w = w + 2; p.h = h + 2;
      p.color = color;
      p.bornMs = millis();
      tft->setTextColor(color);
      tft->setCursor(p.x, p.y);
      tft->print(p.text);
      return;
    }
  }
}

void updatePopups() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_POPUPS; i++) {
    if (!popups[i].active) continue;
    if (now - popups[i].bornMs > POPUP_LIFE_MS) {
      tft->fillRect(popups[i].x - 1, popups[i].y - 1, popups[i].w, popups[i].h, BG);
      popups[i].active = false;
    }
  }
}

void resetJuice() {
  for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
  for (int i = 0; i < MAX_POPUPS; i++) popups[i].active = false;
}

// ================= Power-ups =================
enum PowerupType { PU_WIDEN, PU_NARROW, PU_MULTIBALL, PU_SLOW, PU_LIFE };
struct Powerup { bool active; float x, y, lastDrawnY; PowerupType type; };
const int MAX_POWERUPS = 3;
Powerup powerups[MAX_POWERUPS];
const int PU_W = 16, PU_H = 9;
const float PU_FALL_SPEED = 65.0f;

// Mapped to the closest authentic Arkanoid capsule letter+color for each
// of this game's power-up types — see tools/make_arkanoid_sprites.py.
const uint16_t* powerupSprite(PowerupType t) {
  switch (t) {
    case PU_WIDEN: return AK_SPR_CAPSULE_WIDEN;
    case PU_NARROW: return AK_SPR_CAPSULE_NARROW;
    case PU_MULTIBALL: return AK_SPR_CAPSULE_MULTI;
    case PU_SLOW: return AK_SPR_CAPSULE_SLOW;
    case PU_LIFE: return AK_SPR_CAPSULE_LIFE;
  }
  return AK_SPR_CAPSULE_LIFE;
}

void drawPowerup(Powerup& p) {
  tft->drawRGBBitmap((int)p.x, (int)p.y, powerupSprite(p.type), PU_W, PU_H);
}
void erasePowerupAt(float x, float y) {
  tft->fillRect((int)x - 1, (int)y - 1, PU_W + 2, PU_H + 2, BG);
}

void spawnPowerup(float centerX, float centerY) {
  if (random(100) >= 22) return; // ~22% drop chance
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (!powerups[i].active) {
      int roll = random(100);
      PowerupType t;
      if (roll < 32) t = PU_WIDEN;
      else if (roll < 52) t = PU_NARROW;
      else if (roll < 74) t = PU_MULTIBALL;
      else if (roll < 94) t = PU_SLOW;
      else t = PU_LIFE;
      powerups[i].active = true;
      powerups[i].x = centerX - PU_W / 2.0f;
      powerups[i].y = centerY - PU_H / 2.0f;
      powerups[i].lastDrawnY = powerups[i].y;
      powerups[i].type = t;
      return;
    }
  }
}

void resetPowerups() {
  for (int i = 0; i < MAX_POWERUPS; i++) powerups[i].active = false;
}

// ================= Game state =================
enum GameState { READY, PLAYING, PAUSED, LEVEL_CLEARED, GAME_OVER };
GameState state = READY;

int score = 0, lives = 3, level = 1, highScore = 0;
int lastDrawnScore = -1, lastDrawnLives = -1, lastDrawnLevel = -1;
unsigned long levelClearUntil = 0;

const float BASE_SPEED = 150.0f;
const float SPEED_STEP = 14.0f;
const float MAX_SPEED = 260.0f;

void loadHighScore() {
  prefs.begin("arkanoid", true);
  highScore = prefs.getInt("hi", 0);
  prefs.end();
}
void saveHighScoreIfNeeded() {
  if (score > highScore) {
    highScore = score;
    prefs.begin("arkanoid", false);
    prefs.putInt("hi", highScore);
    prefs.end();
  }
}

void drawHUD(bool force = false) {
  if (force || score != lastDrawnScore || level != lastDrawnLevel) {
    tft->fillRect(0, 2, 160, 16, BG);
    tft->setTextSize(1);
    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(4, 6);
    tft->printf("SCORE %04d  LV%d", score, level);
    lastDrawnScore = score;
    lastDrawnLevel = level;
  }
  if (force || lives != lastDrawnLives) {
    tft->fillRect(SCREEN_W - 70, 2, 70, 16, BG);
    for (int i = 0; i < lives; i++) {
      tft->drawRGBBitmap(SCREEN_W - 10 - i * 14 - AK_SPR_BALL_W / 2, 9 - AK_SPR_BALL_H / 2,
                          AK_SPR_BALL, AK_SPR_BALL_W, AK_SPR_BALL_H);
    }
    lastDrawnLives = lives;
  }
}

void placeBallOnPaddle() {
  resetBalls();
  spawnBall(paddleX + paddleW / 2.0f, paddleY - BALL_RADIUS - 1, 0, 0);
}

void initLevel(bool freshPaddle) {
  loadLevelBricks(level);
  ballSpeed = min(MAX_SPEED, BASE_SPEED + (level - 1) * SPEED_STEP);
  if (freshPaddle) {
    paddleW = PADDLE_W_DEFAULT;
  } else {
    paddleW = max(PADDLE_W_MIN, paddleW - 2);
  }
  paddleX = (SCREEN_W - paddleW) / 2.0f;
  slowActive = false;
  resetPowerups();
  resetJuice();
  placeBallOnPaddle();
}

void resetGame() {
  score = 0;
  lives = 3;
  level = 1;
  initLevel(true);
}

void drawPlayfield() {
  tft->fillScreen(BG);
  tft->drawFastHLine(0, HUD_H, SCREEN_W, HUD_LINE);
  for (int r = 0; r < BRICK_ROWS; r++)
    for (int c = 0; c < BRICK_COLS; c++)
      drawBrick(r, c);
  drawPaddle();
  for (int i = 0; i < MAX_BALLS; i++) {
    if (balls[i].active) {
      drawBallAt(balls[i].x, balls[i].y);
      balls[i].lastDrawnX = balls[i].x;
      balls[i].lastDrawnY = balls[i].y;
    }
  }
  drawHUD(true);
  lastDrawnPaddleX = paddleX;
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
  Serial.println("Level cleared");
}

void loseLife() {
  lives--;
  Serial.printf("Life lost, lives=%d\n", lives);
  if (lives <= 0) {
    saveHighScoreIfNeeded();
    state = GAME_OVER;
    drawGameOverScreen();
    sfxGameOver();
  } else {
    drawHUD();
    placeBallOnPaddle();
    drawBallAt(balls[0].x, balls[0].y);
    state = READY;
    sfxBad();
  }
}

void launchBall() {
  float angleDeg = random(-35, 36);
  float rad = angleDeg * PI / 180.0f;
  balls[0].vx = ballSpeed * sinf(rad);
  balls[0].vy = -ballSpeed * cosf(rad);
  sfxUiConfirm();
  Serial.println("Ball launched");
}

bool circleRectOverlap(float cx, float cy, float r, int rx, int ry, int rw, int rh) {
  float closestX = constrain(cx, (float)rx, (float)(rx + rw));
  float closestY = constrain(cy, (float)ry, (float)(ry + rh));
  float dx = cx - closestX, dy = cy - closestY;
  return (dx * dx + dy * dy) <= r * r;
}

// Applies brick damage/removal and bounces the ball off the hit brick.
void handleBrickHit(int r, int c, float ballCX, float ballCY, float& vx, float& vy) {
  Brick& brick = bricks[r][c];
  int bx = brickX(c), by = brickY(r);
  float dx = ballCX - (bx + BRICK_W / 2.0f);
  float dy = ballCY - (by + BRICK_H / 2.0f);
  float overlapX = (BRICK_W / 2.0f + BALL_RADIUS) - fabsf(dx);
  float overlapY = (BRICK_H / 2.0f + BALL_RADIUS) - fabsf(dy);
  if (overlapX < overlapY) vx = -vx; else vy = -vy;

  if (brick.type == BRICK_STEEL) {
    spawnParticles(bx + BRICK_W / 2.0f, by + BRICK_H / 2.0f, STEEL_COLOR, 4);
    sfxHit();
    return;
  }

  brick.hp--;
  uint16_t brickColor = (brick.type == BRICK_TOUGH) ? SILVER_FULL : rowColors[r];
  if (brick.hp <= 0) {
    brick.alive = false;
    bricksRemaining--;
    eraseBrick(r, c);
    int pts = (brick.type == BRICK_TOUGH) ? 25 : (BRICK_ROWS - r) * 10;
    score += pts;
    spawnParticles(bx + BRICK_W / 2.0f, by + BRICK_H / 2.0f, brickColor, 10);
    spawnPopup(bx + 2, by, pts, TITLE_COLOR);
    spawnPowerup(bx + BRICK_W / 2.0f, by + BRICK_H / 2.0f);
    sfxBreak();
    if (bricksRemaining <= 0) triggerLevelClear();
  } else {
    drawBrick(r, c);
    spawnParticles(bx + BRICK_W / 2.0f, by + BRICK_H / 2.0f, SILVER_FULL, 5);
    sfxHit();
  }
}

void updatePaddle(float dt, long encDelta, bool btn1Held, bool btn2Held, float thumbX) {
  // Base sensitivity is intentionally low for fine, continuous-feeling control.
  // Acceleration is scaled from the time *between encoder ticks* (not the
  // per-frame dt, which is noisy/tiny at this loop rate and made single slow
  // clicks register as huge spikes) so only genuinely fast spins ramp up.
  const float ENC_SENS_BASE = 2.2f;
  const float ENC_SENS_MAX_BONUS = 5.0f;
  const float ENC_ACCEL_SCALE = 0.035f;
  const float KEY_SPEED = 160.0f;
  const float THUMB_SPEED = 200.0f; // additive, alongside the encoder — not a replacement

  if (encDelta != 0) {
    unsigned long now = millis();
    float sens = ENC_SENS_BASE;
    if (lastEncTickMs != 0) {
      float intervalSec = (now - lastEncTickMs) / 1000.0f;
      if (intervalSec > 0.001f) {
        float rate = fabsf((float)encDelta) / intervalSec; // detents/sec between ticks
        sens += constrain(rate * ENC_ACCEL_SCALE, 0.0f, ENC_SENS_MAX_BONUS);
      }
    }
    lastEncTickMs = now;
    paddleX += encDelta * sens;
  }
  if (btn1Held) paddleX -= KEY_SPEED * dt;
  if (btn2Held) paddleX += KEY_SPEED * dt;
  paddleX += thumbX * THUMB_SPEED * dt;
  paddleX = constrain(paddleX, 0.0f, (float)(SCREEN_W - paddleW));

  if (state == READY && balls[0].active) {
    balls[0].x = paddleX + paddleW / 2.0f;
  }

  bool flashNow = millis() < paddleFlashUntil;
  if (fabsf(paddleX - lastDrawnPaddleX) >= 1.0f || flashNow != lastPaddleFlashState) {
    erasePaddleAt(lastDrawnPaddleX);
    drawPaddle();
    lastDrawnPaddleX = paddleX;
  }
}

void applyPowerup(PowerupType t) {
  switch (t) {
    case PU_WIDEN:
      paddleW = min(PADDLE_W_MAX, paddleW + 16);
      paddleX = constrain(paddleX - 8.0f, 0.0f, (float)(SCREEN_W - paddleW));
      forceRedrawPaddle();
      sfxPowerup();
      Serial.println("Powerup: widen");
      break;
    case PU_NARROW:
      paddleW = max(PADDLE_W_MIN, paddleW - 16);
      paddleX = constrain(paddleX + 8.0f, 0.0f, (float)(SCREEN_W - paddleW));
      forceRedrawPaddle();
      sfxBad();
      Serial.println("Powerup: narrow");
      break;
    case PU_MULTIBALL: {
      int srcIdx = -1;
      for (int i = 0; i < MAX_BALLS; i++) if (balls[i].active) { srcIdx = i; break; }
      if (srcIdx < 0) break;
      Ball src = balls[srcIdx];
      float speed = sqrtf(src.vx * src.vx + src.vy * src.vy);
      if (speed < 1.0f) break; // ball not launched yet, nothing to split
      float baseAngle = atan2f(src.vx, -src.vy);
      float offsets[2] = { 25.0f * PI / 180.0f, -25.0f * PI / 180.0f };
      for (int k = 0; k < 2; k++) {
        if (activeBallCount >= MAX_BALLS) break;
        float a = baseAngle + offsets[k];
        spawnBall(src.x, src.y, speed * sinf(a), -speed * cosf(a));
      }
      sfxPowerup();
      Serial.println("Powerup: multiball");
      break;
    }
    case PU_SLOW: {
      if (!slowActive) {
        for (int i = 0; i < MAX_BALLS; i++) {
          if (balls[i].active) { balls[i].vx *= SLOW_FACTOR; balls[i].vy *= SLOW_FACTOR; }
        }
        slowActive = true;
      }
      slowUntil = millis() + SLOW_DURATION_MS;
      sfxPowerup();
      Serial.println("Powerup: slow");
      break;
    }
    case PU_LIFE:
      lives = min(5, lives + 1);
      drawHUD();
      sfxPowerup();
      Serial.println("Powerup: extra life");
      break;
  }
}

void updatePowerups(float dt) {
  for (int i = 0; i < MAX_POWERUPS; i++) {
    if (!powerups[i].active) continue;
    Powerup& p = powerups[i];
    erasePowerupAt(p.x, p.lastDrawnY);
    p.y += PU_FALL_SPEED * dt;

    if (p.y > SCREEN_H) { p.active = false; continue; }

    bool caught = (p.y + PU_H >= paddleY) && (p.y <= paddleY + paddleH) &&
                  (p.x + PU_W >= paddleX) && (p.x <= paddleX + paddleW);
    if (caught) {
      applyPowerup(p.type);
      p.active = false;
      continue;
    }
    drawPowerup(p);
    p.lastDrawnY = p.y;
  }
}

void updateSlowMotionTimer() {
  if (slowActive && millis() > slowUntil) {
    for (int i = 0; i < MAX_BALLS; i++) {
      if (balls[i].active) { balls[i].vx /= SLOW_FACTOR; balls[i].vy /= SLOW_FACTOR; }
    }
    slowActive = false;
    Serial.println("Slow-mo expired");
  }
}

void updateBalls(float dt) {
  for (int i = 0; i < MAX_BALLS; i++) {
    if (!balls[i].active) continue;
    Ball& b = balls[i];

    float newX = b.x + b.vx * dt;
    float newY = b.y + b.vy * dt;

    if (newX - BALL_RADIUS < 0) { newX = BALL_RADIUS; b.vx = -b.vx; sfxHit(); }
    if (newX + BALL_RADIUS > SCREEN_W) { newX = SCREEN_W - BALL_RADIUS; b.vx = -b.vx; sfxHit(); }
    if (newY - BALL_RADIUS < PLAYFIELD_TOP) { newY = PLAYFIELD_TOP + BALL_RADIUS; b.vy = -b.vy; sfxHit(); }

    if (newY - BALL_RADIUS > SCREEN_H) {
      eraseBallAt(b.lastDrawnX, b.lastDrawnY);
      b.active = false;
      activeBallCount--;
      continue;
    }

    if (b.vy > 0 &&
        newY + BALL_RADIUS >= paddleY &&
        newY + BALL_RADIUS <= paddleY + paddleH + 6 &&
        newX >= paddleX - BALL_RADIUS && newX <= paddleX + paddleW + BALL_RADIUS) {
      float offset = (newX - (paddleX + paddleW / 2.0f)) / (paddleW / 2.0f);
      offset = constrain(offset, -1.0f, 1.0f);
      float maxAngle = 65.0f * PI / 180.0f;
      float angle = offset * maxAngle;
      float speed = sqrtf(b.vx * b.vx + b.vy * b.vy);
      b.vx = speed * sinf(angle);
      b.vy = -speed * cosf(angle);
      newY = paddleY - BALL_RADIUS - 0.5f;
      paddleFlashUntil = millis() + 90;
      sfxHit();
    }

    bool hitBrick = false;
    for (int r = 0; r < BRICK_ROWS && !hitBrick; r++) {
      for (int c = 0; c < BRICK_COLS; c++) {
        if (!bricks[r][c].alive) continue;
        int bx = brickX(c), by = brickY(r);
        if (circleRectOverlap(newX, newY, BALL_RADIUS, bx, by, BRICK_W, BRICK_H)) {
          handleBrickHit(r, c, newX, newY, b.vx, b.vy);
          hitBrick = true;
          break;
        }
      }
    }
    if (state != PLAYING) return; // level-clear may have fired mid-loop

    b.x = newX;
    b.y = newY;

    if (fabsf(b.x - b.lastDrawnX) >= 1.0f || fabsf(b.y - b.lastDrawnY) >= 1.0f) {
      eraseBallAt(b.lastDrawnX, b.lastDrawnY);
      drawBallAt(b.x, b.y);
      b.lastDrawnX = b.x;
      b.lastDrawnY = b.y;
    }
  }

  if (activeBallCount <= 0) loseLife();
}

} // namespace

void arkanoidInit() {
  initGamePalette();
  loadHighScore();
  lastEncTickMs = 0;
  resetGame();
  state = READY;
  drawPlayfield();
  Serial.printf("Arkanoid ready. High score: %d\n", highScore);
}

void arkanoidUpdate(const InputState& in, float dt) {
  switch (state) {
    case READY:
      updatePaddle(dt, in.encDelta, in.btn1Held, in.btn2Held, in.thumbX);
      if (in.key0Edge) {
        launchBall();
        state = PLAYING;
      }
      break;

    case PLAYING:
      updatePaddle(dt, in.encDelta, in.btn1Held, in.btn2Held, in.thumbX);
      if (state == PLAYING) updateSlowMotionTimer();
      if (state == PLAYING) updateBalls(dt);
      if (state == PLAYING) updatePowerups(dt);
      if (state == PLAYING) updateParticles(dt);
      if (state == PLAYING) updatePopups();
      if (state == PLAYING && in.encSwEdge) {
        state = PAUSED;
        drawCentered("PAUSED", 150, 2, TITLE_COLOR);
        drawCentered("ENC:RESUME  KEY0:EXIT", 180, 1, TEXT_COLOR);
        sfxUiMove();
      }
      break;

    case PAUSED:
      if (in.encSwEdge) {
        state = PLAYING;
        drawPlayfield();
        sfxUiMove();
      }
      break;

    case LEVEL_CLEARED:
      if (millis() >= levelClearUntil) {
        level++;
        initLevel(false);
        state = READY;
        drawPlayfield();
        Serial.printf("Level %d\n", level);
      }
      break;

    case GAME_OVER:
      if (in.encSwEdge) {
        resetGame();
        state = READY;
        drawPlayfield();
        sfxUiConfirm();
        Serial.println("New game started");
      }
      break;
  }
}

void arkanoidOnExit() {
  saveHighScoreIfNeeded();
}

// KEY0 is now the main action button (launch ball), so it can't also mean
// "back to menu" everywhere the way it used to — only from the pause
// screen, same pattern as R-Type.
bool arkanoidWantsExit(const InputState& in) {
  return state == PAUSED && in.key0Edge;
}

// Re-renders whatever the current game state would show, onto whatever
// `tft` currently points to (used for screenshot capture — see main.cpp).
void arkanoidRenderSnapshot() {
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
    default: // READY, PLAYING
      drawPlayfield();
      break;
  }
}
