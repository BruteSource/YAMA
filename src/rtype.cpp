// R-Type III (The Third Lightning) homage — now a PORTRAIT vertical
// shooter (originally landscape/horizontal; switched so the thumbstick,
// which is mounted for portrait use, works naturally — see README).
// Phase 1: Stage 1 ("Catapult Dimension" — drifting space junk) only.
// See README.md for the full control scheme; short version:
//   encoder rotate + thumbstick X = ship X (left/right, full-width dodge),
//   BTN1/BTN2 held + thumbstick Y = ship Y (forward/back, limited range),
//   KEY0 hold-then-release = charge/fire (also exits from the pause screen),
//   ENC_SW tap = cycle weapon, ENC_SW held = pause.
// Ship flies "up" the screen: enemies/debris spawn near the top and drift
// down, player bullets fire up. Runs in ordinary portrait (same
// orientation as every other game) — no hardwareSetOrientation() call
// needed for this game anymore; still uses its own RT_W/RT_H constants
// rather than the shared SCREEN_W/SCREEN_H just to keep this file
// self-contained.
#include "rtype.h"
#include "hardware.h"
#include "palette.h"
#include "sound.h"
#include "rtype_sprites.h"
#include <Preferences.h>
#include <math.h>

namespace {

Preferences prefs;

// ================= Screen geometry =================
const int RT_W = 240;
const int RT_H = 320;
const int HUD_H = 16;
const int PLAY_TOP = HUD_H;

// ================= Palette =================
uint16_t SHIP_BLUE, SHIP_BLUE_DARK, SHIP_WHITE, SHIP_RED, SHIP_CANOPY, SHIP_ACCENT;
uint16_t FORCE_COLOR, FORCE_RIM, FORCE_GLOW;
uint16_t BULLET_WEAK, BULLET_CHARGED, BULLET_FORCE;
uint16_t ENEMY_DRIFTER, ENEMY_DRIFTER_DARK, ENEMY_EYE, ENEMY_TURRET, ENEMY_BULLET_COLOR;
uint16_t DEBRIS_COLOR, DEBRIS_DARK, DEBRIS_LIGHT;
uint16_t STAR_FAR, STAR_NEAR;
uint16_t CAPSULE_SPEED_COLOR, CAPSULE_WEAPON_COLOR;
uint16_t BOSS_COLOR, BOSS_CORE, BOSS_ACCENT, BOSS_HP_BAR;
uint16_t RT_BG;

void initGamePalette() {
  RT_BG = rgb565(4, 6, 16);
  // The real R-9 is white-hulled with a blue stripe, red fin tips, and a
  // yellow accent — not blue-hulled as an earlier pass here had it.
  SHIP_WHITE     = rgb565(230, 236, 245);
  SHIP_BLUE      = rgb565(60, 150, 230);
  SHIP_BLUE_DARK = darken(SHIP_BLUE, 10);
  SHIP_RED       = rgb565(220, 60, 70);
  SHIP_CANOPY    = rgb565(110, 190, 250);
  SHIP_ACCENT    = rgb565(255, 205, 50);

  FORCE_COLOR = rgb565(90, 180, 255);
  FORCE_RIM   = darken(FORCE_COLOR, 16);
  FORCE_GLOW  = lighten(FORCE_COLOR, 18);

  BULLET_WEAK    = rgb565(255, 235, 120);
  BULLET_CHARGED = rgb565(140, 255, 210);
  BULLET_FORCE   = rgb565(255, 180, 90);

  // Organic/biomechanical (Bydo-style) look: mottled body, one big eye.
  ENEMY_DRIFTER      = rgb565(170, 70, 180);
  ENEMY_DRIFTER_DARK = darken(ENEMY_DRIFTER, 14);
  ENEMY_EYE          = rgb565(230, 220, 60);
  ENEMY_TURRET       = rgb565(120, 130, 150);
  ENEMY_BULLET_COLOR = rgb565(255, 90, 110);

  DEBRIS_COLOR = rgb565(100, 105, 120);
  DEBRIS_DARK  = darken(DEBRIS_COLOR, 12);
  DEBRIS_LIGHT = lighten(DEBRIS_COLOR, 12);

  STAR_FAR  = rgb565(70, 75, 100);
  STAR_NEAR = rgb565(180, 190, 220);

  CAPSULE_SPEED_COLOR  = rgb565(90, 230, 140);
  CAPSULE_WEAPON_COLOR = rgb565(230, 200, 60);

  BOSS_COLOR  = rgb565(130, 100, 150);
  BOSS_CORE   = rgb565(255, 60, 60);
  BOSS_ACCENT = rgb565(255, 210, 80);
  BOSS_HP_BAR = rgb565(220, 70, 90);
}

// Local text-centering helper: palette.h's drawCentered() hardcodes the
// portrait SCREEN_W (240), which would mis-center on this 320-wide screen.
void rtDrawCentered(const char* s, int y, uint8_t size, uint16_t color) {
  int16_t x1, y1; uint16_t w, h;
  tft->setTextSize(size);
  tft->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  tft->setTextColor(color);
  tft->setCursor((RT_W - (int)w) / 2, y);
  tft->print(s);
}

// ================= Game state =================
enum State { PLAYING, BOSS_INTRO, BOSS_FIGHT, PAUSED, GAME_OVER, STAGE_CLEAR };
State state = PLAYING;

int score = 0, highScore = 0, lives = 3;
int weaponType = 0; // 0=normal, 1=spread, 2=laser
const char* WEAPON_NAMES[3] = { "NORMAL", "SPREAD", "LASER" };
float worldDistance = 0;
const float STAGE_LENGTH = 4200.0f;
unsigned long stageClearAtMs = 0;
const unsigned long STAGE_CLEAR_BANNER_MS = 3500;
unsigned long bossIntroUntilMs = 0;
const unsigned long BOSS_INTRO_MS = 2200;
const char* BOSS_NAME = "GUARD RAY";

void loadHighScore() {
  prefs.begin("rtype", true);
  highScore = prefs.getInt("hi", 0);
  prefs.end();
}
void saveHighScoreIfNeeded() {
  if (score > highScore) {
    highScore = score;
    prefs.begin("rtype", false);
    prefs.putInt("hi", highScore);
    prefs.end();
  }
}

// ================= Ship =================
// Sprite bitmaps (ship, Force Pod, enemies, bullets, debris, capsules) are
// hand-authored pixel art baked into flash by tools/make_rtype_sprites.py —
// see rtype_sprites.h. Their "empty" pixels are pre-baked to RT_BG, so a
// plain tft->drawRGBBitmap() blit reads as a transparent sprite with no
// per-pixel masking needed; collision/erase boxes just use the sprite's own
// width/height constants.
const int SHIP_W = RT_SPR_SHIP_W, SHIP_H = RT_SPR_SHIP_H;
const int FORCE_R = 4; // Force Pod's collision radius (visual sprite is 12x12)
// X is now the full-width dodge axis (lateral); Y is a limited-range
// forward/back positioning axis near the bottom of the screen — the ship
// stays in the "safe" lower zone while hazards approach from above, same
// role the old landscape version's limited-range X axis played.
const int SHIP_X_MIN = 4;
const int SHIP_X_MAX = RT_W - SHIP_W - 4;
const int SHIP_Y_MIN = (int)(RT_H * 0.45f);
const int SHIP_Y_MAX = RT_H - SHIP_H - 3;
const int FORCE_DOCK_OFFSET_Y = -(RT_SPR_FORCE_POD_H - 6); // Force Pod's top edge, relative to ship y (above the nose)

float shipX, shipY;
unsigned long lastEncTickMs = 0;
bool invulnerable = false;
unsigned long invulnUntilMs = 0;
bool shipBlinkOn = true;
unsigned long lastBlinkMs = 0;

// Ship erase+redraw used to happen unconditionally every single frame, even
// while stationary — a background-color flash-and-redraw every ~10-20ms
// that was subtle against a frozen starfield but reads as a visible
// flicker now that the stars actually animate (see the star-scroll fix).
// Only touch the display when the ship's position or blink-visibility has
// actually changed since the last time it was drawn.
float lastDrawnShipX = -1000, lastDrawnShipY = -1000;
bool lastDrawnShipVisible = true;

void eraseShipAt(float ex, float ey) {
  tft->fillRect((int)ex - 1, (int)ey + FORCE_DOCK_OFFSET_Y - 1,
                SHIP_W + 2, SHIP_H - FORCE_DOCK_OFFSET_Y + 2, RT_BG);
}

void drawShipSprite() {
  int x = (int)shipX, y = (int)shipY;
  int midX = x + SHIP_W / 2;
  tft->drawRGBBitmap(x, y, RT_SPR_SHIP, RT_SPR_SHIP_W, RT_SPR_SHIP_H);
  tft->drawRGBBitmap(midX - RT_SPR_FORCE_POD_W / 2, y + FORCE_DOCK_OFFSET_Y,
                      RT_SPR_FORCE_POD, RT_SPR_FORCE_POD_W, RT_SPR_FORCE_POD_H);
}

// Always draws right now (used for full-screen redraws like drawPlayfield()
// and renderSnapshot(), where the ship's old position is meaningless).
void drawShip() {
  if (invulnerable && !shipBlinkOn) return; // blink out every other ~120ms while invulnerable
  drawShipSprite();
  lastDrawnShipX = shipX;
  lastDrawnShipY = shipY;
  lastDrawnShipVisible = true;
}

// Per-frame version: only erases+redraws if something actually changed.
void redrawShipIfNeeded() {
  bool visible = !(invulnerable && !shipBlinkOn);
  if (shipX == lastDrawnShipX && shipY == lastDrawnShipY && visible == lastDrawnShipVisible) return;
  eraseShipAt(lastDrawnShipX, lastDrawnShipY);
  if (visible) drawShipSprite();
  lastDrawnShipX = shipX;
  lastDrawnShipY = shipY;
  lastDrawnShipVisible = visible;
}

void updateShipMovement(const InputState& in, float dt) {
  const float ENC_SENS_BASE = 2.2f;
  const float ENC_SENS_MAX_BONUS = 5.0f;
  const float ENC_ACCEL_SCALE = 0.035f;
  const float KEY_SPEED_Y = 150.0f;

  if (in.encDelta != 0) {
    unsigned long now = millis();
    float sens = ENC_SENS_BASE;
    if (lastEncTickMs != 0) {
      float intervalSec = (now - lastEncTickMs) / 1000.0f;
      if (intervalSec > 0.001f) {
        float rate = fabsf((float)in.encDelta) / intervalSec;
        sens += constrain(rate * ENC_ACCEL_SCALE, 0.0f, ENC_SENS_MAX_BONUS);
      }
    }
    lastEncTickMs = now;
    shipX += in.encDelta * sens;
  }
  if (in.btn1Held) shipY += KEY_SPEED_Y * dt;
  if (in.btn2Held) shipY -= KEY_SPEED_Y * dt;

  // Thumbstick: full analog 2D movement, additive on top of the encoder/
  // button controls above (all keep working at once). Both axes read
  // inverted from the physical wiring during testing.
  const float THUMB_SPEED = 190.0f;
  const float THUMB_X_SIGN = 1.0f;
  const float THUMB_Y_SIGN = -1.0f;
  shipX += in.thumbX * THUMB_SPEED * dt * THUMB_X_SIGN;
  shipY += in.thumbY * THUMB_SPEED * dt * THUMB_Y_SIGN;
  shipX = constrain(shipX, (float)SHIP_X_MIN, (float)SHIP_X_MAX);
  shipY = constrain(shipY, (float)SHIP_Y_MIN, (float)SHIP_Y_MAX);
}

// ================= Player bullets =================
// Fired upward now (negative vy) instead of rightward — x is the fixed
// lateral firing position, y is the leading (top) edge that decreases.
struct PBullet { bool active; float x, y, vy; int kind; int pierce; };
const int MAX_PBULLETS = 16;
PBullet pbullets[MAX_PBULLETS];

void spawnPBullet(float x, float y, float vy, int kind, int pierce) {
  for (int i = 0; i < MAX_PBULLETS; i++) {
    if (!pbullets[i].active) {
      pbullets[i] = { true, x, y, vy, kind, pierce };
      return;
    }
  }
}

void pbulletSprite(int kind, int* w, int* h, const uint16_t** data) {
  if (kind == 1) { *w = RT_SPR_BULLET_CHARGED_W; *h = RT_SPR_BULLET_CHARGED_H; *data = RT_SPR_BULLET_CHARGED; return; }
  if (kind == 2) { *w = RT_SPR_BULLET_FORCE_W;   *h = RT_SPR_BULLET_FORCE_H;   *data = RT_SPR_BULLET_FORCE;   return; }
  *w = RT_SPR_BULLET_WEAK_W; *h = RT_SPR_BULLET_WEAK_H; *data = RT_SPR_BULLET_WEAK;
}

void eraseAndDrawPBullets(float dt) {
  for (int i = 0; i < MAX_PBULLETS; i++) {
    if (!pbullets[i].active) continue;
    PBullet& b = pbullets[i];
    int w, h; const uint16_t* data;
    pbulletSprite(b.kind, &w, &h, &data);
    tft->fillRect((int)b.x - w / 2 - 1, (int)b.y - 1, w + 2, h + 2, RT_BG);
    b.y += b.vy * dt;
    if (b.y < -h - 10) { b.active = false; continue; }
    tft->drawRGBBitmap((int)b.x - w / 2, (int)b.y, data, w, h);
  }
}

// ================= Enemy bullets =================
struct EBullet { bool active; float x, y, vx, vy; bool homing; };
const int MAX_EBULLETS = 16;
EBullet ebullets[MAX_EBULLETS];

void spawnEBullet(float x, float y, float vx, float vy, bool homing = false) {
  for (int i = 0; i < MAX_EBULLETS; i++) {
    if (!ebullets[i].active) {
      ebullets[i] = { true, x, y, vx, vy, homing };
      return;
    }
  }
}

void eraseAndDrawEBullets(float dt) {
  for (int i = 0; i < MAX_EBULLETS; i++) {
    if (!ebullets[i].active) continue;
    EBullet& b = ebullets[i];
    tft->fillRect((int)b.x - RT_SPR_ENEMY_BULLET_W / 2 - 1, (int)b.y - RT_SPR_ENEMY_BULLET_H / 2 - 1,
                  RT_SPR_ENEMY_BULLET_W + 2, RT_SPR_ENEMY_BULLET_H + 2, RT_BG);
    if (b.homing) {
      // Guard Ray's missiles: gently steer toward the ship's current X
      // rather than tracking perfectly, so they stay dodgeable.
      float targetVx = (shipX - b.x) * 1.8f;
      targetVx = constrain(targetVx, -70.0f, 70.0f);
      b.vx += (targetVx - b.vx) * constrain(dt * 3.0f, 0.0f, 1.0f);
    }
    b.x += b.vx * dt;
    b.y += b.vy * dt;
    if (b.x < -10 || b.x > RT_W + 10 || b.y > RT_H + 10) { b.active = false; continue; }
    tft->drawRGBBitmap((int)b.x - RT_SPR_ENEMY_BULLET_W / 2, (int)b.y - RT_SPR_ENEMY_BULLET_H / 2,
                        RT_SPR_ENEMY_BULLET, RT_SPR_ENEMY_BULLET_W, RT_SPR_ENEMY_BULLET_H);
  }
}

// ================= Particles (explosion "juice") =================
struct Particle { bool active; float x, y, vx, vy; unsigned long bornMs; uint16_t color; };
const int MAX_PARTICLES = 30;
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
    tft->fillRect((int)p.x, (int)p.y, 2, 2, RT_BG);
    if (now - p.bornMs > PARTICLE_LIFE_MS) { p.active = false; continue; }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.x < 0 || p.x > RT_W || p.y < PLAY_TOP || p.y > RT_H) { p.active = false; continue; }
    tft->fillRect((int)p.x, (int)p.y, 2, 2, p.color);
  }
}

// ================= Debris (destructible "space junk") =================
struct Debris { bool active; float x, y; int w, h; int hp; int turretIdx; int variant; };
const int MAX_DEBRIS = 6;
Debris debris[MAX_DEBRIS];

// 3 fixed hand-drawn "space junk" chunk variants, picked at random on spawn,
// instead of a continuously-random rectangle — a bitmap sprite needs a fixed
// size, and a handful of pre-drawn variants reads as more authentic anyway.
const int DEBRIS_VARIANT_COUNT = 3;
void debrisVariantSprite(int variant, int* w, int* h, const uint16_t** data) {
  switch (variant) {
    case 0: *w = RT_SPR_DEBRIS_0_W; *h = RT_SPR_DEBRIS_0_H; *data = RT_SPR_DEBRIS_0; break;
    case 1: *w = RT_SPR_DEBRIS_1_W; *h = RT_SPR_DEBRIS_1_H; *data = RT_SPR_DEBRIS_1; break;
    default: *w = RT_SPR_DEBRIS_2_W; *h = RT_SPR_DEBRIS_2_H; *data = RT_SPR_DEBRIS_2; break;
  }
}

// ================= Enemies =================
enum EnemyType { EN_DRIFTER, EN_TURRET };
struct Enemy {
  bool active; EnemyType type; float x, y, baseX, phase; int hp;
  unsigned long lastFireMs; int debrisIdx;
};
const int MAX_ENEMIES = 10;
Enemy enemies[MAX_ENEMIES];

void eraseDebrisAt(Debris& d) { tft->fillRect((int)d.x - 1, (int)d.y - 1, d.w + 2, d.h + 2, RT_BG); }
void drawDebrisAt(Debris& d) {
  int w, h; const uint16_t* data;
  debrisVariantSprite(d.variant, &w, &h, &data);
  tft->drawRGBBitmap((int)d.x, (int)d.y, data, w, h);
}

void eraseEnemyAt(Enemy& e) {
  if (e.type == EN_DRIFTER) {
    tft->fillRect((int)e.x - RT_SPR_DRIFTER_W / 2 - 1, (int)e.y - RT_SPR_DRIFTER_H / 2 - 1,
                  RT_SPR_DRIFTER_W + 2, RT_SPR_DRIFTER_H + 2, RT_BG);
  }
  // Turrets are drawn on top of their debris and erased as part of it.
}
void drawEnemyAt(Enemy& e) {
  int ex = (int)e.x, ey = (int)e.y;
  if (e.type == EN_DRIFTER) {
    tft->drawRGBBitmap(ex - RT_SPR_DRIFTER_W / 2, ey - RT_SPR_DRIFTER_H / 2,
                        RT_SPR_DRIFTER, RT_SPR_DRIFTER_W, RT_SPR_DRIFTER_H);
  } else {
    tft->drawRGBBitmap(ex - RT_SPR_TURRET_W / 2, ey - RT_SPR_TURRET_H / 2,
                        RT_SPR_TURRET, RT_SPR_TURRET_W, RT_SPR_TURRET_H);
  }
}

unsigned long lastEnemySpawnMs = 0, lastDebrisSpawnMs = 0;
const float SCROLL_SPEED = 55.0f;      // px/sec, world objects drift down at this rate
const float ENEMY_EXTRA_SPEED = 12.0f; // enemies drift slightly faster than plain scroll

void spawnDrifter() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      float x = random(14, RT_W - 14);
      enemies[i] = { true, EN_DRIFTER, x, -14.0f, x, (float)random(0, 628) / 100.0f, 2, 0, -1 };
      return;
    }
  }
}

void spawnDebrisWithMaybeTurret() {
  int slot = -1;
  for (int i = 0; i < MAX_DEBRIS; i++) if (!debris[i].active) { slot = i; break; }
  if (slot < 0) return;
  int variant = random(0, DEBRIS_VARIANT_COUNT);
  int w, h; const uint16_t* unusedData;
  debrisVariantSprite(variant, &w, &h, &unusedData);
  float x = random(4, RT_W - w - 4);
  debris[slot] = { true, x, -(float)h - 10.0f, w, h, 4, -1, variant };

  if (random(0, 100) < 55) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (!enemies[i].active) {
        enemies[i] = { true, EN_TURRET, x + w / 2.0f, debris[slot].y + h / 2.0f, x, 0, 3, millis(), slot };
        debris[slot].turretIdx = i;
        break;
      }
    }
  }
}

// ================= Capsules (power-ups) =================
struct Capsule { bool active; float x, y; int kind; }; // kind: 0=speed(unused stat, cosmetic score bonus), 1=weapon
const int MAX_CAPSULES = 4;
Capsule capsules[MAX_CAPSULES];

void maybeDropCapsule(float x, float y) {
  if (random(0, 100) >= 22) return;
  for (int i = 0; i < MAX_CAPSULES; i++) {
    if (!capsules[i].active) {
      capsules[i] = { true, x, y, random(0, 2) };
      return;
    }
  }
}

void eraseCapsuleAt(Capsule& c) {
  tft->fillRect((int)c.x - 1, (int)c.y - 1, RT_SPR_CAPSULE_SPEED_W + 2, RT_SPR_CAPSULE_SPEED_H + 2, RT_BG);
}
void drawCapsuleAt(Capsule& c) {
  const uint16_t* spr = c.kind == 0 ? RT_SPR_CAPSULE_SPEED : RT_SPR_CAPSULE_WEAPON;
  tft->drawRGBBitmap((int)c.x, (int)c.y, spr, RT_SPR_CAPSULE_SPEED_W, RT_SPR_CAPSULE_SPEED_H);
}

// ================= Stars (parallax backdrop) =================
struct Star { float x, y, speed; uint16_t color; };
const int MAX_STARS = 36;
Star stars[MAX_STARS];

void initStars() {
  for (int i = 0; i < MAX_STARS; i++) {
    stars[i].x = random(0, RT_W);
    stars[i].y = random(PLAY_TOP, RT_H);
    bool near = random(0, 2);
    stars[i].speed = near ? random(28, 42) : random(8, 18);
    stars[i].color = near ? STAR_NEAR : STAR_FAR;
  }
}

void eraseAndDrawStars(float dt) {
  for (int i = 0; i < MAX_STARS; i++) {
    Star& s = stars[i];
    tft->drawPixel((int)s.x, (int)s.y, RT_BG);
    s.y += s.speed * dt;
    if (s.y >= RT_H) {
      s.y = PLAY_TOP;
      s.x = random(0, RT_W);
    }
    tft->drawPixel((int)s.x, (int)s.y, s.color);
  }
}

// ================= Fire / charge (KEY0) =================
bool prevKey0Held = false;
unsigned long key0HeldSinceMs = 0;
const unsigned long CHARGE_FULL_MS = 900;

void fireWeapon(bool charged) {
  float noseX = shipX + SHIP_W / 2.0f;
  float noseY = shipY - 2;
  if (charged) {
    spawnPBullet(noseX, noseY, -260.0f, 1, 3);
    sfxHit();
  } else {
    const float VY = -220.0f;
    if (weaponType == 0) {
      spawnPBullet(noseX, noseY, VY, 0, 1);
    } else if (weaponType == 1) {
      spawnPBullet(noseX, noseY, VY, 0, 1);
      spawnPBullet(noseX - 6, noseY, VY, 0, 1);
      spawnPBullet(noseX + 6, noseY, VY, 0, 1);
    } else {
      spawnPBullet(noseX, noseY, VY * 1.3f, 0, 1);
    }
    sfxUiMove();
  }
}

unsigned long lastForceFireMs = 0;
const unsigned long FORCE_FIRE_INTERVAL_MS = 350;

void updateFireControl(const InputState& in) {
  if (in.key0Held && !prevKey0Held) key0HeldSinceMs = millis();
  if (!in.key0Held && prevKey0Held) {
    unsigned long heldFor = millis() - key0HeldSinceMs;
    fireWeapon(heldFor >= CHARGE_FULL_MS);
  }
  prevKey0Held = in.key0Held;

  unsigned long now = millis();
  if (now - lastForceFireMs >= FORCE_FIRE_INTERVAL_MS) {
    lastForceFireMs = now;
    spawnPBullet(shipX + SHIP_W / 2.0f, shipY + FORCE_DOCK_OFFSET_Y - 2, -200.0f, 2, 1);
  }
}

// ================= Weapon cycle / pause (ENC_SW) =================
bool prevEncSwHeld = false;
unsigned long encSwHeldSinceMs = 0;
bool pauseLatched = false;
const unsigned long PAUSE_HOLD_MS = 500;

State stateBeforePause = PLAYING;

void updateEncSwControl(const InputState& in) {
  if (in.encSwHeld && !prevEncSwHeld) { encSwHeldSinceMs = millis(); pauseLatched = false; }
  if (in.encSwHeld && !pauseLatched && millis() - encSwHeldSinceMs >= PAUSE_HOLD_MS
      && (state == PLAYING || state == BOSS_FIGHT)) {
    pauseLatched = true;
    stateBeforePause = state;
    state = PAUSED;
    sfxUiMove();
  }
  if (in.encSwEdge && (state == PLAYING || state == BOSS_FIGHT)) {
    weaponType = (weaponType + 1) % 3;
    sfxUiMove();
  }
  prevEncSwHeld = in.encSwHeld;
}

// ================= HUD =================
int lastHudScore = -1, lastHudWeapon = -1, lastHudLives = -1;

// `force=false` (used for the per-frame gameplay calls) skips the redraw
// entirely when score/weapon/lives haven't actually changed since last
// drawn — this strip was being wiped and redrawn every single frame
// regardless, which didn't help the "rolling" look. Every other call site
// (a full drawPlayfield() redraw, overlays, etc.) keeps the default
// `force=true` so it's never accidentally left blank after a screen wipe.
void drawHUD(bool force = true) {
  bool changed = force || score != lastHudScore || weaponType != lastHudWeapon || lives != lastHudLives;
  if (!changed) return;
  lastHudScore = score; lastHudWeapon = weaponType; lastHudLives = lives;
  tft->fillRect(0, 0, RT_W, HUD_H, RT_BG);
  tft->drawFastHLine(0, HUD_H - 1, RT_W, HUD_LINE);
  char buf[24];
  tft->setTextSize(1);
  tft->setTextColor(TEXT_COLOR);
  snprintf(buf, sizeof(buf), "SCORE %06d", score);
  tft->setCursor(2, 4);
  tft->print(buf);

  snprintf(buf, sizeof(buf), "WPN:%s", WEAPON_NAMES[weaponType]);
  tft->setCursor(82, 4);
  tft->print(buf);

  snprintf(buf, sizeof(buf), "LIVES %d", lives);
  tft->setCursor(172, 4);
  tft->print(buf);
}

// ================= Collision helpers =================
bool overlaps(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void killShip() {
  lives--;
  spawnParticles(shipX + SHIP_W / 2, shipY + SHIP_H / 2, SHIP_RED, 14);
  sfxBad();
  if (lives <= 0) {
    state = GAME_OVER;
    saveHighScoreIfNeeded();
  } else {
    invulnerable = true;
    invulnUntilMs = millis() + 2000;
  }
}

// ================= Stage 1 boss: "Guard Ray" =================
// Real R-Type III's Guard Ray flies in and out of the background lobbing
// shots, then settles onto the player's plane to fire homing missiles, and
// splits into three near the end of the fight. This board can't do the
// SNES's background-scaling trick, so "in the background" is approximated
// by patrolling further up (just further away, same size) before settling
// down closer to the player to fight properly — same beats, simplified.
struct BossUnit { bool active; float x, y, vy; float baseX, phase; int hp, maxHp; unsigned long lastFireMs; float radius; };
const int MAX_BOSS_UNITS = 3; // 1 while whole, 3 after it splits
BossUnit boss[MAX_BOSS_UNITS];
bool bossHasSplit = false;
bool bossSettled = false;
unsigned long bossSettleToggleMs = 0;
const int BOSS_MAIN_HP = 36;
const int BOSS_SPLIT_HP = 10;

int bossHpRemaining() {
  int hp = 0;
  for (auto& b : boss) if (b.active) hp += b.hp;
  return hp;
}
int bossHpMax() {
  return bossHasSplit ? BOSS_SPLIT_HP * MAX_BOSS_UNITS : BOSS_MAIN_HP;
}

void spawnBoss() {
  for (auto& b : boss) b.active = false;
  boss[0] = { true, RT_W / 2.0f, 60.0f, 18.0f, RT_W / 2.0f, 0, BOSS_MAIN_HP, BOSS_MAIN_HP, 0, 13 };
  bossHasSplit = false;
  bossSettled = false;
  bossSettleToggleMs = millis() + 2600;
}

// The BossUnit's `radius` field (13 whole, 8 split) doubles as which sprite
// variant to use — the two bitmaps are proportioned to match those hitbox
// sizes (see tools/make_rtype_sprites.py).
bool bossUnitIsMain(BossUnit& b) { return b.radius > 10; }

void eraseBossUnit(BossUnit& b) {
  int w = bossUnitIsMain(b) ? RT_SPR_GUARD_RAY_MAIN_W : RT_SPR_GUARD_RAY_SPLIT_W;
  int h = bossUnitIsMain(b) ? RT_SPR_GUARD_RAY_MAIN_H : RT_SPR_GUARD_RAY_SPLIT_H;
  tft->fillRect((int)b.x - w / 2 - 1, (int)b.y - h / 2 - 1, w + 2, h + 2, RT_BG);
}
void drawBossUnit(BossUnit& b) {
  int x = (int)b.x, y = (int)b.y;
  if (bossUnitIsMain(b)) {
    tft->drawRGBBitmap(x - RT_SPR_GUARD_RAY_MAIN_W / 2, y - RT_SPR_GUARD_RAY_MAIN_H / 2,
                        RT_SPR_GUARD_RAY_MAIN, RT_SPR_GUARD_RAY_MAIN_W, RT_SPR_GUARD_RAY_MAIN_H);
  } else {
    tft->drawRGBBitmap(x - RT_SPR_GUARD_RAY_SPLIT_W / 2, y - RT_SPR_GUARD_RAY_SPLIT_H / 2,
                        RT_SPR_GUARD_RAY_SPLIT, RT_SPR_GUARD_RAY_SPLIT_W, RT_SPR_GUARD_RAY_SPLIT_H);
  }
}

void drawBossHpBar() {
  int maxHp = bossHpMax();
  int hp = bossHpRemaining();
  int barW = 140, barH = 6;
  int x = RT_W / 2 - barW / 2, y = HUD_H + 4; // just under the HUD row, not on top of it
  tft->drawRect(x - 1, y - 1, barW + 2, barH + 2, TEXT_COLOR);
  tft->fillRect(x, y, barW, barH, RT_BG);
  int fillW = maxHp > 0 ? (barW * hp) / maxHp : 0;
  if (fillW > 0) tft->fillRect(x, y, fillW, barH, BOSS_HP_BAR);
}

// One unit's simple patrol-and-fire behavior. `speedMul`/`fireIntervalMs`
// let split parts feel faster/more aggressive than the whole boss.
void updateBossUnit(BossUnit& b, float dt, float speedMul, unsigned long fireIntervalMs, bool canSettle) {
  eraseBossUnit(b);

  bool settled = canSettle && bossSettled;
  if (!settled) {
    b.y += b.vy * speedMul * dt;
    float patrolMin = PLAY_TOP + 30.0f, patrolMax = 110.0f;
    if (b.y < patrolMin) { b.y = patrolMin; b.vy = fabsf(b.vy); }
    if (b.y > patrolMax) { b.y = patrolMax; b.vy = -fabsf(b.vy); }
  } else {
    float targetY = 150.0f; // settles down closer to the player, still short of the ship's own zone
    b.y += (targetY - b.y) * constrain(dt * 2.0f, 0.0f, 1.0f);
  }
  b.phase += dt * 1.6f;
  b.x = b.baseX + sinf(b.phase) * 44.0f;
  b.x = constrain(b.x, 14.0f, (float)(RT_W - 14));

  unsigned long now = millis();
  if (now - b.lastFireMs > fireIntervalMs) {
    b.lastFireMs = now;
    if (settled) {
      spawnEBullet(b.x, b.y + b.radius, 0.0f, 130.0f, true); // homing missile
    } else {
      // 3-bolt spread, lobbed from a distance.
      spawnEBullet(b.x, b.y + b.radius, -40.0f, 140.0f);
      spawnEBullet(b.x, b.y + b.radius, 0.0f, 150.0f);
      spawnEBullet(b.x, b.y + b.radius, 40.0f, 140.0f);
    }
  }

  drawBossUnit(b);
}

void splitBoss() {
  float x = boss[0].x, y = boss[0].y;
  bool wasActive = boss[0].active;
  eraseBossUnit(boss[0]);
  boss[0].active = false;
  if (!wasActive) return;
  float offsets[3] = { -34.0f, 0.0f, 34.0f };
  for (int i = 0; i < MAX_BOSS_UNITS; i++) {
    boss[i] = { true, x + offsets[i], y, (i % 2 == 0) ? -60.0f : 60.0f, x + offsets[i], (float)i * 2.0f, BOSS_SPLIT_HP, BOSS_SPLIT_HP, 0, 8 };
  }
  bossHasSplit = true;
  spawnParticles(x, y, BOSS_COLOR, 20);
  sfxBad();
}

void updateBossFight(float dt) {
  if (!bossHasSplit) {
    if (boss[0].active) updateBossUnit(boss[0], dt, 1.0f, 1500, true);
  } else {
    for (auto& b : boss) if (b.active) updateBossUnit(b, dt, 1.6f, 1100, false);
  }

  if (bossSettled && millis() > bossSettleToggleMs) { bossSettled = false; bossSettleToggleMs = millis() + 2600; }
  if (!bossSettled && !bossHasSplit && millis() > bossSettleToggleMs) { bossSettled = true; bossSettleToggleMs = millis() + 2200; }

  eraseAndDrawStars(dt);
  eraseAndDrawEBullets(dt);
  eraseAndDrawPBullets(dt);
  updateParticles(dt);
  drawBossHpBar();

  // Player bullets vs boss unit(s).
  for (int bi = 0; bi < MAX_PBULLETS; bi++) {
    if (!pbullets[bi].active) continue;
    PBullet& pb = pbullets[bi];
    for (auto& b : boss) {
      if (!b.active) continue;
      if (overlaps(pb.x - 2, pb.y - 2, 4, 4, b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2)) {
        b.hp--;
        pb.pierce--;
        if (b.hp <= 0) { b.active = false; eraseBossUnit(b); spawnParticles(b.x, b.y, BOSS_COLOR, 16); sfxBreak(); }
        if (pb.pierce <= 0) { pb.active = false; break; }
      }
    }
  }

  if (!bossHasSplit && boss[0].hp > 0 && boss[0].hp <= BOSS_MAIN_HP / 2) splitBoss();

  if (bossHpRemaining() <= 0) {
    score += 3000;
    saveHighScoreIfNeeded();
    state = STAGE_CLEAR;
    stageClearAtMs = millis();
    return;
  }

  if (invulnerable) {
    if (millis() >= invulnUntilMs) invulnerable = false;
  } else {
    for (auto& b : boss) {
      if (!b.active) continue;
      if (overlaps(shipX, shipY, SHIP_W, SHIP_H, b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2)) { killShip(); return; }
    }
  }
}

// ================= Reset / stage setup =================
void resetGame() {
  shipX = RT_W / 2.0f - SHIP_W / 2.0f; shipY = SHIP_Y_MAX - 10.0f;
  lives = 3; score = 0; weaponType = 0;
  worldDistance = 0;
  invulnerable = false;
  lastEncTickMs = 0;
  prevKey0Held = false;
  prevEncSwHeld = false;
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
  state = PLAYING;
}

void drawPlayfield() {
  tft->fillScreen(RT_BG);
  drawHUD();
  for (auto& s : stars) tft->drawPixel((int)s.x, (int)s.y, s.color);
  for (auto& d : debris) if (d.active) drawDebrisAt(d);
  for (auto& e : enemies) if (e.active) drawEnemyAt(e);
  for (auto& c : capsules) if (c.active) drawCapsuleAt(c);
  drawShip();
}

// ================= Main per-frame simulation =================
void updateWorld(float dt) {
  worldDistance += SCROLL_SPEED * dt;
  if (worldDistance >= STAGE_LENGTH) {
    // Clear the field of ordinary hazards before Guard Ray shows up.
    for (auto& e : enemies) if (e.active) { eraseEnemyAt(e); e.active = false; }
    for (auto& d : debris) if (d.active) { eraseDebrisAt(d); d.active = false; }
    for (auto& b : ebullets) if (b.active) {
      tft->fillRect((int)b.x - RT_SPR_ENEMY_BULLET_W / 2 - 1, (int)b.y - RT_SPR_ENEMY_BULLET_H / 2 - 1,
                    RT_SPR_ENEMY_BULLET_W + 2, RT_SPR_ENEMY_BULLET_H + 2, RT_BG);
      b.active = false;
    }
    state = BOSS_INTRO;
    bossIntroUntilMs = millis() + BOSS_INTRO_MS;
    tft->fillRect(0, PLAY_TOP, RT_W, RT_H - PLAY_TOP, RT_BG);
    rtDrawCentered(BOSS_NAME, RT_H / 2 - 10, 3, WARN_COLOR);
    rtDrawCentered("STAGE BOSS APPROACHING", RT_H / 2 + 18, 1, TEXT_COLOR);
    return;
  }

  unsigned long now = millis();
  if (now - lastDebrisSpawnMs > 2200) { lastDebrisSpawnMs = now; spawnDebrisWithMaybeTurret(); }
  if (now - lastEnemySpawnMs > 1400) { lastEnemySpawnMs = now; if (random(0, 100) < 70) spawnDrifter(); }

  // Debris (and any mounted turret) drift down together.
  for (int i = 0; i < MAX_DEBRIS; i++) {
    if (!debris[i].active) continue;
    Debris& d = debris[i];
    eraseDebrisAt(d);
    d.y += SCROLL_SPEED * dt;
    if (d.turretIdx >= 0 && enemies[d.turretIdx].active) {
      Enemy& t = enemies[d.turretIdx];
      t.x = d.x + d.w / 2.0f;
      t.y = d.y + d.h / 2.0f;
      if (now - t.lastFireMs > 1700 && t.y < RT_H && t.y > 0) {
        t.lastFireMs = now;
        float dx = shipX - t.x, dy = shipY - t.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1) spawnEBullet(t.x, t.y, dx / len * 90.0f, dy / len * 90.0f);
      }
    }
    if (d.y > RT_H) { d.active = false; if (d.turretIdx >= 0) enemies[d.turretIdx].active = false; continue; }
    drawDebrisAt(d);
  }

  // Drifters move down with a gentle lateral sine bob.
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active || enemies[i].type != EN_DRIFTER) continue;
    Enemy& e = enemies[i];
    eraseEnemyAt(e);
    e.y += (SCROLL_SPEED + ENEMY_EXTRA_SPEED) * dt;
    e.phase += dt * 2.2f;
    e.x = e.baseX + sinf(e.phase) * 18.0f;
    if (e.y > RT_H + 12) { e.active = false; continue; }
    drawEnemyAt(e);
  }

  eraseAndDrawStars(dt);
  eraseAndDrawEBullets(dt);
  eraseAndDrawPBullets(dt);
  updateParticles(dt);

  for (int i = 0; i < MAX_CAPSULES; i++) {
    if (!capsules[i].active) continue;
    Capsule& c = capsules[i];
    eraseCapsuleAt(c);
    c.y += SCROLL_SPEED * dt;
    if (c.y > RT_H + 12) { c.active = false; continue; }
    drawCapsuleAt(c);
  }
}

void checkCollisions() {
  // Player bullets vs enemies / debris.
  for (int bi = 0; bi < MAX_PBULLETS; bi++) {
    if (!pbullets[bi].active) continue;
    PBullet& b = pbullets[bi];
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (!enemies[i].active) continue;
      Enemy& e = enemies[i];
      float r = (e.type == EN_DRIFTER) ? 7 : 5;
      if (overlaps(b.x - 2, b.y - 2, 4, 4, e.x - r, e.y - r, r * 2, r * 2)) {
        e.hp--;
        b.pierce--;
        if (e.hp <= 0) {
          score += (e.type == EN_DRIFTER) ? 100 : 150;
          spawnParticles(e.x, e.y, e.type == EN_DRIFTER ? ENEMY_DRIFTER : ENEMY_TURRET, 10);
          eraseEnemyAt(e);
          e.active = false;
          if (e.debrisIdx >= 0 && debris[e.debrisIdx].active) debris[e.debrisIdx].turretIdx = -1;
          maybeDropCapsule(e.x, e.y);
          sfxBreak();
        }
        if (b.pierce <= 0) { b.active = false; break; }
      }
    }
    if (!b.active) continue;
    for (int i = 0; i < MAX_DEBRIS; i++) {
      if (!debris[i].active) continue;
      Debris& d = debris[i];
      if (overlaps(b.x - 2, b.y - 2, 4, 4, d.x, d.y, d.w, d.h)) {
        d.hp--;
        b.pierce--;
        if (d.hp <= 0) {
          score += 60;
          spawnParticles(d.x + d.w / 2.0f, d.y + d.h / 2.0f, DEBRIS_COLOR, 12);
          eraseDebrisAt(d);
          if (d.turretIdx >= 0) { eraseEnemyAt(enemies[d.turretIdx]); enemies[d.turretIdx].active = false; }
          d.active = false;
          sfxBreak();
        }
        if (b.pierce <= 0) { b.active = false; break; }
      }
    }
  }

  if (invulnerable) {
    if (millis() >= invulnUntilMs) invulnerable = false;
    return; // no ship-contact damage while flashing invulnerable
  }

  float sx = shipX, sy = shipY, sw = SHIP_W, sh = SHIP_H;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    Enemy& e = enemies[i];
    float r = (e.type == EN_DRIFTER) ? 7 : 5;
    if (overlaps(sx, sy, sw, sh, e.x - r, e.y - r, r * 2, r * 2)) { killShip(); return; }
  }
  for (int i = 0; i < MAX_DEBRIS; i++) {
    if (!debris[i].active) continue;
    Debris& d = debris[i];
    if (overlaps(sx, sy, sw, sh, d.x, d.y, d.w, d.h)) { killShip(); return; }
  }
  for (int i = 0; i < MAX_EBULLETS; i++) {
    if (!ebullets[i].active) continue;
    EBullet& b = ebullets[i];
    if (overlaps(sx, sy, sw, sh, b.x - 2, b.y - 2, 4, 4)) {
      ebullets[i].active = false;
      tft->fillRect((int)b.x - RT_SPR_ENEMY_BULLET_W / 2 - 1, (int)b.y - RT_SPR_ENEMY_BULLET_H / 2 - 1,
                    RT_SPR_ENEMY_BULLET_W + 2, RT_SPR_ENEMY_BULLET_H + 2, RT_BG);
      killShip();
      return;
    }
  }
  for (int i = 0; i < MAX_CAPSULES; i++) {
    if (!capsules[i].active) continue;
    Capsule& c = capsules[i];
    if (overlaps(sx, sy, sw, sh, c.x, c.y, RT_SPR_CAPSULE_SPEED_W, RT_SPR_CAPSULE_SPEED_H)) {
      eraseCapsuleAt(c);
      c.active = false;
      score += 40;
      if (c.kind == 1) weaponType = (weaponType + 1) % 3;
      sfxPowerup();
    }
  }
}

void drawPauseOverlay() {
  tft->fillRect(RT_W / 2 - 70, RT_H / 2 - 26, 140, 52, darken(RT_BG, -6));
  tft->drawRect(RT_W / 2 - 70, RT_H / 2 - 26, 140, 52, TITLE_COLOR);
  rtDrawCentered("PAUSED", RT_H / 2 - 16, 2, TITLE_COLOR);
  rtDrawCentered("ENC:RESUME KEY0:EXIT", RT_H / 2 + 8, 1, TEXT_COLOR);
}

void drawGameOverOverlay() {
  tft->fillRect(RT_W / 2 - 90, RT_H / 2 - 30, 180, 60, darken(RT_BG, -6));
  tft->drawRect(RT_W / 2 - 90, RT_H / 2 - 30, 180, 60, WARN_COLOR);
  rtDrawCentered("GAME OVER", RT_H / 2 - 20, 2, WARN_COLOR);
  char buf[28];
  snprintf(buf, sizeof(buf), "SCORE %06d  HI %06d", score, highScore);
  rtDrawCentered(buf, RT_H / 2 + 4, 1, TEXT_COLOR);
  rtDrawCentered("ENC: RESTART", RT_H / 2 + 18, 1, TEXT_COLOR);
}

void drawStageClearOverlay() {
  tft->fillRect(RT_W / 2 - 100, RT_H / 2 - 26, 200, 52, darken(RT_BG, -6));
  tft->drawRect(RT_W / 2 - 100, RT_H / 2 - 26, 200, 52, CAPSULE_SPEED_COLOR);
  rtDrawCentered("STAGE 1 CLEAR", RT_H / 2 - 16, 2, CAPSULE_SPEED_COLOR);
  rtDrawCentered("MORE STAGES COMING SOON", RT_H / 2 + 8, 1, TEXT_COLOR);
}

} // namespace

void rtypeInit() {
  initGamePalette();
  loadHighScore();
  resetGame();
  drawPlayfield();
  Serial.printf("R-Type ready. High score: %d\n", highScore);
}

void rtypeUpdate(const InputState& in, float dt) {
  switch (state) {
    case PLAYING: {
      updateShipMovement(in, dt);
      updateFireControl(in);
      updateEncSwControl(in);
      if (state != PLAYING) { redrawShipIfNeeded(); break; } // just paused this frame

      unsigned long now = millis();
      if (invulnerable && now - lastBlinkMs > 110) { lastBlinkMs = now; shipBlinkOn = !shipBlinkOn; }
      else if (!invulnerable) shipBlinkOn = true;

      updateWorld(dt);
      if (state != PLAYING) break; // boss incoming this frame
      checkCollisions();
      redrawShipIfNeeded();
      drawHUD(false);
      break;
    }
    case BOSS_INTRO:
      // Controls freeze on the intro banner (ship doesn't move, so it can't
      // fly through and erase the banner text) — pause still works.
      updateEncSwControl(in);
      if (state != BOSS_INTRO) break; // just paused this frame
      if (millis() >= bossIntroUntilMs) {
        drawPlayfield();
        spawnBoss();
        state = BOSS_FIGHT;
      }
      break;
    case BOSS_FIGHT: {
      updateShipMovement(in, dt);
      updateFireControl(in);
      updateEncSwControl(in);
      if (state != BOSS_FIGHT) { redrawShipIfNeeded(); break; } // just paused this frame

      unsigned long now = millis();
      if (invulnerable && now - lastBlinkMs > 110) { lastBlinkMs = now; shipBlinkOn = !shipBlinkOn; }
      else if (!invulnerable) shipBlinkOn = true;

      updateBossFight(dt);
      if (state != BOSS_FIGHT) { redrawShipIfNeeded(); break; } // defeated Guard Ray or died this frame
      redrawShipIfNeeded();
      drawHUD(false);
      break;
    }
    case PAUSED:
      updateEncSwControl(in);
      if (in.encSwEdge) {
        state = stateBeforePause;
        pauseLatched = false;
        drawPlayfield();
        if (state == BOSS_FIGHT) {
          for (auto& b : boss) if (b.active) drawBossUnit(b);
          drawBossHpBar();
        }
      }
      break;
    case GAME_OVER:
      if (in.encSwEdge) {
        resetGame();
        drawPlayfield();
        sfxUiConfirm();
      }
      break;
    case STAGE_CLEAR:
      // Auto-return to menu after the banner has shown a while; see
      // rtypeWantsExit(), which main.cpp polls every frame.
      break;
  }
}

void rtypeOnExit() {
  saveHighScoreIfNeeded();
}

void rtypeRenderSnapshot() {
  drawPlayfield();
  switch (state) {
    case PAUSED: drawPauseOverlay(); break;
    case GAME_OVER: drawGameOverOverlay(); break;
    case STAGE_CLEAR: drawStageClearOverlay(); break;
    case BOSS_INTRO:
      tft->fillRect(0, PLAY_TOP, RT_W, RT_H - PLAY_TOP, RT_BG);
      rtDrawCentered(BOSS_NAME, RT_H / 2 - 10, 3, WARN_COLOR);
      rtDrawCentered("STAGE BOSS APPROACHING", RT_H / 2 + 18, 1, TEXT_COLOR);
      break;
    case BOSS_FIGHT:
      for (auto& b : boss) if (b.active) drawBossUnit(b);
      drawBossHpBar();
      break;
    default: break;
  }
}

bool rtypeWantsExit(const InputState& in) {
  if (state == PAUSED) return in.key0Edge;
  if (state == STAGE_CLEAR) return millis() - stageClearAtMs >= STAGE_CLEAR_BANNER_MS;
  return false;
}
