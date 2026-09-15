#include "menu.h"
#include "apps.h"
#include "hardware.h"
#include "palette.h"
#include "sound.h"

namespace {
  int selection = 0;
  int lastDrawnSelection = -2;
  long encAccum = 0;
  unsigned long lastMoveMs = 0;
  // Require several net ticks in the same direction, and a minimum time
  // between moves, before flipping the highlighted item. With only a
  // couple of menu entries a single tick is enough to flip the selection,
  // so this needs a much bigger deadzone than in-game paddle/piece control.
  const long ENC_STEP_THRESHOLD = 4;
  const unsigned long MOVE_COOLDOWN_MS = 200;

  // Thumbstick Y also moves the selection (up = previous, down = next),
  // sharing the same cooldown as the encoder above. A push has to return
  // near center before it can trigger another move, so holding the stick
  // over doesn't repeatedly flip the selection every frame.
  bool thumbMoveLatched = false;
  const float THUMB_MENU_TRIGGER = 0.6f;
  const float THUMB_MENU_RELEASE = 0.3f;

  const int ITEM_H = 40;
  const int ITEM_W = SCREEN_W - 28;
  const int LIST_TOP = 90;

  void drawItem(int idx, bool selected) {
    int y = LIST_TOP + idx * ITEM_H;
    uint16_t bg = selected ? lighten(BG, 10) : BG;
    tft->fillRect(14, y, ITEM_W, ITEM_H - 6, BG); // clear full slot first (handles shrinking text)
    tft->fillRoundRect(14, y, ITEM_W, ITEM_H - 6, 4, bg);
    tft->drawRoundRect(14, y, ITEM_W, ITEM_H - 6, 4, selected ? TITLE_COLOR : darken(bg, 6));

    uint16_t textColor = GAMES[idx].available ? TEXT_COLOR : darken(TEXT_COLOR, 12);
    tft->setTextSize(2);
    tft->setTextColor(textColor, bg);
    tft->setCursor(26, y + 8);
    tft->print(GAMES[idx].name);

    if (!GAMES[idx].available) {
      tft->setTextSize(1);
      tft->setTextColor(darken(TEXT_COLOR, 20), bg);
      tft->setCursor(26, y + 26);
      tft->print("COMING SOON");
    }
  }

  void drawMenu() {
    tft->fillScreen(BG);
    drawCentered("GAME SELECT", 40, 2, TITLE_COLOR);
    for (int i = 0; i < GAME_COUNT; i++) drawItem(i, i == selection);
    drawCentered("STICK/TURN: CHOOSE  KEY0/ENC: PLAY", SCREEN_H - 24, 1, TEXT_COLOR);
    lastDrawnSelection = selection;
  }
}

void menuInit() {
  selection = 0;
  encAccum = 0;
  lastMoveMs = 0;
  thumbMoveLatched = false;
  drawMenu();
}

int menuUpdate(const InputState& in) {
  unsigned long now = millis();
  if (now - lastMoveMs < MOVE_COOLDOWN_MS) {
    encAccum = 0; // drop ticks that arrive during the cooldown entirely
  } else {
    encAccum += in.encDelta;
    if (encAccum >= ENC_STEP_THRESHOLD) {
      selection = (selection + 1) % GAME_COUNT;
      encAccum = 0;
      lastMoveMs = now;
    } else if (encAccum <= -ENC_STEP_THRESHOLD) {
      selection = (selection - 1 + GAME_COUNT) % GAME_COUNT;
      encAccum = 0;
      lastMoveMs = now;
    }
  }

  if (fabsf(in.thumbY) < THUMB_MENU_RELEASE) thumbMoveLatched = false;
  if (!thumbMoveLatched && now - lastMoveMs >= MOVE_COOLDOWN_MS && fabsf(in.thumbY) >= THUMB_MENU_TRIGGER) {
    thumbMoveLatched = true;
    lastMoveMs = now;
    // Raw thumbY reads positive when the stick is pushed up (matches the
    // sign convention already established/inverted for R-Type); up moves
    // to the previous item, down to the next.
    if (in.thumbY > 0) selection = (selection - 1 + GAME_COUNT) % GAME_COUNT;
    else selection = (selection + 1) % GAME_COUNT;
  }

  if (selection != lastDrawnSelection) {
    int prev = lastDrawnSelection;
    lastDrawnSelection = selection;
    if (prev >= 0 && prev < GAME_COUNT) drawItem(prev, false);
    drawItem(selection, true);
    if (prev != -2) sfxUiMove();
  }

  if ((in.key0Edge || in.encSwEdge) && GAMES[selection].available) {
    sfxUiConfirm();
    return selection;
  }
  return -1;
}

// Used for screenshot capture (see main.cpp) — re-renders the menu onto
// whatever `tft` currently points to.
void menuRenderSnapshot() {
  drawMenu();
}
