// Tetris clone. Controls: thumbstick X = shift piece left/right (DAS-style
// hold-to-repeat; encoder no longer moves it), KEY0 = rotate (main action
// button), BTN1 = hold for soft drop, BTN2 = hard drop, ENC_SW held
// (~500ms) = pause. KEY0 only returns to the menu from the pause screen
// (or from GAME_OVER) — see tetrisWantsExit().
#include "tetris.h"
#include "hardware.h"
#include "palette.h"
#include "sound.h"
#include "tetris_sprites.h"
#include <Preferences.h>

namespace {

Preferences prefs;

// ================= Board geometry =================
const int BOARD_COLS = 10;
const int BOARD_ROWS = 20;
const int CELL = 14;
const int BOARD_LEFT = 8;
const int BOARD_TOP = 26;
const int BOARD_W = BOARD_COLS * CELL;
const int BOARD_H = BOARD_ROWS * CELL;
const int HUD_H = 20;
const int SIDEBAR_X = BOARD_LEFT + BOARD_W + 12;

int cellX(int c) { return BOARD_LEFT + c * CELL; }
int cellY(int r) { return BOARD_TOP + r * CELL; }

// Horizontal movement is thumbstick-only (encoder no longer moves the
// piece, per user request). DAS-style (delayed auto-shift): crossing the
// trigger threshold shifts once immediately, then repeats at a fixed
// interval for as long as the stick stays past it — same feel as holding
// a D-pad direction in classic Tetris.
int thumbShiftDir = 0;
unsigned long thumbShiftLastMs = 0;
const float THUMB_SHIFT_TRIGGER = 0.5f;
const unsigned long THUMB_SHIFT_REPEAT_MS = 130;

// ================= Palette =================
// Per-piece-type colors now live as bitmaps (tetris_sprites.h) — the
// modern "Guideline" 7-color scheme (I=cyan/O=yellow/T=purple/S=green/
// Z=red/J=blue/L=orange), which is what's actually recognized as "the
// Tetris colors" today even though the real 1989 NES cartridge cycled a
// 3-color palette per 10 levels instead of a fixed color per shape — see
// tools/make_tetris_sprites.py. GHOST_COLOR stays a flat color: the ghost
// piece is a simple outline, not a sprite.
uint16_t GHOST_COLOR;

const uint16_t* const CELL_SPRITES[7] = {
  TE_SPR_CELL_I, TE_SPR_CELL_O, TE_SPR_CELL_T, TE_SPR_CELL_S,
  TE_SPR_CELL_Z, TE_SPR_CELL_J, TE_SPR_CELL_L,
};
const uint16_t* const PREVCELL_SPRITES[7] = {
  TE_SPR_PREVCELL_I, TE_SPR_PREVCELL_O, TE_SPR_PREVCELL_T, TE_SPR_PREVCELL_S,
  TE_SPR_PREVCELL_Z, TE_SPR_PREVCELL_J, TE_SPR_PREVCELL_L,
};

void initGamePalette() {
  GHOST_COLOR = rgb565(90, 96, 120);
}

// ================= Piece shapes =================
// One base (rotation 0) 4x4 bitmask per piece; other rotations are derived
// at runtime via rotateCW, which avoids hand-transcribing 4 tables per piece.
const uint16_t BASE_SHAPE[7] = {
  0x0F00, // I
  0x0660, // O
  0x04E0, // T
  0x06C0, // S
  0x0C60, // Z
  0x08E0, // J
  0x02E0, // L
};

bool shapeCellBit(uint16_t shape, int row, int col) {
  int bit = 15 - (row * 4 + col);
  return (shape >> bit) & 1;
}

uint16_t rotateCW(uint16_t shape) {
  uint16_t result = 0;
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (shapeCellBit(shape, r, c)) {
        int nr = c, nc = 3 - r;
        int nbit = 15 - (nr * 4 + nc);
        result |= (1 << nbit);
      }
    }
  }
  return result;
}

uint16_t pieceShape(int type, int rot) {
  uint16_t s = BASE_SHAPE[type];
  for (int i = 0; i < rot; i++) s = rotateCW(s);
  return s;
}

// ================= Board state =================
uint8_t board[BOARD_ROWS][BOARD_COLS]; // 0 = empty, else pieceType+1

bool canPlace(int type, int rot, int row, int col) {
  uint16_t shape = pieceShape(type, rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!shapeCellBit(shape, r, c)) continue;
      int br = row + r, bc = col + c;
      if (bc < 0 || bc >= BOARD_COLS || br >= BOARD_ROWS) return false;
      if (br >= 0 && board[br][bc] != 0) return false;
    }
  }
  return true;
}

// ================= Drawing =================
void drawBoardCell(int row, int col, int typeIdx) {
  tft->drawRGBBitmap(cellX(col), cellY(row), CELL_SPRITES[typeIdx], CELL, CELL);
}
void eraseBoardCell(int row, int col) {
  tft->fillRect(cellX(col), cellY(row), CELL, CELL, BG);
}
void drawGhostCell(int row, int col) {
  int x = cellX(col), y = cellY(row);
  tft->drawRect(x, y, CELL - 1, CELL - 1, GHOST_COLOR);
}

void drawShapeCells(int type, int rot, int row, int col, int typeIdx) {
  uint16_t shape = pieceShape(type, rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!shapeCellBit(shape, r, c)) continue;
      int br = row + r, bc = col + c;
      if (br < 0) continue;
      drawBoardCell(br, bc, typeIdx);
    }
  }
}
void drawGhostShapeCells(int type, int rot, int row, int col) {
  uint16_t shape = pieceShape(type, rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!shapeCellBit(shape, r, c)) continue;
      int br = row + r, bc = col + c;
      if (br < 0) continue;
      drawGhostCell(br, bc);
    }
  }
}
void eraseShapeCells(int type, int rot, int row, int col) {
  uint16_t shape = pieceShape(type, rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!shapeCellBit(shape, r, c)) continue;
      int br = row + r, bc = col + c;
      if (br < 0) continue;
      eraseBoardCell(br, bc);
    }
  }
}

void drawBoard() {
  for (int r = 0; r < BOARD_ROWS; r++) {
    for (int c = 0; c < BOARD_COLS; c++) {
      if (board[r][c]) drawBoardCell(r, c, board[r][c] - 1);
      else eraseBoardCell(r, c);
    }
  }
}

// ================= Game state =================
enum GameState { PLAYING, PAUSED, GAME_OVER };
GameState state = PLAYING;
GameState stateBeforePause = PLAYING;

// ENC_SW held (~500ms) pauses — same convention as R-Type/Arkanoid; ENC_SW
// is reserved for infrequent actions since it's awkward to press.
bool prevEncSwHeld = false;
unsigned long encSwHeldSinceMs = 0;
bool pauseLatched = false;
const unsigned long PAUSE_HOLD_MS = 500;

int curType, curRot, curRow, curCol;
int nextType;
int lastType = -1, lastRot = -99, lastRow = -99, lastCol = -99, lastGhostRow = -99;

int score = 0, level = 1, lines = 0, highScore = 0;
int lastDrawnScore = -1, lastDrawnLevel = -1, lastDrawnLines = -1;
unsigned long lastDropMs = 0;

void loadHighScore() {
  prefs.begin("tetris", true);
  highScore = prefs.getInt("hi", 0);
  prefs.end();
}
void saveHighScoreIfNeeded() {
  if (score > highScore) {
    highScore = score;
    prefs.begin("tetris", false);
    prefs.putInt("hi", highScore);
    prefs.end();
  }
}

void drawHUD(bool force) {
  tft->setTextSize(1);
  if (force || score != lastDrawnScore) {
    tft->fillRect(SIDEBAR_X, 96, SCREEN_W - SIDEBAR_X - 4, 20, BG);
    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(SIDEBAR_X, 96);
    tft->print("SCORE");
    tft->setTextColor(TITLE_COLOR);
    tft->setCursor(SIDEBAR_X, 108);
    tft->printf("%06d", score);
    lastDrawnScore = score;
  }
  if (force || level != lastDrawnLevel) {
    tft->fillRect(SIDEBAR_X, 130, SCREEN_W - SIDEBAR_X - 4, 20, BG);
    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(SIDEBAR_X, 130);
    tft->print("LEVEL");
    tft->setTextColor(TITLE_COLOR);
    tft->setCursor(SIDEBAR_X, 142);
    tft->printf("%d", level);
    lastDrawnLevel = level;
  }
  if (force || lines != lastDrawnLines) {
    tft->fillRect(SIDEBAR_X, 164, SCREEN_W - SIDEBAR_X - 4, 20, BG);
    tft->setTextColor(TEXT_COLOR);
    tft->setCursor(SIDEBAR_X, 164);
    tft->print("LINES");
    tft->setTextColor(TITLE_COLOR);
    tft->setCursor(SIDEBAR_X, 176);
    tft->printf("%d", lines);
    lastDrawnLines = lines;
  }
}

void drawNextPreview() {
  const int PREV_CELL = 11;
  const int PREV_X = SIDEBAR_X;
  const int PREV_Y = 40;
  tft->fillRect(PREV_X - 2, PREV_Y - 2, PREV_CELL * 4 + 4, PREV_CELL * 4 + 4, BG);
  tft->drawRect(PREV_X - 2, PREV_Y - 2, PREV_CELL * 4 + 4, PREV_CELL * 4 + 4, HUD_LINE);
  uint16_t shape = pieceShape(nextType, 0);
  const uint16_t* spr = PREVCELL_SPRITES[nextType];
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!shapeCellBit(shape, r, c)) continue;
      int x = PREV_X + c * PREV_CELL, y = PREV_Y + r * PREV_CELL;
      tft->drawRGBBitmap(x, y, spr, PREV_CELL, PREV_CELL);
    }
  }
}

void drawFrame() {
  tft->fillScreen(BG);
  tft->drawFastHLine(0, HUD_H, SCREEN_W, HUD_LINE);
  tft->drawRect(BOARD_LEFT - 2, BOARD_TOP - 2, BOARD_W + 4, BOARD_H + 4, HUD_LINE);
  tft->setTextSize(1);
  tft->setTextColor(TEXT_COLOR);
  tft->setCursor(SIDEBAR_X, 26);
  tft->print("NEXT");
}

void drawPauseOverlay() {
  tft->fillRect(20, 130, SCREEN_W - 40, 60, darken(BG, -6));
  tft->drawRect(20, 130, SCREEN_W - 40, 60, TITLE_COLOR);
  drawCentered("PAUSED", 145, 2, TITLE_COLOR);
  drawCentered("ENC:RESUME  KEY0:EXIT", 172, 1, TEXT_COLOR);
}

void drawGameOverScreen() {
  tft->fillScreen(BG);
  drawCentered("GAME OVER", 90, 3, WARN_COLOR);
  char buf[24];
  snprintf(buf, sizeof(buf), "SCORE: %06d", score);
  drawCentered(buf, 140, 2, TEXT_COLOR);
  snprintf(buf, sizeof(buf), "LEVEL %d   LINES %d", level, lines);
  drawCentered(buf, 168, 1, TEXT_COLOR);
  if (score >= highScore && score > 0) {
    drawCentered("NEW HIGH SCORE!", 190, 1, TITLE_COLOR);
  } else {
    snprintf(buf, sizeof(buf), "HIGH SCORE: %06d", highScore);
    drawCentered(buf, 190, 1, TEXT_COLOR);
  }
  drawCentered("PRESS KNOB: PLAY AGAIN", 230, 1, TITLE_COLOR);
  drawCentered("KEY0: MENU", 248, 1, TEXT_COLOR);
}

void triggerGameOver() {
  saveHighScoreIfNeeded();
  state = GAME_OVER;
  drawGameOverScreen();
  sfxGameOver();
  Serial.println("Tetris: game over");
}

int computeGhostRow() {
  int gr = curRow;
  while (canPlace(curType, curRot, gr + 1, curCol)) gr++;
  return gr;
}

void spawnNextPiece() {
  curType = nextType;
  curRot = 0;
  curRow = -2;
  curCol = 3;
  nextType = random(0, 7);
  lastRow = -99; // force a fresh draw of the new piece next update
  drawNextPreview();
  if (!canPlace(curType, curRot, curRow, curCol)) {
    triggerGameOver();
  }
}

int clearLines() {
  int cleared = 0;
  for (int r = BOARD_ROWS - 1; r >= 0; r--) {
    bool full = true;
    for (int c = 0; c < BOARD_COLS; c++) {
      if (board[r][c] == 0) { full = false; break; }
    }
    if (full) {
      cleared++;
      for (int rr = r; rr > 0; rr--)
        for (int c = 0; c < BOARD_COLS; c++)
          board[rr][c] = board[rr - 1][c];
      for (int c = 0; c < BOARD_COLS; c++) board[0][c] = 0;
      r++; // re-check this row index since rows shifted down into it
    }
  }
  if (cleared > 0) {
    lines += cleared;
    const int base[5] = { 0, 40, 100, 300, 1200 };
    score += base[cleared] * level;
    level = 1 + lines / 10;
    Serial.printf("Cleared %d line(s), score=%d, level=%d\n", cleared, score, level);
  }
  return cleared;
}

// Debug aid: dumps the logical board as ASCII since we have no way to read
// pixels back off this display (no MISO wire on this module). Uppercase =
// locked block, lowercase = the active falling piece, '+' = ghost preview.
void printBoardDebug() {
  static const char PIECE_LETTERS[7] = { 'I', 'O', 'T', 'S', 'Z', 'J', 'L' };
  uint16_t shape = pieceShape(curType, curRot);
  int ghostRow = computeGhostRow();
  Serial.println("+----------+");
  for (int r = 0; r < BOARD_ROWS; r++) {
    Serial.print('|');
    for (int c = 0; c < BOARD_COLS; c++) {
      char ch = board[r][c] ? PIECE_LETTERS[board[r][c] - 1] : '.';
      int pr = r - curRow, pc = c - curCol;
      bool pieceCell = (pr >= 0 && pr < 4 && pc >= 0 && pc < 4 && shapeCellBit(shape, pr, pc));
      if (pieceCell) {
        ch = PIECE_LETTERS[curType] + 32; // lowercase
      } else if (ch == '.') {
        int gr = r - ghostRow;
        if (pc >= 0 && pc < 4 && gr >= 0 && gr < 4 && shapeCellBit(shape, gr, pc)) ch = '+';
      }
      Serial.print(ch);
    }
    Serial.println('|');
  }
  Serial.printf("+----------+ piece=%c rot=%d row=%d col=%d ghost=%d score=%d lines=%d lvl=%d\n",
                PIECE_LETTERS[curType], curRot, curRow, curCol, ghostRow, score, lines, level);
}

void lockPiece() {
  bool blockedOut = false;
  uint16_t shape = pieceShape(curType, curRot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!shapeCellBit(shape, r, c)) continue;
      int br = curRow + r, bc = curCol + c;
      if (br < 0) { blockedOut = true; continue; }
      board[br][bc] = curType + 1;
    }
  }
  if (blockedOut) {
    triggerGameOver();
    return;
  }
  int cleared = clearLines();
  // Always resync the screen with the board array here: the piece may have
  // locked at a position that was never actually drawn yet (e.g. right
  // after a hard drop that skipped straight past several rows, or a
  // shift/rotate applied the same tick gravity locked it) — without this,
  // those cells become solid-but-invisible "phantom" blocks.
  drawBoard();
  drawHUD(false);
  if (cleared > 0) sfxLineClear(cleared);
  else sfxBreak();
  spawnNextPiece();
  printBoardDebug();
}

void tryShiftHorizontal(int dir) {
  if (canPlace(curType, curRot, curRow, curCol + dir)) curCol += dir;
}

void tryRotate() {
  int newRot = (curRot + 1) % 4;
  const int kicks[] = { 0, -1, 1, -2, 2 };
  for (int k : kicks) {
    if (canPlace(curType, newRot, curRow, curCol + k)) {
      curCol += k;
      curRot = newRot;
      sfxHit();
      return;
    }
  }
}

void hardDrop() {
  int dist = 0;
  while (canPlace(curType, curRot, curRow + 1, curCol)) { curRow++; dist++; }
  score += dist * 2;
  lockPiece();
}

void gravityStep() {
  if (canPlace(curType, curRot, curRow + 1, curCol)) curRow++;
  else lockPiece();
}

unsigned long dropIntervalForLevel(int lvl) {
  long iv = 700 - (long)(lvl - 1) * 55;
  if (iv < 120) iv = 120;
  return (unsigned long)iv;
}

void redrawPieceIfMoved() {
  if (curRow == lastRow && curCol == lastCol && curRot == lastRot && curType == lastType) return;
  if (lastRow > -99) {
    eraseShapeCells(lastType, lastRot, lastGhostRow, lastCol);
    eraseShapeCells(lastType, lastRot, lastRow, lastCol);
  }
  int ghostRow = computeGhostRow();
  drawGhostShapeCells(curType, curRot, ghostRow, curCol);
  drawShapeCells(curType, curRot, curRow, curCol, curType);
  lastRow = curRow;
  lastCol = curCol;
  lastRot = curRot;
  lastType = curType;
  lastGhostRow = ghostRow;
}

void startNewGame() {
  for (int r = 0; r < BOARD_ROWS; r++)
    for (int c = 0; c < BOARD_COLS; c++)
      board[r][c] = 0;
  score = 0;
  lines = 0;
  level = 1;
  curType = random(0, 7);
  nextType = random(0, 7);
  curRot = 0;
  curRow = -2;
  curCol = 3;
  lastRow = -99;
  lastGhostRow = -99;
  lastDropMs = millis();
  thumbShiftDir = 0;
  pauseLatched = false;
  prevEncSwHeld = false;

  drawFrame();
  drawBoard();
  drawNextPreview();
  drawHUD(true);
  state = PLAYING;
}

} // namespace

void tetrisInit() {
  initGamePalette();
  loadHighScore();
  startNewGame();
  Serial.printf("Tetris ready. High score: %d\n", highScore);
}

void tetrisUpdate(const InputState& in, float dt) {
  switch (state) {
    case PLAYING: {
      int dir = 0;
      if (in.thumbX > THUMB_SHIFT_TRIGGER) dir = 1;
      else if (in.thumbX < -THUMB_SHIFT_TRIGGER) dir = -1;
      if (dir != thumbShiftDir) {
        thumbShiftDir = dir;
        if (dir != 0) { tryShiftHorizontal(dir); thumbShiftLastMs = millis(); }
      } else if (dir != 0 && millis() - thumbShiftLastMs >= THUMB_SHIFT_REPEAT_MS) {
        tryShiftHorizontal(dir);
        thumbShiftLastMs = millis();
      }
      if (state == PLAYING && in.key0Edge) tryRotate();
      if (state == PLAYING && in.btn2Edge) hardDrop();

      if (state == PLAYING) {
        unsigned long now = millis();
        unsigned long interval = dropIntervalForLevel(level);
        if (in.btn1Held) interval = max(30UL, interval / 6);
        if (now - lastDropMs >= interval) {
          gravityStep();
          lastDropMs = now;
        }
      }
      if (state == PLAYING) redrawPieceIfMoved();

      if (state == PLAYING) {
        if (in.encSwHeld && !prevEncSwHeld) { encSwHeldSinceMs = millis(); pauseLatched = false; }
        if (in.encSwHeld && !pauseLatched && millis() - encSwHeldSinceMs >= PAUSE_HOLD_MS) {
          pauseLatched = true;
          stateBeforePause = state;
          state = PAUSED;
          drawPauseOverlay();
          sfxUiMove();
        }
      }
      prevEncSwHeld = in.encSwHeld;
      break;
    }
    case PAUSED:
      prevEncSwHeld = in.encSwHeld;
      if (in.encSwEdge) {
        state = stateBeforePause;
        pauseLatched = false;
        drawFrame();
        drawBoard();
        drawNextPreview();
        drawHUD(true);
        // Not redrawPieceIfMoved(): the piece's row/col/rot haven't changed
        // since before pausing, so that would (correctly, by its own logic)
        // skip drawing anything — but drawBoard() just wiped it, so it
        // needs to be force-drawn instead, same as tetrisRenderSnapshot's
        // default case does for the same reason.
        {
          int ghostRow = computeGhostRow();
          drawGhostShapeCells(curType, curRot, ghostRow, curCol);
          drawShapeCells(curType, curRot, curRow, curCol, curType);
          lastRow = curRow; lastCol = curCol; lastRot = curRot; lastType = curType; lastGhostRow = ghostRow;
        }
        sfxUiMove();
      }
      break;
    case GAME_OVER:
      if (in.encSwEdge) {
        startNewGame();
        sfxUiConfirm();
      }
      break;
  }
}

void tetrisOnExit() {
  saveHighScoreIfNeeded();
}

// Re-renders whatever the current game state would show, onto whatever
// `tft` currently points to (used for screenshot capture — see main.cpp).
void tetrisRenderSnapshot() {
  switch (state) {
    case GAME_OVER:
      drawGameOverScreen();
      break;
    case PAUSED:
    default: // PLAYING
      drawFrame();
      drawBoard();
      drawNextPreview();
      drawHUD(true);
      {
        int ghostRow = computeGhostRow();
        drawGhostShapeCells(curType, curRot, ghostRow, curCol);
        drawShapeCells(curType, curRot, curRow, curCol, curType);
      }
      if (state == PAUSED) drawPauseOverlay();
      break;
  }
}

// KEY0 is now the main action button (rotate), so it can't also mean
// "back to menu" during play — only from the pause screen, or from
// GAME_OVER (which already has no conflicting use for KEY0).
bool tetrisWantsExit(const InputState& in) {
  return (state == PAUSED || state == GAME_OVER) && in.key0Edge;
}
