// Bust-A-Move / Puzzle Bobble style bubble shooter.
// Controls: thumbstick X = aim (encoder no longer aims), KEY0 = fire (main
// action button), ENC_SW held (~500ms) = pause. KEY0 only returns to the
// menu from the pause screen (or from GAME_OVER) — see bustamoveWantsExit().
#include "bustamove.h"
#include "hardware.h"
#include "palette.h"
#include "sound.h"
#include "bustamove_sprites.h"
#include <Preferences.h>
#include <string.h>

namespace {

Preferences prefs;

// ================= Grid geometry (hex-packed, odd rows shifted right) =====
const int BUBBLE_D = 18;
const int R = BUBBLE_D / 2;
const int ROW_H = 16;
const int GRID_LEFT = 3;
const int GRID_TOP = 24;
const int GRID_COLS = 13;      // even-row column count; odd rows have one fewer
const int MAX_ROWS = 14;       // kept clear of the shooter zone below it
const int LOSE_ROW = 13;       // a bubble at/after this row ends the game
const int COLLIDE_DIST = BUBBLE_D - 3;

int colsInRow(int row) { return (row % 2 == 0) ? GRID_COLS : GRID_COLS - 1; }

float cellCenterX(int row, int col) {
  float x = GRID_LEFT + R + col * BUBBLE_D;
  if (row % 2 == 1) x += R;
  return x;
}
float cellCenterY(int row) { return GRID_TOP + R + row * ROW_H; }

int rowFromY(float y) { return (int)roundf((y - GRID_TOP - R) / (float)ROW_H); }
int colFromX(float x, int row) {
  float xx = x - GRID_LEFT - R;
  if (row % 2 == 1) xx -= R;
  return (int)roundf(xx / (float)BUBBLE_D);
}

void getNeighbors(int row, int col, int outR[6], int outC[6], int& count) {
  count = 0;
  outR[count] = row; outC[count] = col - 1; count++;
  outR[count] = row; outC[count] = col + 1; count++;
  if (row % 2 == 0) {
    outR[count] = row - 1; outC[count] = col - 1; count++;
    outR[count] = row - 1; outC[count] = col;     count++;
    outR[count] = row + 1; outC[count] = col - 1; count++;
    outR[count] = row + 1; outC[count] = col;     count++;
  } else {
    outR[count] = row - 1; outC[count] = col;     count++;
    outR[count] = row - 1; outC[count] = col + 1; count++;
    outR[count] = row + 1; outC[count] = col;     count++;
    outR[count] = row + 1; outC[count] = col + 1; count++;
  }
}

// ================= Palette =================
uint16_t BUBBLE_COLORS[6];
const int NUM_COLORS = 6;
uint16_t METAL, METAL_DARK;

void initGamePalette() {
  BUBBLE_COLORS[0] = rgb565(230, 60, 70);   // red
  BUBBLE_COLORS[1] = rgb565(70, 140, 255);  // blue
  BUBBLE_COLORS[2] = rgb565(90, 220, 110);  // green
  BUBBLE_COLORS[3] = rgb565(250, 210, 60);  // yellow
  BUBBLE_COLORS[4] = rgb565(200, 90, 230);  // purple
  BUBBLE_COLORS[5] = rgb565(60, 220, 220);  // cyan
  METAL = rgb565(120, 125, 145);
  METAL_DARK = rgb565(55, 58, 72);
}

// ================= Bubble sprite (glossy sphere look) =================
const uint16_t* const BUBBLE_SPRITES[6] = {
  BM_SPR_BUBBLE_RED, BM_SPR_BUBBLE_BLUE, BM_SPR_BUBBLE_GREEN,
  BM_SPR_BUBBLE_YELLOW, BM_SPR_BUBBLE_PURPLE, BM_SPR_BUBBLE_CYAN,
};
const uint16_t* const BUBBLE_SPRITES_SMALL[6] = {
  BM_SPR_BUBBLESMALL_RED, BM_SPR_BUBBLESMALL_BLUE, BM_SPR_BUBBLESMALL_GREEN,
  BM_SPR_BUBBLESMALL_YELLOW, BM_SPR_BUBBLESMALL_PURPLE, BM_SPR_BUBBLESMALL_CYAN,
};

// `rad` picks which of the 2 baked sizes to use (>= R-1 -> the full grid/
// flying-bubble sprite, smaller -> the "next bubble" preview sprite) —
// kept as a radius param so call sites (and the erase-by-radius calls
// alongside them) didn't need restructuring.
void drawBubbleAt(float cx, float cy, int colorIdx, int rad) {
  bool full = rad >= R - 1;
  const uint16_t* spr = full ? BUBBLE_SPRITES[colorIdx] : BUBBLE_SPRITES_SMALL[colorIdx];
  int w = full ? BM_SPR_BUBBLE_RED_W : BM_SPR_BUBBLESMALL_RED_W;
  int h = full ? BM_SPR_BUBBLE_RED_H : BM_SPR_BUBBLESMALL_RED_H;
  tft->drawRGBBitmap((int)cx - w / 2, (int)cy - h / 2, spr, w, h);
}
void eraseBubbleAt(float cx, float cy, int rad) {
  tft->fillCircle((int)cx, (int)cy, rad + 1, BG);
}

// ================= Grid state =================
int8_t grid[MAX_ROWS][GRID_COLS]; // -1 empty, else color index

void drawGridCell(int row, int col) {
  if (grid[row][col] < 0) return;
  drawBubbleAt(cellCenterX(row, col), cellCenterY(row), grid[row][col], R - 1);
}
void eraseGridCell(int row, int col) {
  eraseBubbleAt(cellCenterX(row, col), cellCenterY(row), R);
}
void drawGrid() {
  for (int r = 0; r < MAX_ROWS; r++) {
    int cols = colsInRow(r);
    for (int c = 0; c < cols; c++) {
      if (grid[r][c] >= 0) drawGridCell(r, c);
      else eraseGridCell(r, c);
    }
  }
}

bool present[NUM_COLORS];
int pickRandomColorFromGrid() {
  for (int i = 0; i < NUM_COLORS; i++) present[i] = false;
  int count = 0;
  for (int r = 0; r < MAX_ROWS; r++) {
    for (int c = 0; c < colsInRow(r); c++) {
      if (grid[r][c] >= 0 && !present[grid[r][c]]) { present[grid[r][c]] = true; count++; }
    }
  }
  if (count == 0) return random(0, NUM_COLORS);
  int pick = random(0, count);
  for (int i = 0; i < NUM_COLORS; i++) {
    if (present[i]) {
      if (pick == 0) return i;
      pick--;
    }
  }
  return random(0, NUM_COLORS);
}

// Named layout shapes (cycled by level, like real Puzzle Bobble stages,
// which are hand-designed clusters/gaps rather than one solid filled
// block). Parametric on (row,rows,col,cols) rather than hand-drawn ASCII
// so they scale cleanly with however many rows a level uses.
enum LayoutShape { SHAPE_PYRAMID, SHAPE_DIAMOND, SHAPE_CHECKER, SHAPE_ISLANDS, SHAPE_RING, NUM_SHAPES };

bool shapeFilled(LayoutShape shape, int row, int rows, int col, int cols) {
  int center = cols / 2;
  switch (shape) {
    case SHAPE_PYRAMID: {
      int half = rows - 1 - row; // widest at the ceiling, tapering to a point
      return col >= center - half && col <= center + half;
    }
    case SHAPE_DIAMOND: {
      int mid = rows / 2;
      int half = rows / 2 - abs(row - mid);
      return col >= center - half && col <= center + half;
    }
    case SHAPE_CHECKER:
      return ((row + col) % 2) == 0;
    case SHAPE_ISLANDS: {
      bool inGapBand = col >= center - 1 && col <= center + 1;
      bool nearBottom = row >= rows - 2;
      return !inGapBand || nearBottom;
    }
    case SHAPE_RING: {
      bool borderRow = (row <= 1 || row >= rows - 2);
      bool borderCol = (col <= 1 || col >= cols - 2);
      return borderRow || borderCol;
    }
    default:
      return true;
  }
}

// Builds a level layout: a named shape (or, one slot in the rotation, a
// scattered/chaotic fill) with chunky 2x2 mirrored color blocks inside it,
// rather than one random color per cell or a solid filled rectangle.
void initGrid(int rows, int levelForShape) {
  for (int r = 0; r < MAX_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++)
      grid[r][c] = -1;

  const int BLOCK_W = 2, BLOCK_H = 2;
  const int MACRO_ROWS = (MAX_ROWS + BLOCK_H - 1) / BLOCK_H;
  const int MACRO_COLS = (GRID_COLS / 2 + BLOCK_W - 1) / BLOCK_W + 1;
  static int8_t macroColor[MACRO_ROWS][MACRO_COLS];
  for (int mr = 0; mr < MACRO_ROWS; mr++)
    for (int mc = 0; mc < MACRO_COLS; mc++)
      macroColor[mr][mc] = random(0, NUM_COLORS);

  int slot = (levelForShape - 1) % (NUM_SHAPES + 1);
  bool chaos = (slot == NUM_SHAPES);
  LayoutShape shape = (LayoutShape)(chaos ? 0 : slot);

  for (int r = 0; r < rows; r++) {
    int cols = colsInRow(r);
    int half = (cols + 1) / 2;
    for (int c = 0; c < half; c++) {
      bool filled = chaos ? (random(100) < 65) : shapeFilled(shape, r, rows, c, cols);
      grid[r][c] = filled ? macroColor[r / BLOCK_H][c / BLOCK_W] : -1;
    }
    for (int c = half; c < cols; c++) {
      grid[r][c] = grid[r][cols - 1 - c];
    }
  }
}

// ================= Group search (match + floating-bubble check) =================
bool visitedBuf[MAX_ROWS][GRID_COLS];
bool anchoredBuf[MAX_ROWS][GRID_COLS];
int stackR[MAX_ROWS * GRID_COLS];
int stackC[MAX_ROWS * GRID_COLS];
int groupR[MAX_ROWS * GRID_COLS];
int groupC[MAX_ROWS * GRID_COLS];

int collectGroup(int row, int col, int color) {
  memset(visitedBuf, 0, sizeof(visitedBuf));
  int sp = 0, count = 0;
  stackR[sp] = row; stackC[sp] = col; sp++;
  visitedBuf[row][col] = true;
  while (sp > 0) {
    sp--;
    int r = stackR[sp], c = stackC[sp];
    groupR[count] = r; groupC[count] = c; count++;
    int nr[6], nc[6], ncount;
    getNeighbors(r, c, nr, nc, ncount);
    for (int i = 0; i < ncount; i++) {
      int rr = nr[i], cc = nc[i];
      if (rr < 0 || rr >= MAX_ROWS || cc < 0 || cc >= colsInRow(rr)) continue;
      if (visitedBuf[rr][cc] || grid[rr][cc] != color) continue;
      visitedBuf[rr][cc] = true;
      stackR[sp] = rr; stackC[sp] = cc; sp++;
    }
  }
  return count;
}

void computeAnchored() {
  memset(anchoredBuf, 0, sizeof(anchoredBuf));
  int sp = 0;
  for (int c = 0; c < colsInRow(0); c++) {
    if (grid[0][c] >= 0) { anchoredBuf[0][c] = true; stackR[sp] = 0; stackC[sp] = c; sp++; }
  }
  while (sp > 0) {
    sp--;
    int r = stackR[sp], c = stackC[sp];
    int nr[6], nc[6], ncount;
    getNeighbors(r, c, nr, nc, ncount);
    for (int i = 0; i < ncount; i++) {
      int rr = nr[i], cc = nc[i];
      if (rr < 0 || rr >= MAX_ROWS || cc < 0 || cc >= colsInRow(rr)) continue;
      if (grid[rr][cc] < 0 || anchoredBuf[rr][cc]) continue;
      anchoredBuf[rr][cc] = true;
      stackR[sp] = rr; stackC[sp] = cc; sp++;
    }
  }
}

void findNearestEmpty(int startRow, int startCol, float px, float py, int& outRow, int& outCol) {
  float bestDist = 1e9f;
  bool found = false;
  for (int rr = max(0, startRow - 2); rr <= min(MAX_ROWS - 1, startRow + 2); rr++) {
    for (int cc = 0; cc < colsInRow(rr); cc++) {
      if (grid[rr][cc] >= 0) continue;
      float bx = cellCenterX(rr, cc), by = cellCenterY(rr);
      float d = (bx - px) * (bx - px) + (by - py) * (by - py);
      if (d < bestDist) { bestDist = d; outRow = rr; outCol = cc; found = true; }
    }
  }
  (void)found; // if nothing found, caller's original (occupied) cell is left as-is
}

// ================= Particles (match/pop juice) =================
struct Particle { bool active; float x, y, vx, vy; unsigned long bornMs; uint16_t color; };
const int MAX_PARTICLES = 50;
Particle particles[MAX_PARTICLES];
const unsigned long PARTICLE_LIFE_MS = 350;

void spawnParticles(float x, float y, uint16_t color, int count) {
  for (int n = 0; n < count; n++) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
      if (!particles[i].active) {
        float ang = random(0, 360) * PI / 180.0f;
        float spd = random(40, 110);
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
    tft->fillRect((int)p.x, (int)p.y, 2, 2, BG);
    if (now - p.bornMs > PARTICLE_LIFE_MS) { p.active = false; continue; }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.x < 0 || p.x > SCREEN_W || p.y < GRID_TOP || p.y > SCREEN_H) { p.active = false; continue; }
    tft->fillRect((int)p.x, (int)p.y, 2, 2, p.color);
  }
}
void resetParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) particles[i].active = false;
}

// ================= Game state =================
enum GameState { PLAYING, LEVEL_CLEARED, PAUSED, GAME_OVER };
GameState state = PLAYING;
GameState stateBeforePause = PLAYING;

// ENC_SW held (~500ms) pauses — same convention as R-Type/Arkanoid/Tetris;
// ENC_SW is reserved for infrequent actions since it's awkward to press.
bool prevEncSwHeld = false;
unsigned long encSwHeldSinceMs = 0;
bool pauseLatched = false;
const unsigned long PAUSE_HOLD_MS = 500;

int score = 0, level = 1, highScore = 0;
int lastDrawnScore = -1, lastDrawnLevel = -1;
unsigned long levelClearUntil = 0;

const float BASE_SHOT_SPEED = 260.0f;
const float SPEED_STEP = 12.0f;
const float MAX_SHOT_SPEED = 340.0f;

const float SHOOTER_X = SCREEN_W / 2.0f;
const float SHOOTER_Y = 300.0f;
float aimAngleDeg = 0;

int curColorIdx = 0, nextColorIdx = 0;
bool bubbleFlying = false;
int flyColorIdx = 0;
float flyX, flyY, flyVX, flyVY;
float lastDrawnFlyX = -1000, lastDrawnFlyY = -1000;

void loadHighScore() {
  prefs.begin("bustamove", true);
  highScore = prefs.getInt("hi", 0);
  prefs.end();
}
void saveHighScoreIfNeeded() {
  if (score > highScore) {
    highScore = score;
    prefs.begin("bustamove", false);
    prefs.putInt("hi", highScore);
    prefs.end();
  }
}

void drawHUD(bool force) {
  if (force || score != lastDrawnScore || level != lastDrawnLevel) {
    tft->fillRect(0, 2, SCREEN_W, 16, BG);
    tft->setTextSize(1);
    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(4, 6);
    tft->printf("SCORE %05d  LV%d", score, level);
    lastDrawnScore = score;
    lastDrawnLevel = level;
  }
}

void drawFrame() {
  tft->fillScreen(BG);
  tft->drawFastHLine(0, 20, SCREEN_W, HUD_LINE);
  tft->drawFastHLine(0, SHOOTER_Y - 34, SCREEN_W, HUD_LINE);
}

const float BARREL_START = 15.0f; // kept clear of the hub radius (12px) so
const float BARREL_LEN = 24.0f;   // erasing/redrawing it never touches the
                                   // static hub/bubble underneath.
float lastDrawnAngle = -999;

void barrelEndpoints(float angleDeg, int& x0, int& y0, int& x1, int& y1) {
  float rad = angleDeg * PI / 180.0f;
  float dirX = sinf(rad), dirY = -cosf(rad);
  x0 = (int)(SHOOTER_X + dirX * BARREL_START);
  y0 = (int)(SHOOTER_Y - 4 + dirY * BARREL_START);
  x1 = (int)(SHOOTER_X + dirX * (BARREL_START + BARREL_LEN));
  y1 = (int)(SHOOTER_Y - 4 + dirY * (BARREL_START + BARREL_LEN));
}

void paintBarrel(float angleDeg, uint16_t color) {
  int x0, y0, x1, y1;
  barrelEndpoints(angleDeg, x0, y0, x1, y1);
  float rad = angleDeg * PI / 180.0f;
  float dirX = sinf(rad), dirY = -cosf(rad);
  for (int o = -3; o <= 3; o++) {
    tft->drawLine(x0 - (int)(o * dirY), y0 + (int)(o * dirX),
                 x1 - (int)(o * dirY), y1 + (int)(o * dirX), color);
  }
}

// Static parts: base, turret hub, loaded bubble, next-bubble preview, and
// the barrel at its current angle. Drawn once per level/shot (not per aim
// tick) so turning the encoder can't flicker this whole zone — only the
// thin barrel line gets erased/redrawn on aim changes (redrawBarrelIfMoved
// below). The barrel is repainted here too because this function's own
// clear-rect covers where it lives; skipping that repaint was the bug
// where the barrel vanished after every shot and stayed gone until the
// next aim adjustment.
void drawShooterBase() {
  const int zoneW = 100, zoneH = 56;
  tft->fillRect((int)SHOOTER_X - zoneW / 2, (int)SHOOTER_Y - zoneH + 8, zoneW, zoneH, BG);

  tft->fillRoundRect((int)SHOOTER_X - 24, (int)SHOOTER_Y - 6, 48, 20, 6, METAL);
  tft->drawRoundRect((int)SHOOTER_X - 24, (int)SHOOTER_Y - 6, 48, 20, 6, METAL_DARK);
  tft->fillCircle((int)SHOOTER_X, (int)SHOOTER_Y - 4, 12, METAL);
  tft->drawCircle((int)SHOOTER_X, (int)SHOOTER_Y - 4, 12, METAL_DARK);

  drawBubbleAt(SHOOTER_X, SHOOTER_Y - 4, curColorIdx, R - 1);

  tft->setTextSize(1);
  tft->setTextColor(TEXT_COLOR);
  tft->setCursor((int)SHOOTER_X + 30, (int)SHOOTER_Y - 34);
  tft->print("NEXT");
  drawBubbleAt(SHOOTER_X + 40, SHOOTER_Y - 18, nextColorIdx, 6);

  paintBarrel(aimAngleDeg, METAL);
  lastDrawnAngle = aimAngleDeg;
}

void redrawBarrelIfMoved() {
  if (aimAngleDeg == lastDrawnAngle) return;
  if (lastDrawnAngle > -999) paintBarrel(lastDrawnAngle, BG);
  paintBarrel(aimAngleDeg, METAL);
  lastDrawnAngle = aimAngleDeg;
}

void drawPauseOverlay() {
  tft->fillRect(20, 130, SCREEN_W - 40, 60, darken(BG, -6));
  tft->drawRect(20, 130, SCREEN_W - 40, 60, TITLE_COLOR);
  drawCentered("PAUSED", 145, 2, TITLE_COLOR);
  drawCentered("ENC:RESUME  KEY0:EXIT", 172, 1, TEXT_COLOR);
}

void drawGameOverScreen() {
  tft->fillScreen(BG);
  drawCentered("GAME OVER", 100, 3, WARN_COLOR);
  char buf[24];
  snprintf(buf, sizeof(buf), "SCORE: %05d", score);
  drawCentered(buf, 150, 2, TEXT_COLOR);
  if (score >= highScore && score > 0) {
    drawCentered("NEW HIGH SCORE!", 180, 1, TITLE_COLOR);
  } else {
    snprintf(buf, sizeof(buf), "HIGH SCORE: %05d", highScore);
    drawCentered(buf, 180, 1, TEXT_COLOR);
  }
  drawCentered("PRESS BTN1: PLAY AGAIN", 230, 1, TITLE_COLOR);
  drawCentered("KEY0: MENU", 248, 1, TEXT_COLOR);
}

void drawLevelClearOverlay() {
  drawCentered("CLEARED!", 150, 2, TITLE_COLOR);
}

// Debug aid: ASCII dump of the grid (no way to read pixels back off this
// display). Digits 0-5 = bubble color index, '.' = empty. Odd rows are
// printed with a leading space to visually reflect their half-cell offset.
void printGridDebug(int lastRow, int lastCol, int lastGroupSize, int floaters) {
  Serial.println("+-------------+");
  for (int r = 0; r < MAX_ROWS; r++) {
    Serial.print(r % 2 == 1 ? " " : "");
    for (int c = 0; c < colsInRow(r); c++) {
      Serial.print(grid[r][c] >= 0 ? char('0' + grid[r][c]) : '.');
      Serial.print(' ');
    }
    Serial.println();
  }
  Serial.printf("+------------- landed=(%d,%d) group=%d floaters=%d score=%d level=%d\n",
                lastRow, lastCol, lastGroupSize, floaters, score, level);
}

void triggerGameOver() {
  saveHighScoreIfNeeded();
  state = GAME_OVER;
  drawGameOverScreen();
  sfxGameOver();
  Serial.println("Bust-a-Move: game over");
}

void triggerLevelClear() {
  score += 150;
  drawHUD(true);
  drawLevelClearOverlay();
  levelClearUntil = millis() + 1400;
  state = LEVEL_CLEARED;
  sfxLevelClear();
  Serial.println("Bust-a-Move: grid cleared");
}

bool anyBubbleAtOrBelow(int rowLimit) {
  for (int r = rowLimit; r < MAX_ROWS; r++)
    for (int c = 0; c < colsInRow(r); c++)
      if (grid[r][c] >= 0) return true;
  return false;
}
bool gridEmpty() {
  for (int r = 0; r < MAX_ROWS; r++)
    for (int c = 0; c < colsInRow(r); c++)
      if (grid[r][c] >= 0) return false;
  return true;
}

void startLevel(bool freshRun) {
  if (freshRun) { score = 0; level = 1; }
  int rows = min(8 + (level - 1), 11);
  initGrid(rows, level);
  curColorIdx = pickRandomColorFromGrid();
  nextColorIdx = pickRandomColorFromGrid();
  aimAngleDeg = 0;
  lastDrawnAngle = -999;
  bubbleFlying = false;
  resetParticles();
  pauseLatched = false;
  prevEncSwHeld = false;
  drawFrame();
  drawGrid();
  drawShooterBase(); // also (re)paints the barrel at the reset angle
  drawHUD(true);
  state = PLAYING;
}

void snapAndResolve() {
  bubbleFlying = false;
  eraseBubbleAt(lastDrawnFlyX, lastDrawnFlyY, R);

  int row = constrain(rowFromY(flyY), 0, MAX_ROWS - 1);
  int col = constrain(colFromX(flyX, row), 0, colsInRow(row) - 1);
  if (grid[row][col] >= 0) {
    findNearestEmpty(row, col, flyX, flyY, row, col);
  }
  grid[row][col] = flyColorIdx;

  int groupSize = collectGroup(row, col, flyColorIdx);
  bool popped = false;
  if (groupSize >= 3) {
    for (int i = 0; i < groupSize; i++) {
      int r = groupR[i], c = groupC[i];
      grid[r][c] = -1;
      spawnParticles(cellCenterX(r, c), cellCenterY(r), BUBBLE_COLORS[flyColorIdx], 8);
    }
    score += groupSize * 10;
    sfxBreak();
    popped = true;
  } else {
    sfxHit();
  }

  computeAnchored();
  int floaters = 0;
  for (int r = 0; r < MAX_ROWS; r++) {
    for (int c = 0; c < colsInRow(r); c++) {
      if (grid[r][c] >= 0 && !anchoredBuf[r][c]) {
        spawnParticles(cellCenterX(r, c), cellCenterY(r), BUBBLE_COLORS[grid[r][c]], 6);
        grid[r][c] = -1;
        floaters++;
      }
    }
  }
  if (floaters > 0) {
    score += floaters * 20;
    sfxPowerup();
  }

  drawGrid();
  drawHUD(true);
  printGridDebug(row, col, groupSize, floaters);

  if (anyBubbleAtOrBelow(LOSE_ROW)) {
    triggerGameOver();
    return;
  }
  if (gridEmpty()) {
    triggerLevelClear();
    return;
  }

  curColorIdx = nextColorIdx;
  nextColorIdx = pickRandomColorFromGrid();
  drawShooterBase();
  (void)popped;
}

void fireBubble() {
  flyColorIdx = curColorIdx;
  float rad = aimAngleDeg * PI / 180.0f;
  float speed = min(MAX_SHOT_SPEED, BASE_SHOT_SPEED + (level - 1) * SPEED_STEP);
  flyVX = speed * sinf(rad);
  flyVY = -speed * cosf(rad);
  flyX = SHOOTER_X;
  flyY = SHOOTER_Y - 18;
  bubbleFlying = true;
  // Sentinel, not the spawn point: the spawn point overlaps the turret hub,
  // so erasing "the previous position" there on the first flight frame bit
  // a chunk out of the hub/loaded-bubble graphics every time you fired.
  lastDrawnFlyX = -1000;
  lastDrawnFlyY = -1000;
  sfxUiConfirm();
}

// Aim is thumbstick-only now (encoder no longer aims, per user request) —
// a direct analog angular velocity rather than the encoder's tick-timing
// acceleration curve, since the stick already gives a continuous -1..1
// deflection instead of discrete ticks.
void updateAim(float thumbX, float dt) {
  const float AIM_SPEED_DEG_PER_SEC = 90.0f;
  if (thumbX == 0.0f) return;
  aimAngleDeg = constrain(aimAngleDeg + thumbX * AIM_SPEED_DEG_PER_SEC * dt, -75.0f, 75.0f);
  redrawBarrelIfMoved();
}

void updateFlyingBubble(float dt) {
  if (!bubbleFlying) return;

  if (lastDrawnFlyX > -999) eraseBubbleAt(lastDrawnFlyX, lastDrawnFlyY, R);
  flyX += flyVX * dt;
  flyY += flyVY * dt;

  if (flyX - R < GRID_LEFT) { flyX = GRID_LEFT + R; flyVX = -flyVX; sfxHit(); }
  if (flyX + R > SCREEN_W - GRID_LEFT) { flyX = SCREEN_W - GRID_LEFT - R; flyVX = -flyVX; sfxHit(); }

  bool landed = (flyY - R <= GRID_TOP);
  if (!landed) {
    int approxRow = rowFromY(flyY);
    for (int rr = max(0, approxRow - 1); rr <= min(MAX_ROWS - 1, approxRow + 1) && !landed; rr++) {
      for (int cc = 0; cc < colsInRow(rr); cc++) {
        if (grid[rr][cc] < 0) continue;
        float bx = cellCenterX(rr, cc), by = cellCenterY(rr);
        float dx = flyX - bx, dy = flyY - by;
        if (dx * dx + dy * dy <= (float)(COLLIDE_DIST * COLLIDE_DIST)) { landed = true; break; }
      }
    }
  }

  if (landed) {
    snapAndResolve();
  } else {
    drawBubbleAt(flyX, flyY, flyColorIdx, R - 1);
    lastDrawnFlyX = flyX;
    lastDrawnFlyY = flyY;
  }
}

} // namespace

void bustamoveInit() {
  initGamePalette();
  loadHighScore();
  startLevel(true);
  Serial.printf("Bust-a-Move ready. High score: %d\n", highScore);
}

void bustamoveUpdate(const InputState& in, float dt) {
  switch (state) {
    case PLAYING:
      if (!bubbleFlying) {
        updateAim(in.thumbX, dt);
        if (in.key0Edge) fireBubble();
      }
      updateFlyingBubble(dt);
      updateParticles(dt);

      if (in.encSwHeld && !prevEncSwHeld) { encSwHeldSinceMs = millis(); pauseLatched = false; }
      if (in.encSwHeld && !pauseLatched && millis() - encSwHeldSinceMs >= PAUSE_HOLD_MS) {
        pauseLatched = true;
        stateBeforePause = state;
        state = PAUSED;
        drawPauseOverlay();
        sfxUiMove();
      }
      prevEncSwHeld = in.encSwHeld;
      break;

    case PAUSED:
      prevEncSwHeld = in.encSwHeld;
      if (in.encSwEdge) {
        state = stateBeforePause;
        pauseLatched = false;
        drawFrame();
        drawGrid();
        drawShooterBase();
        drawHUD(true);
        sfxUiMove();
      }
      break;

    case LEVEL_CLEARED:
      if (millis() >= levelClearUntil) {
        level++;
        startLevel(false);
        Serial.printf("Level %d\n", level);
      }
      break;

    case GAME_OVER:
      if (in.btn1Edge) {
        startLevel(true);
        sfxUiConfirm();
      }
      break;
  }
}

void bustamoveOnExit() {
  saveHighScoreIfNeeded();
}

// Re-renders whatever the current game state would show, onto whatever
// `tft` currently points to (used for screenshot capture — see main.cpp).
void bustamoveRenderSnapshot() {
  switch (state) {
    case GAME_OVER:
      drawGameOverScreen();
      break;
    case LEVEL_CLEARED:
      drawFrame();
      drawGrid();
      drawShooterBase();
      drawHUD(true);
      drawLevelClearOverlay();
      break;
    case PAUSED:
    default: // PLAYING
      drawFrame();
      drawGrid();
      drawShooterBase();
      drawHUD(true);
      if (bubbleFlying) drawBubbleAt(flyX, flyY, flyColorIdx, R - 1);
      if (state == PAUSED) drawPauseOverlay();
      break;
  }
}

// KEY0 is now the main action button (fire), so it can't also mean "back
// to menu" during play — only from the pause screen, or from GAME_OVER
// (which already has no conflicting use for KEY0).
bool bustamoveWantsExit(const InputState& in) {
  return (state == PAUSED || state == GAME_OVER) && in.key0Edge;
}
