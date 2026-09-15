#include "bubblebobble_pr32.h"
#include <graphics/Color.h>
#include <cmath>
#include <cstdio>
#include "hardware.h"
#include "bubblebobble_pr32_sprites.h"

namespace pr32 = pixelroot32;

namespace bubblebobble_pr32 {

using pr32::core::arenaNew;
using pr32::math::toScalar;
using pr32::math::Vector2;
using pr32::math::Scalar;
using pr32::graphics::Color;
using pr32::graphics::Renderer;

namespace {

// ================= Grid / level layout =================
// Real per-tile grid, extended to fill the whole display vertically. The
// original pass here was a straight decode of JulianRijken/BubbleBobble's
// actual Levels.png (32x28 tiles, cropped to 30x28 — see the git history/
// plan file for that derivation) — at TILE_SIZE=8 with a 24px top margin,
// 28 rows only reached y=248 on a 320px-tall screen, leaving 72px (nearly
// a quarter of the display) unused black space at the bottom, reported
// live ("why are we not using all of the vertical space"). Re-authored to
// 37 rows (fills to y=320 exactly) — genuinely extended with more real
// platform content per level, not just the same layout stretched: level 0
// gained a 4th band, level 1 a 5th, level 2's box-maze frame grew from a
// 10-row single frame to an 19-row triple-banded one. Each level keeps its
// original row PATTERNS (same one-way-platform shapes, verified against
// the real reference data) just at more of them, so the "verified against
// JulianRijken/BubbleBobble" provenance still holds for every individual
// pattern even though the overall row count/placement is this session's
// own extension rather than a further crop of the source image.
constexpr int TILE_SIZE = 8;
constexpr int COLS = 30;
constexpr int ROWS = 37;
constexpr int PLAYFIELD_TOP = 24;
// 3 real levels decoded from JulianRijken/BubbleBobble's actual Levels.png
// (see LEVELS' top comment) + 5 new ones this session, each a genuinely
// different platform SHAPE (not just a recolor of an existing band/maze
// pattern) — researched against real NES Bubble Bobble round layouts
// (zigzag staircases, diamond/hourglass rooms, alternating pillars,
// symmetric cross-shaped obstacles, dense open scatter are all real
// recurring round shapes in the original game, not invented from nothing).
constexpr int NUM_LEVELS = 8;

// Real Bubble Bobble rounds: some enclose the play area with a solid
// floor AND ceiling, and others leave a single narrow gap in both (NOT
// the whole boundary open — see the gap constants below) so falling
// through the floor gap wraps you back in through the ceiling gap at the
// top. A core mechanic that was never actually reachable in this rebuild
// before this pass: the vertical-wraparound teleport in
// BubbleBobbleScene::update()/updateEnemies() has existed since Phase 2,
// but was dead code with nothing to fall through (every level had a full
// solid floor and — less obviously, since it was never visible — no
// ceiling actor existed at all, for any level, so "closed" levels weren't
// even truly closed at the top). First pass at this mechanic made the
// floor entirely open on these levels, which let Bub/enemies fall through
// literally anywhere rather than needing to find and use a specific gap
// (reported live: "just one or two gaps... should fit through", not the
// whole boundary). Levels 0/1/3/5/7 get a real narrow gap in both
// boundaries (see kGapCol/kGapWidth); levels 2/4/6 stay fully enclosed —
// both boundaries solid, no gap at all.
constexpr bool LEVEL_HAS_GAP[NUM_LEVELS] = {
    true, true, false, true, false, true, false, true,
};
// Same column range in every level with a gap (keeps the floor gap and
// ceiling gap vertically aligned, like the reference game's rounds) —
// 5 tiles (40px) wide, comfortably over Bub's 32px visual width with
// real margin either side (see BUB_COLLISION_W's own comment on why a
// gap exactly actor-width wide isn't safe — corner-grazing showed up
// there even with only a few px of overlap).
constexpr int kGapCol = 12;
constexpr int kGapWidth = 5;

int rowY(int r) { return PLAYFIELD_TOP + r * TILE_SIZE; }
int colX(int c) { return c * TILE_SIZE; }

// '.' = None (no collider, just backdrop — not yet drawn, background art
// is future work), 'p' = Semi (one-way platform), 'S' = Solid (wall/floor,
// blocks from every side), ' ' = no tile at all (open air).
const char* const LEVELS[NUM_LEVELS][ROWS] = {
    { // Level 0 — 4 evenly-spaced bands (was 3)
        "                              ",
        "                              ",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "Spp   pppppppppppppppppp   ppS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "Spp   pppppppppppppppppp   ppS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "Spp   pppppppppppppppppp   ppS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "Spp   pppppppppppppppppp   ppS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 1 — 5 varied bands (was 4)
        "                              ",
        "                              ",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S           pppppp           S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S        ppppp  ppppp        S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S     pppppppppppppppppp     S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S  ppppppp  pppppp  ppppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S        ppppp  ppppp        S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 2 — triple-banded box maze (was a single band)
        "                              ",
        "                              ",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S   pppppppp      pppppppp   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   ppppppppp    ppppppppp   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   ppppppppp    ppppppppp   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   p                    p   S",
        "S   pppppppppp  pppppppppp   S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "Sppppp  ppp        ppp  pppppS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 3 -- Zigzag (side-to-side descent). Redesigned after the
      // original diagonal-staircase version (6-wide steps shifting only 3
      // columns per row) put two one-way tiles just 1 row (8px) apart in
      // the 3-column overlap between adjacent steps -- reported live as
      // "bub trapped" and an enemy trapped too, on this exact level. A
      // real diagonal ramp isn't safe here since one-way platforms are
      // flat tiles, not slopes -- any two overlapping-column tiles closer
      // than a real row-pitch apart trap whatever's between them. Fixed
      // by making each zigzag "step" a genuinely separate platform (zero
      // column overlap with its same-row neighbor, and the same 6-row
      // pitch as levels 1/4/7 against anything sharing its columns one
      // band up/down), alternating left/right across the room instead of
      // sliding diagonally.
        "                              ",
        "                              ",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S ppppppppppppp     ppppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S  ppppppp     ppppppppppppp S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S ppppppppppppp     ppppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S  ppppppp     ppppppppppppp S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S ppppppppppppp     ppppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 4 -- Diamond / hourglass. Re-spaced (see below) after the
      // first version packed 14 bands only 2 rows (16px) apart -- nowhere
      // near enough clearance for Bub's 20px collision box, reported live
      // as enemies getting stuck in the platforms and Bub sinking through
      // them. Now 5 bands at the same 6-row pitch level 1 already
      // uses and proved fine on hardware -- fewer steps, but still reads
      // as a diamond silhouette, and every remaining gap is a real,
      // proven-safe distance instead of a guess.
        "                              ",
        "                              ",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S  pppppppppppppppppppppp    S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S      pppppppppppppp        S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S          pppppp            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S      pppppppppppppp        S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S  pppppppppppppppppppppp    S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 5 -- Alternating solid pillars (forced zig-zag climb)
        "                              ",
        "                              ",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "S                            S",
        "S ppppp                      S",
        "S   S                        S",
        "S   S                        S",
        "S   S                        S",
        "S   S                        S",
        "S   S                        S",
        "S   S                 ppppp  S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S         ppppppppp          S",
        "S   S                   S    S",
        "S ppSpp                 S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                 ppSpp  S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S         ppppppppp          S",
        "S   S                   S    S",
        "S ppSpp                 S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S   S                   S    S",
        "S                       S    S",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 6 -- Plus/cross-shaped central obstacle (open perimeter)
        "                              ",
        "                              ",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S                            S",
        "S ppppppppppppppppppppppppp  S",
        "S                            S",
        "S                            S",
        "S  ppppppp         ppppppp   S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S             S              S",
        "S             S              S",
        "S pppppp             pppppp  S",
        "S             S              S",
        "S             S              S",
        "S        SSSSSSSSSSS         S",
        "S             S              S",
        "S             S              S",
        "S             S              S",
        "S  ppppppp         ppppppp   S",
        "S             S              S",
        "S             S              S",
        "S                            S",
        "S                            S",
        "S ppppppppppppppppppppppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S   pppppppp     pppppppp    S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
        "                              ",
    },
    { // Level 7 -- Dense scattered platforms (open free-climb). Re-spaced
      // (6-row pitch instead of 4) as a preventive fix alongside level 4's
      // confirmed bug -- same risk shape (many flat platform islands
      // stacked in the same columns), just less severe; fixed before it
      // got reported rather than waiting.
        "                              ",
        "                              ",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "S                            S",
        "S                            S",
        "S pppppp    ppppp    pppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S    ppppp      pppp    pppp S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S pppp   pppppp    pppp  ppp S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S     ppppp   pppp   pppppp  S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S ppppp   pppp   ppppp  pppp S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "S                            S",
        "SSSSSSSSSSSS     SSSSSSSSSSSSS",
        "                              ",
    },
};

// Bub physics tuning — timings/speeds reused verbatim from the old
// bubblebobble.cpp, but BUB_W/H are NOT: the old file (and this file's
// first sprite-export pass) assumed BubbleCharacter.png was a 4x4 grid of
// 24x16 frames, scaled 2x to 48x32. It's actually a 6x4 grid of 16x16
// frames (confirmed by a column-opacity scan after the 24px-wide crop was
// caught rendering half of the neighboring icon on real hardware) — 2x
// scale of the real 16x16 frame is 32x32.
constexpr int BUB_W = 32;
constexpr int BUB_H = 32;
// Collision box narrower than the drawn sprite: the layout's platform gaps
// are exactly 2 tiles (32px) wide, i.e. exactly BUB_W with zero clearance.
// Falling straight down through one with vx==0, Bub's hitbox edge sits
// flush against a platform's side edge, and PixelRoot32's one-way corner
// check (validateOneWayPlatform's absNormalY<0.1 branch) can read that
// graze as "resting on top" — seen live on hardware as Bub parked
// motionless in mid-air (vy pinned at exactly 0.0 forever, on both axes,
// regardless of input) until horizontal movement shifted it clear of the
// edge. First attempt used a 24px collision box (4px margin each side in
// a 32px gap) — NOT enough: live diagnostics caught Bub stopped at x=57.4,
// its 24px-wide box spanning to 81.4, poking 1.4px into the next solid
// segment starting at x=80. Narrowed further to a full tile's worth of
// margin (8px each side) so an ordinary mid-gap stop can't reach either edge.
constexpr int BUB_COLLISION_W = 16;
constexpr int BUB_VISUAL_OFFSET_X = (BUB_W - BUB_COLLISION_W) / 2;
// Collision height shorter than the drawn sprite for the same reason as
// width, but a different constraint: the level's row pitch is 36px with an
// 8px-thick platform, leaving only 28px of clear vertical air between two
// consecutive rows — less than BUB_H (32). Walking under a platform into
// that corridor with a full-height hitbox meant Bub's own top always
// clipped the row above, and the resulting depenetration read as "fall
// through" (reported live: "not enough vertical space... I fall through").
// Anchored to the FEET, not centered: the collision box's bottom always
// matches the sprite's bottom (so ground contact still looks right), and
// the box is shorter only from the top, leaving Bub's head/crest free to
// poke through a platform above without being blocked by it while passing
// underneath — the standard "feet hitbox" shape for exactly this situation.
constexpr int BUB_COLLISION_H = 20;
constexpr int BUB_VISUAL_OFFSET_Y = BUB_H - BUB_COLLISION_H;
constexpr float GRAVITY = 600.0f;
// -230 (the old hand-rolled game's value) only reaches ~44px of height
// (v^2/2g), barely over the level's 36px row pitch. -270 (~61px in theory)
// still read as "almost, a few pixels short" on real hardware — the actual
// discrete-timestep integration undershoots the ideal-parabola number by
// more than expected, and reaching a platform 2 rows up (72px) needs
// noticeably more than one row's worth anyway. Bumped further with real
// margin this time rather than inching up again.
constexpr float JUMP_VELOCITY = -320.0f;
// 75 (the old hand-rolled game's value) felt sluggish live on hardware once
// real per-pixel movement/collision was in play — bumped for a snappier feel.
constexpr float WALK_SPEED = 115.0f;
constexpr float ENC_NUDGE = 3.0f;
constexpr float THUMB_JUMP_TRIGGER = 0.55f;
constexpr float THUMB_JUMP_RELEASE = 0.3f;
constexpr float MAX_FALL_SPEED = 180.0f;

// Collision layers: platforms collide with both the player and enemies,
// but the player and enemies deliberately do NOT collide with each other
// through the engine. Sharing one layer for all three (the original
// Phase 2/3 setup) meant Bub and a Zen-Chan solid-blocked each other like
// two walls — reported live as both of them getting stuck on ledges, and
// as no longer fitting through the level's narrow inter-platform corridor
// whenever an enemy happened to be standing in it. Bub/Zen-Chan "touch"
// (for the capture bubble and the hurtbox above) is checked entirely by
// BubbleBobbleScene's own manual AABB code, independent of these layers.
constexpr pixelroot32::physics::CollisionLayer kLayerPlatform = 1u << 0;
constexpr pixelroot32::physics::CollisionLayer kLayerPlayer = 1u << 1;
constexpr pixelroot32::physics::CollisionLayer kLayerEnemy = 1u << 2;

// Character-vs-character touch ("hurtbox"), deliberately separate from the
// narrow 16px physics collision box used for platform movement. Reusing
// the narrow box for "did an enemy touch Bub" meant two sprites had to
// overlap by a full 16px (8px trimmed off each side) before anything
// registered — visually they'd already be standing on top of each other
// with nothing happening, reported live as "nothing happens when they
// touch me." This is sized off the full 32px visual sprite instead, with
// a little trim so it isn't overly generous at the very edges.
constexpr int HURTBOX_W = 24;
constexpr int HURTBOX_H = 24;
constexpr int HURTBOX_OFFSET_X = (BUB_W - HURTBOX_W) / 2;
constexpr int HURTBOX_OFFSET_Y = (BUB_H - HURTBOX_H) / 2;

// ================= Zen-Chan (enemy) tuning =================
// Same visual/collision sizing as Bub (Enemys.png is also, per the same
// opacity-scan verification, 16x16 native frames at 2x scale) and the same
// feet-anchored hitbox shrink for the same two reasons (gap-edge corner
// grazing, insufficient inter-row vertical clearance) — reusing
// BUB_COLLISION_W/H/offsets rather than re-deriving separate constants for
// an actor with identical dimensions.
constexpr int ZC_W = BUB_W;
constexpr int ZC_H = BUB_H;
constexpr int ZC_COLLISION_W = BUB_COLLISION_W;
constexpr int ZC_COLLISION_H = BUB_COLLISION_H;
constexpr int ZC_VISUAL_OFFSET_X = BUB_VISUAL_OFFSET_X;
constexpr int ZC_VISUAL_OFFSET_Y = BUB_VISUAL_OFFSET_Y;

// AI rule constants + enemy speed, ported verbatim (same seconds-based
// values) from the old bubblebobble.cpp, itself ported from
// JulianRijken/BubbleBobble's ZenChanBehaviour (GPL-3.0).
constexpr float ENEMY_SPEED_BASE = 40.0f;
constexpr float ANGRY_MULT = 1.7f;
constexpr float MIN_TIME_BETWEEN_WALL_TURN = 1.0f;
constexpr float FACE_PLAYER_MIN_DIST = 15.0f;
// Zen-Chan's jump only needs to clear one row like Bub's does, but Bub's
// -320 was tuned with a lot of headroom for the *player's* feel; keep the
// enemy's closer to the old game's original -230 (still comfortably over
// the 36px row pitch on its own, ~44px in theory) since there's no "almost
// but not quite" complaint to react to here.
constexpr float ENEMY_JUMP_VELOCITY = -260.0f;

// ================= Maita boulder tuning =================
// Ported spirit (not the exact physics units) of Maita.h's
// BOULDER_THROW_POWER/BOULDER_THROW_INTERVAL — a straight horizontal lob,
// no arc, simple enough to be dodged by moving or jumping over it, same as
// the original's rolling-boulder read.
constexpr int BOULDER_W = 16;
constexpr int BOULDER_H = 16;
constexpr float BOULDER_SPEED = 90.0f;
constexpr unsigned long BOULDER_LIFETIME_MS = 3000;

// ================= Capture bubble tuning =================
// Ported verbatim from the old bubblebobble.cpp / JulianRijken/BubbleBobble's
// CaptureBubble constants (GPL-3.0): ~1s travel arc before a bubble becomes
// poppable (BUBBLE_TRAVEL_TIME), 8s before an unpapped catch auto-releases
// (BUBBLE_LIFETIME_MS).
constexpr int BUBBLE_W = 32;
constexpr int BUBBLE_H = 32;
constexpr float BUBBLE_TRAVEL_SPEED = 95.0f;
constexpr float BUBBLE_RISE_SPEED = -32.0f;
constexpr float BUBBLE_TRAVEL_TIME = 0.9f;
constexpr unsigned long BUBBLE_LIFETIME_MS = 8000;
constexpr float CHAIN_RADIUS = 34.0f;

// ================= Food tuning =================
// Ported verbatim from the old bubblebobble.cpp / the reference project's
// actual pickup types and Game::PICKUP_VALUES.
constexpr int FOOD_W = 36;   // watermelon's native crop width (18px, 2x scale)
constexpr int FOOD_H = 32;
constexpr unsigned long FOOD_LIFETIME_MS = 10000;
constexpr int FOOD_VALUES[2] = {100, 200};

constexpr unsigned long PAUSE_HOLD_MS = 500;

// 'p' (one-way) or 'S' (solid) both count as "something to stand on" for
// this query's purposes (Zen-Chan's climb check, food's landing check) —
// matches the old single-character '1' check's intent, just against the
// real grid's two solid-ish tile kinds instead of one.
bool platformSolidAt(int level, int row, float x0, float x1) {
    if (row < 0 || row >= ROWS) return false;
    int c0 = (x0 / TILE_SIZE) > 0 ? (int)(x0 / TILE_SIZE) : 0;
    int c1raw = (int)((x1 - 1) / TILE_SIZE);
    int c1 = c1raw < (COLS - 1) ? c1raw : (COLS - 1);
    for (int c = c0; c <= c1; ++c) {
        char t = LEVELS[level][row][c];
        if (t == 'p' || t == 'S') return true;
    }
    return false;
}

// One tier up from an actor's current row, or -1 if it isn't resting neatly
// on a row right now (ported from the old rowAbove()) — used by Zen-Chan's
// climb-toward-player check.
int rowAbove(float footY) {
    float rel = (footY - PLAYFIELD_TOP) / (float)TILE_SIZE;
    int r = (int)(rel + 0.5f);
    if (fabsf((float)rowY(r) - footY) > 2.0f) return -1;
    return r - 1;
}

// Simple gravity-to-rest sweep for non-Actor manual-physics objects (food)
// — ported from the old bubblebobble.cpp's applyVerticalPhysics(). Food
// doesn't need a real KinematicActor (no player/AI-driven horizontal
// motion, nothing else needs to collide with it), so this plain row-scan
// against the level grid is simpler and avoids yet another engine physics
// actor.
void applyVerticalPhysics(int level, float x, int w, float& y, float& vy, bool& onGround, float dt, int h) {
    vy += GRAVITY * dt;
    float footOld = y + h;
    float newY = y + vy * dt;
    float footNew = newY + h;
    onGround = false;

    if (vy > 0) {
        float bestRy = 1e9f;
        bool found = false;
        for (int r = 0; r < ROWS; ++r) {
            float ry = (float)rowY(r);
            if (ry > footOld - 0.5f && ry <= footNew + 0.5f && platformSolidAt(level, r, x, x + w)) {
                if (ry < bestRy) { bestRy = ry; found = true; }
            }
        }
        if (found) {
            newY = bestRy - h;
            vy = 0;
            onGround = true;
        } else if (newY + h > SCREEN_H) {
            // A level with a floor gap (LEVEL_HAS_GAP) has nothing solid
            // for platformSolidAt() to find directly under that gap's
            // columns — that's the point, it's how Bub/enemies wrap from
            // bottom to top. Food doesn't participate in that mechanic
            // (it's not something the real game wraps around either), so
            // without this it would just fall forever past the visible
            // screen and become permanently
            // uncollectible during a bonus round. Clamp it to the visual
            // screen bottom as an implicit hard floor instead.
            newY = (float)SCREEN_H - h;
            vy = 0;
            onGround = true;
        }
    } else if (vy < 0 && newY < PLAYFIELD_TOP) {
        newY = PLAYFIELD_TOP;
        vy = 0;
    }
    y = newY;
}

bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

// Converts a Bub/Zen-Chan actor's physics-collision-box position (what
// `position` holds) into its wider hurtbox's world position — see
// HURTBOX_W/H's comment for why these two boxes are deliberately not the
// same. Both actor kinds share identical visual/collision dimensions, so
// one pair of helpers covers both.
float hurtboxX(float actorPositionX) { return actorPositionX - BUB_VISUAL_OFFSET_X + HURTBOX_OFFSET_X; }
float hurtboxY(float actorPositionY) { return actorPositionY - BUB_VISUAL_OFFSET_Y + HURTBOX_OFFSET_Y; }

} // namespace

BubActor::BubActor(Scalar x, Scalar y, int w, int h) : KinematicActor(x, y, w, h) {
    // Layer/mask: player only collides with platforms, NOT with Zen-Chan
    // (see kLayerPlatform/kLayerPlayer/kLayerEnemy's comment) — Bub/enemy
    // "touch" is handled entirely by BubbleBobbleScene's own hurtbox AABB
    // check, deliberately outside the engine's physics.
    setCollisionLayer(kLayerPlayer);
    setCollisionMask(kLayerPlatform);
    setStrictTopSurfaceFloor(true);
}

void BubActor::update(unsigned long deltaTime) {
    KinematicActor::update(deltaTime);
    if (!walking) return;
    constexpr unsigned long kFrameTimeMs = 150;
    animAccumulatorMs += deltaTime;
    while (animAccumulatorMs >= kFrameTimeMs) {
        animAccumulatorMs -= kFrameTimeMs;
        walkFrame = walkFrame ? 0 : 1;
    }
}

void BubActor::setAnimState(bool facingRightIn, bool walkingIn, bool airborneIn) {
    facingRight = facingRightIn;
    walking = walkingIn;
    airborne = airborneIn;
}

void BubActor::draw(Renderer& renderer) {
    if (!visible) return;
    namespace spr = bubblebobble_pr32_sprites;
    const pr32::graphics::Sprite4bpp* sprite;
    if (airborne) {
        sprite = facingRight ? &spr::BB_BUB_JUMP_R : &spr::BB_BUB_JUMP_L;
    } else if (walking && walkFrame) {
        sprite = facingRight ? &spr::BB_BUB_WALK1_R : &spr::BB_BUB_WALK1_L;
    } else {
        sprite = facingRight ? &spr::BB_BUB_WALK0_R : &spr::BB_BUB_WALK0_L;
    }
    // position.x/position.y are the (narrower, shorter) collision box's
    // top-left. X: shift left by the trimmed margin to stay centered under
    // the wider sprite. Y: the collision box is trimmed only from the TOP
    // (feet-anchored — see BUB_COLLISION_H), so the sprite draws BELOW
    // position.y by the trimmed amount to keep its feet aligned with the
    // hitbox's bottom while its head/crest extends upward past the hitbox.
    renderer.drawSprite(*sprite, static_cast<int>(position.x) - BUB_VISUAL_OFFSET_X,
                         static_cast<int>(position.y) - BUB_VISUAL_OFFSET_Y, false);
}

PlatformActor::PlatformActor(Scalar x, Scalar y, int w, int h, bool oneWayIn, int levelThemeIn)
    : StaticActor(x, y, w, h), oneWay(oneWayIn), levelTheme(levelThemeIn) {
    // Platforms collide with both the player and enemies — only Bub-vs-
    // Zen-Chan collision is deliberately excluded (see kLayerPlayer's
    // constructor comment).
    setCollisionLayer(kLayerPlatform);
    setCollisionMask(kLayerPlayer | kLayerEnemy);
    setOneWay(oneWayIn);
}

void PlatformActor::draw(Renderer& renderer) {
    // Every primitive draw call (not just sprites) defaults to
    // PaletteContext::Sprite — so without this override, Bub's custom
    // sprite palette (set in BubbleBobbleScene::init()) would ALSO hijack
    // whatever named Color this draws, since it's the same 16-slot table
    // unless dual-palette mode explicitly separates them. Force this
    // plain-color draw through the untouched Background table instead —
    // which also means these patterns are free: they use the engine's
    // built-in 16-color PR32 table, not a single byte of the already-tight
    // custom sprite palette budget.
    namespace gfx = pixelroot32::graphics;
    gfx::PaletteContext bgContext = gfx::PaletteContext::Background;
    gfx::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);

    // Real NES screenshots (checked directly this session) show one-way
    // platforms and solid walls sharing the SAME brick/checker texture per
    // level — the one-way/solid split is a pure gameplay behavior, not a
    // visual one. So the only thing that varies here is level theme, not
    // oneWay. Three deliberately distinct looks (color AND pattern, not
    // just a palette swap) matching the 3 real levels' screenshots.
    int x0 = static_cast<int>(position.x);
    int y0 = static_cast<int>(position.y);
    // 3 pattern kinds (0=brick-seam, 1=checkerboard, 2=diagonal stripe) so
    // 8 levels don't just cycle the same two looks — every distinct
    // Color enumerator this palette has (Color.h's non-alias 16) gets used
    // at most once as a base, paired with a genuinely different accent.
    Color base, accent;
    int patternKind;
    switch (levelTheme) {
        // Color::LightBlue is an alias of Color::Blue in this engine's
        // default palette (confirmed directly in Color.h) — using it as an
        // "accent" against a Blue base is invisible by construction, not a
        // rendering bug. Cyan is a real, distinct slot.
        case 0: base = Color::Blue;      accent = Color::Cyan;       patternKind = 0; break;
        case 1: base = Color::Purple;    accent = Color::Magenta;    patternKind = 0; break;
        case 2: base = Color::DarkRed;   accent = Color::Magenta;    patternKind = 1; break;
        case 3: base = Color::Green;     accent = Color::LightGreen; patternKind = 1; break;
        case 4: base = Color::Navy;      accent = Color::Cyan;       patternKind = 2; break;
        case 5: base = Color::DarkGreen; accent = Color::Yellow;     patternKind = 0; break;
        case 6: base = Color::Orange;    accent = Color::Yellow;     patternKind = 1; break;
        default: base = Color::Gray;     accent = Color::White;      patternKind = 2; break;
    }
    renderer.drawFilledRectangle(x0, y0, width, height, base);
    for (int ty = 0; ty < height; ty += TILE_SIZE) {
        for (int tx = 0; tx < width; tx += TILE_SIZE) {
            if (patternKind == 1) {
                // Checkerboard, matching level 2's real screenshot.
                bool alt = (((tx / TILE_SIZE) + (ty / TILE_SIZE)) & 1) == 0;
                if (alt) renderer.drawFilledRectangle(x0 + tx, y0 + ty, TILE_SIZE, TILE_SIZE, accent);
            } else if (patternKind == 2) {
                // Diagonal stripe: every 3rd diagonal (tx/8 + ty/8) gets a
                // full accent tile — a distinct "band" look from both the
                // brick seam and the checkerboard.
                int diag = (tx / TILE_SIZE + ty / TILE_SIZE) % 3;
                if (diag == 0) renderer.drawFilledRectangle(x0 + tx, y0 + ty, TILE_SIZE, TILE_SIZE, accent);
            } else {
                // Brick-seam look: a 2px seam at each tile's left/top edge
                // rather than a small centered circle — bolder and reads
                // reliably at this resolution instead of nearly vanishing.
                renderer.drawFilledRectangle(x0 + tx, y0 + ty, 2, TILE_SIZE, accent);
                renderer.drawFilledRectangle(x0 + tx, y0 + ty, TILE_SIZE, 1, accent);
            }
        }
    }

    renderer.setRenderContext(saved);
}

ZenChanActor::ZenChanActor(Scalar x, Scalar y, int w, int h) : KinematicActor(x, y, w, h) {
    setCollisionLayer(kLayerEnemy);
    setCollisionMask(kLayerPlatform);
    setStrictTopSurfaceFloor(true);
}

void ZenChanActor::update(unsigned long deltaTime) {
    KinematicActor::update(deltaTime);
    constexpr unsigned long kFrameTimeMs = 120;
    animAccumulatorMs += deltaTime;
    while (animAccumulatorMs >= kFrameTimeMs) {
        animAccumulatorMs -= kFrameTimeMs;
        walkFrame = walkFrame ? 0 : 1;
    }
}

void ZenChanActor::draw(Renderer& renderer) {
    if (!alive || trapped) return;
    namespace spr = bubblebobble_pr32_sprites;
    const pr32::graphics::Sprite4bpp* sprite;
    if (isMaita) {
        sprite = (facing >= 0)
            ? (walkFrame ? &spr::BB_MAITA_WALK1_R : &spr::BB_MAITA_WALK0_R)
            : (walkFrame ? &spr::BB_MAITA_WALK1_L : &spr::BB_MAITA_WALK0_L);
    } else {
        sprite = (facing >= 0)
            ? (walkFrame ? &spr::BB_ZC_WALK1_R : &spr::BB_ZC_WALK0_R)
            : (walkFrame ? &spr::BB_ZC_WALK1_L : &spr::BB_ZC_WALK0_L);
    }
    renderer.drawSprite(*sprite, static_cast<int>(position.x) - ZC_VISUAL_OFFSET_X,
                         static_cast<int>(position.y) - ZC_VISUAL_OFFSET_Y, false);
}

void BubbleBobbleScene::buildLevel() {
    platformCount = 0;

    // Side walls + floor: identical shape in all 3 real levels (verified
    // directly against the decoded Levels.png data, not assumed) — a
    // 1-tile-thick solid wall down columns 0 and COLS-1 from row 2 through
    // the floor row (26), plus the floor spanning the columns between the
    // two walls. Hardcoded rather than generically scanning for 'S' tiles:
    // scanning would need 2D rectangle merging (a run of solid tiles that's
    // both wide AND tall) to avoid one StaticActor per tile, and every real
    // level already shares this exact wall/floor shape, so there's nothing
    // for a generic scanner to actually generalize over yet.
    constexpr int kWallTopRow = 2;
    constexpr int kFloorRow = 35;
    constexpr int kWallRows = kFloorRow - kWallTopRow + 1;
    auto addSolid = [this](float x, float y, int w, int h) {
        if (platformCount >= kMaxPlatforms) return;
        PlatformActor* p = arenaNew<PlatformActor>(arena, toScalar(x), toScalar(y), w, h, false, currentLevelIndex);
        if (p == nullptr) return;
        addEntity(p);
        platforms[platformCount++] = p;
    };
    addSolid((float)colX(0), (float)rowY(kWallTopRow), TILE_SIZE, kWallRows * TILE_SIZE);
    addSolid((float)colX(COLS - 1), (float)rowY(kWallTopRow), TILE_SIZE, kWallRows * TILE_SIZE);
    // Floor AND ceiling (the latter never existed as a real collider
    // before this pass — every level's row-2 interior was always '.'/
    // None). Levels with LEVEL_HAS_GAP leave a single narrow gap
    // (kGapCol/kGapWidth) in both, aligned to the same columns, so
    // falling through the floor gap and reappearing at the top (the
    // existing vertical-wraparound teleport) actually requires finding
    // and using that one gap — not falling through anywhere, which the
    // first version of this mechanic did by leaving the whole floor open.
    auto buildBoundaryRow = [&](int row) {
        if (!LEVEL_HAS_GAP[currentLevelIndex]) {
            addSolid((float)colX(1), (float)rowY(row), (COLS - 2) * TILE_SIZE, TILE_SIZE);
            return;
        }
        addSolid((float)colX(1), (float)rowY(row), (kGapCol - 1) * TILE_SIZE, TILE_SIZE);
        int rightStart = kGapCol + kGapWidth;
        addSolid((float)colX(rightStart), (float)rowY(row), (COLS - 1 - rightStart) * TILE_SIZE, TILE_SIZE);
    };
    buildBoundaryRow(kFloorRow);
    buildBoundaryRow(kWallTopRow);

    // One-way platforms: same horizontal-run merge as before, just against
    // the real per-level grid and the 'p' tile character.
    const char* const* level = LEVELS[currentLevelIndex];
    for (int r = 0; r < ROWS; ++r) {
        int c = 0;
        while (c < COLS) {
            if (level[r][c] != 'p') { ++c; continue; }
            int runStart = c;
            while (c < COLS && level[r][c] == 'p') ++c;
            int runLen = c - runStart;
            if (platformCount >= kMaxPlatforms) break;
            PlatformActor* p = arenaNew<PlatformActor>(
                arena, toScalar((float)colX(runStart)), toScalar((float)rowY(r)),
                runLen * TILE_SIZE, TILE_SIZE, true, currentLevelIndex);
            if (p == nullptr) break;
            addEntity(p);
            platforms[platformCount++] = p;
        }
    }

    // Interior solid runs ('S' inside columns 1..COLS-2, i.e. NOT the
    // outer border columns/floor row already hardcoded above as
    // addSolid() calls) — added for level 6's plus/cross-shaped central
    // obstacle, the first level to use an interior solid tile at all.
    // Without this, an 'S' anywhere except the outer border was silently
    // ignored: never built as a collider AND never drawn (both this and
    // the one-way scan above only ever matched 'p'), so the obstacle was
    // just invisible/absent — found via a hardware screenshot showing an
    // empty gap where the cross should be. Skips the floor row (kFloorRow)
    // AND the ceiling row (kWallTopRow) since both rows' interior 'S'
    // runs are already built by buildBoundaryRow() above — scanning them
    // here too would just add redundant duplicate actors spanning the
    // same columns.
    for (int r = 0; r < ROWS; ++r) {
        if (r == kFloorRow || r == kWallTopRow) continue;
        int c = 1;
        while (c < COLS - 1) {
            if (level[r][c] != 'S') { ++c; continue; }
            int runStart = c;
            while (c < COLS - 1 && level[r][c] == 'S') ++c;
            int runLen = c - runStart;
            if (platformCount >= kMaxPlatforms) break;
            PlatformActor* p = arenaNew<PlatformActor>(
                arena, toScalar((float)colX(runStart)), toScalar((float)rowY(r)),
                runLen * TILE_SIZE, TILE_SIZE, false, currentLevelIndex);
            if (p == nullptr) break;
            addEntity(p);
            platforms[platformCount++] = p;
        }
    }
}

void BubbleBobbleScene::buildLevelPreview(int levelIndex, float yOffset) {
    // Same shape logic as buildLevel(), deliberately duplicated rather than
    // parameterizing buildLevel() itself: this one must NOT touch
    // platformCount/clearEntities/arena.init() (it's adding alongside the
    // CURRENT level's still-alive entities, not replacing them), and every
    // position gets yOffset added. Cleanup is implicit — the next real
    // init() resets the whole arena, silently invalidating these too, same
    // as the ordinary level-to-level transition already relies on.
    constexpr int kWallTopRow = 2;
    constexpr int kFloorRow = 35;
    constexpr int kWallRows = kFloorRow - kWallTopRow + 1;
    int previewCount = 0;
    constexpr int kMaxPreview = kMaxPlatforms;
    auto addSolid = [this, levelIndex, yOffset, &previewCount](float x, float y, int w, int h) {
        if (previewCount >= kMaxPreview) return;
        PlatformActor* p = arenaNew<PlatformActor>(arena, toScalar(x), toScalar(y + yOffset), w, h, false, levelIndex);
        if (p == nullptr) return;
        addEntity(p);
        ++previewCount;
    };
    addSolid((float)colX(0), (float)rowY(kWallTopRow), TILE_SIZE, kWallRows * TILE_SIZE);
    addSolid((float)colX(COLS - 1), (float)rowY(kWallTopRow), TILE_SIZE, kWallRows * TILE_SIZE);
    // Same floor+ceiling-with-a-gap logic as buildLevel() — see its
    // matching comment.
    auto buildBoundaryRow = [&](int row) {
        if (!LEVEL_HAS_GAP[levelIndex]) {
            addSolid((float)colX(1), (float)rowY(row), (COLS - 2) * TILE_SIZE, TILE_SIZE);
            return;
        }
        addSolid((float)colX(1), (float)rowY(row), (kGapCol - 1) * TILE_SIZE, TILE_SIZE);
        int rightStart = kGapCol + kGapWidth;
        addSolid((float)colX(rightStart), (float)rowY(row), (COLS - 1 - rightStart) * TILE_SIZE, TILE_SIZE);
    };
    buildBoundaryRow(kFloorRow);
    buildBoundaryRow(kWallTopRow);

    const char* const* level = LEVELS[levelIndex];
    for (int r = 0; r < ROWS; ++r) {
        int c = 0;
        while (c < COLS) {
            if (level[r][c] != 'p') { ++c; continue; }
            int runStart = c;
            while (c < COLS && level[r][c] == 'p') ++c;
            int runLen = c - runStart;
            if (previewCount >= kMaxPreview) break;
            PlatformActor* p = arenaNew<PlatformActor>(
                arena, toScalar((float)colX(runStart)), toScalar((float)rowY(r) + yOffset),
                runLen * TILE_SIZE, TILE_SIZE, true, levelIndex);
            if (p == nullptr) break;
            addEntity(p);
            ++previewCount;
        }
    }

    // Interior solid runs (level 6's plus/cross obstacle) — see
    // buildLevel()'s matching comment for why this is needed at all (an
    // 'S' outside the outer border/floor row was silently never built OR
    // drawn before this). Skips the ceiling row too, same reason as
    // buildLevel().
    for (int r = 0; r < ROWS; ++r) {
        if (r == kFloorRow || r == kWallTopRow) continue;
        int c = 1;
        while (c < COLS - 1) {
            if (level[r][c] != 'S') { ++c; continue; }
            int runStart = c;
            while (c < COLS - 1 && level[r][c] == 'S') ++c;
            int runLen = c - runStart;
            addSolid((float)colX(runStart), (float)rowY(r), runLen * TILE_SIZE, TILE_SIZE);
        }
    }
}

namespace {
struct EnemySpawn { int row; int col; int facing; bool isMaita; };
// One safe (row, col) picked from each real level's actual 'p' rows/runs
// (see the LEVELS data above) — fixed rather than the old game's random
// placement, same reasoning as before (deterministic, easier to test).
// Re-picked for the extended 37-row layouts (see LEVELS above) — same
// "one safe spot per level's real 'p' rows" reasoning as before. Maita
// (see ZenChanActor::isMaita) is mixed in starting level 1 — one per
// level from there on, same as the reference project always pairing
// Zen-Chans with at least one Maita rather than an all-or-nothing switch.
const EnemySpawn LEVEL_SPAWNS[NUM_LEVELS][3] = {
    { {8, 10, 1, false}, {15, 15, -1, false}, {29, 20, 1, false} },    // level 0
    { {13, 10, 1, false}, {19, 15, -1, false}, {25, 20, 1, true} },    // level 1
    { {6, 7, 1, false}, {18, 20, -1, true}, {30, 4, 1, false} },       // level 2
    { {6, 8, 1, false}, {12, 20, -1, true}, {30, 10, 1, false} },      // level 3 (zigzag)
    { {7, 10, 1, false}, {19, 13, -1, true}, {31, 20, 1, false} },     // level 4 (diamond)
    { {6, 3, 1, false}, {17, 14, -1, true}, {27, 25, 1, false} },      // level 5 (pillars)
    { {9, 5, 1, false}, {16, 10, -1, true}, {31, 6, 1, false} },       // level 6 (plus/cross)
    { {5, 3, 1, false}, {17, 15, -1, true}, {29, 21, 1, false} },      // level 7 (scattered)
};
} // namespace

void BubbleBobbleScene::spawnEnemies() {
    // Same "spawn a few pixels above the surface, not flush" treatment as
    // Bub (see BubbleBobbleScene::init()'s comment on why flush spawning
    // tunnels through a one-way platform on the very first physics frame).
    const EnemySpawn* kSpawns = LEVEL_SPAWNS[currentLevelIndex];
    for (int i = 0; i < kMaxEnemies && i < 3; ++i) {
        const EnemySpawn& s = kSpawns[i];
        float x = colX(s.col);
        float y = rowY(s.row) - ZC_COLLISION_H - 4.0f;
        ZenChanActor* e = arenaNew<ZenChanActor>(arena, toScalar(x), toScalar(y), ZC_COLLISION_W, ZC_COLLISION_H);
        if (e == nullptr) break;
        e->alive = true;
        e->trapped = false;
        e->angry = false;
        e->isMaita = s.isMaita;
        e->facing = s.facing;
        e->timeSinceLastTurn = 0.0f;
        e->timeSinceLastJump = 0.0f;
        e->jumpInterval = 1.0f + (float)(millis() % 500) / 1000.0f;
        e->timeSinceLastFacePlayer = 0.0f;
        e->facePlayerInterval = 2.0f + (float)(millis() % 1500) / 1000.0f;
        // Maita's boulder toss (BOULDER_THROW_INTERVAL in the reference's
        // Maita.h is a fixed 4s; jittered slightly here, same rationale as
        // the jump/face-player intervals above, so 2 Maitas on screen at
        // once — not possible today with 1-per-level, but future-proof —
        // wouldn't throw in perfect lockstep).
        e->timeSinceLastThrow = 0.0f;
        e->throwInterval = 3.5f + (float)(millis() % 1000) / 1000.0f;
        addEntity(e);
        enemies[i] = e;
    }
}

void BubbleBobbleScene::init() {
    // Dual-palette mode: sprites (Bub) resolve through this game's own custom
    // 16-color table; everything else (PlatformActor's plain Color::Green
    // fills, drawn under an explicit Background context — see its draw())
    // keeps using the engine's default PR32 table untouched. A single shared
    // setCustomPalette() would have clobbered Color::Green/White/etc. globally
    // for every non-sprite draw too, since they resolve through the very same
    // active table unless separated like this. Idempotent, but only needs
    // setting once, not per scene-reset.
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);
    pr32::graphics::setSpriteCustomPalette(bubblebobble_pr32_sprites::PALETTE_RGB565);

    // Bumped again: level 5's alternating-pillar design plus the new
    // floor/ceiling gap actors (every level now builds 2-4 boundary
    // actors instead of 1) pushed a single level's own actor count close
    // to 65 on its own, and the transition preview briefly holds the
    // OUTGOING level's actors AND the next level's preview copy alive
    // simultaneously — see MAX_ENTITIES's comment in platformio.ini for
    // the matching Scene/CollisionSystem entity-count bump.
    // No longer squeezed to make room for other games' static footprint:
    // every game Scene is now heap-allocated only while active (see
    // main.cpp's returnToMenu()/ActiveApp::Menu case), so this buffer's
    // size is once again purely about what THIS scene itself needs.
    static uint8_t sceneBuffer[49152];
    arena.init(sceneBuffer, sizeof(sceneBuffer));
    clearEntities();

    buildLevel();
    spawnEnemies();
    for (int i = 0; i < kMaxBubbles; ++i) bubbles[i] = CaptureBubble{};
    for (int i = 0; i < kMaxFood; ++i) foods[i] = FoodItem{};
    for (int i = 0; i < kMaxBoulders; ++i) boulders[i] = Boulder{};
    paused = false;
    pauseLatched = false;
    bonusRoundActive = false;
    enemiesKilledThisLevel = 0;

    float startX = colX(COLS / 2) - BUB_COLLISION_W / 2.0f;
    // A few pixels ABOVE the platform's surface, not flush with it: PixelRoot32's
    // one-way-platform crossing check looks at whether the actor's *previous*
    // frame bottom was already above the platform top (see
    // KinematicActor::resolvePreSlideDepenetration/slide, which skip a one-way
    // body entirely once previousBottom is at/below its surface). Spawning
    // exactly flush left that check ambiguous on the very first physics frame
    // and Bub fell straight through every platform, including the solid-looking
    // bottom row, and kept free-falling through the vertical wraparound forever.
    float startY = rowY(ROWS - 2) - BUB_COLLISION_H - 4.0f;
    bub = arenaNew<BubActor>(arena, toScalar(startX), toScalar(startY), BUB_COLLISION_W, BUB_COLLISION_H);
    addEntity(bub);
    velocityY = toScalar(0.0f);
    thumbJumpLatched = false;
    lastFacingRight = true;
    stuckFrameCount = 0;
    invulnerableUntilMs = 0;
}

void BubbleBobbleScene::update(unsigned long deltaTime) {
    if (bub == nullptr) {
        Scene::update(deltaTime);
        return;
    }

    // Clamp: the very first frame's deltaTime includes all of setup()'s
    // overhead (~1s) — fed straight through, that one frame's fall distance
    // (velocity*dt) is large enough to jump clean over every platform and
    // the vertical-wraparound check in a single step, since collision here
    // is an endpoint-overlap test (checked at the target position, then
    // binary-searched back only if THAT overlaps something) rather than a
    // true continuous sweep along the whole path. A stray slow frame later
    // (GC-style stalls don't exist here, but a long screenshot capture
    // mid-frame could still do it) would do the same thing.
    constexpr unsigned long kMaxFrameMs = 50;
    if (deltaTime > kMaxFrameMs) deltaTime = kMaxFrameMs;

    // R3 held for PAUSE_HOLD_MS toggles pause — checked before anything
    // else so it still works while paused (to resume). pauseLatched avoids
    // re-toggling every frame the switch stays held past the threshold.
    // Holding well past that (kReturnToMenuHoldMs) requests a return to
    // the arcade menu instead — one continuous gesture ("hold longer to
    // back all the way out"), reviving this console's old "hold to force
    // back to the menu" convention (see tools/rig.py's hold_latch() doc
    // comment referencing "the 2s global L3 exit"). main.cpp polls
    // wantsReturnToMenu() once per loop() and calls
    // clearReturnToMenuRequest() after acting on it.
    if (pendingInput.r3Held) {
        if (encSwHeldSinceMs == 0) encSwHeldSinceMs = millis();
        unsigned long heldMs = millis() - encSwHeldSinceMs;
        if (!pauseLatched && heldMs >= PAUSE_HOLD_MS) {
            paused = !paused;
            pauseLatched = true;
        }
        constexpr unsigned long kReturnToMenuHoldMs = 2000;
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
        // Graphical debug level select (temporary — remove along with
        // debugNextLevel()/debugPrevLevel() once the 5 new levels are
        // confirmed on hardware): while paused, rotating the encoder
        // jumps straight to the next/previous level and re-freezes on it,
        // so every level's actual layout can be browsed on the real
        // screen (round-number HUD pill updates live) without touching
        // serial at all. debugNextLevel()/debugPrevLevel() call init(),
        // which clears `paused` as a side effect (a normal level
        // change always does) — re-assert it immediately so scrubbing
        // doesn't accidentally resume gameplay on whatever level was
        // showing when a tick landed.
        //
        // Ticks accumulate rather than firing on every nonzero encDelta:
        // one physical detent generates several raw interrupt-level ticks
        // (same reason menu.cpp's own encoder nav uses a 4-tick threshold,
        // ENC_STEP_THRESHOLD), so triggering on any nonzero delta made a
        // single indent jump several levels at once (reported live as
        // "unusable"). kMenuStepTicks matches that same established
        // per-detent tick count.
        debugLevelEncAccum += static_cast<int>(pendingInput.encDelta);
        constexpr int kMenuStepTicks = 4;
        if (debugLevelEncAccum >= kMenuStepTicks) {
            debugNextLevel();
            paused = true;
            debugLevelEncAccum = 0;
        } else if (debugLevelEncAccum <= -kMenuStepTicks) {
            debugPrevLevel();
            paused = true;
            debugLevelEncAccum = 0;
        }
        // A immediately starts play on whatever level is currently
        // showing — the only other way out was holding R3 again for
        // the same PAUSE_HOLD_MS as entering pause, which isn't
        // discoverable as "start" (reported live: no way to actually
        // start the selected level). Clears the same pause-hold-tracking
        // state the R3 toggle above would, so re-holding R3
        // afterward doesn't immediately re-toggle from stale state.
        if (pendingInput.aEdge) {
            paused = false;
            pauseLatched = false;
            encSwHeldSinceMs = 0;
        }
        Scene::update(deltaTime);
        return;
    }
    debugLevelEncAccum = 0;

    // "Descend through the tower" level transition: gameplay is fully
    // frozen (no input processed) while this runs — draw() does the actual
    // animating, scrolling the render offset over the next level's
    // already-built preview (see buildLevelPreview()). Once the timer's
    // up, advanceLevel() does the real (non-preview) rebuild.
    if (transitioning) {
        if (millis() - transitionStartMs >= kTransitionMs) {
            transitioning = false;
            advanceLevel();
            return;
        }
        Scene::update(deltaTime);
        return;
    }

    // Level clear: checked once at the top of the frame (before any of this
    // frame's mutation) rather than right where an enemy is destroyed
    // (inside updateBubbles()'s loop over bubbles) — advanceLevel() calls
    // init(), which would invalidate the very array that loop is iterating.
    // One frame with input skipped right at a transition is unnoticeable.
    // Clearing all enemies doesn't advance immediately: it starts a short
    // bonus round (fruit rains down for the player to grab) first, matching
    // the real game — the transition (and then advanceLevel()) only fires
    // once that timer ends.
    if (bonusRoundActive) {
        if (millis() >= bonusRoundEndMs) {
            bonusRoundActive = false;
            transitioning = true;
            transitionStartMs = millis();
            buildLevelPreview((currentLevelIndex + 1) % NUM_LEVELS, (float)SCREEN_H);
            return;
        }
    } else {
        bool anyAlive = false;
        for (int i = 0; i < kMaxEnemies; ++i) {
            if (enemies[i] != nullptr && enemies[i]->alive) { anyAlive = true; break; }
        }
        if (!anyAlive) {
            startBonusRound();
        }
    }

    const float thumbX = pendingInput.thumbX;
    const float thumbY = pendingInput.thumbY;
    const long enc = pendingInput.encDelta;

    const float dtSeconds = static_cast<float>(deltaTime) * 0.001f;
    const Scalar dt = toScalar(dtSeconds);

    // Gravity — KinematicActor applies none automatically (unlike RigidActor).
    // Capped: at normal frame times (~30ms) an unbounded fall speed eventually
    // makes velocity*dt exceed the platforms' 8px thickness too, tunneling
    // through for the same endpoint-overlap reason as above. Real Bubble
    // Bobble has no terminal velocity either — this is purely a collision-
    // robustness clamp, tuned comfortably under 8px/frame at ~30ms frames.
    float vy = static_cast<float>(velocityY) + GRAVITY * dtSeconds;
    if (vy > MAX_FALL_SPEED) vy = MAX_FALL_SPEED;

    bool jumpEdge = pendingInput.rbEdge;
    if (fabsf(thumbY) < THUMB_JUMP_RELEASE) thumbJumpLatched = false;
    if (!thumbJumpLatched && thumbY > THUMB_JUMP_TRIGGER) {
        thumbJumpLatched = true;
        jumpEdge = true;
    }
    if (jumpEdge && bub->is_on_floor()) {
        vy = JUMP_VELOCITY;
        bub->clearFloorVelocity();
    }

    float vx = thumbX * WALK_SPEED;
    if (enc != 0 && dtSeconds > 0.0f) {
        vx += (static_cast<float>(enc) * ENC_NUDGE) / dtSeconds;
    }

    // The one-way-platform crossing check in CollisionSystem reads the
    // actor's *stored* velocity (getVelocity()), not the vector passed to
    // moveAndSlide() — leaving it at its default {0,0} silently confuses
    // that check, so keep it in sync every frame.
    Vector2 frameVelocity(toScalar(vx), toScalar(vy));
    bub->setVelocity(frameVelocity);
    Vector2 resolved = bub->moveAndSlide(frameVelocity, dt);
    velocityY = resolved.y;

    // Defensive auto-unstick: a one-way platform's side edge can, in a
    // corner-overlap case, get misread as a solid wall and pin BOTH axes of
    // motion at zero indefinitely regardless of input (reproduced live on
    // hardware even after narrowing Bub's hitbox — see BUB_COLLISION_W's
    // comment). If Bub is airborne, wedged against a "wall", and genuinely
    // not moving for more than a few frames, nudge it down a few pixels to
    // break the false contact rather than leaving it permanently stranded.
    bool wedged = bub->is_on_wall() && !bub->is_on_floor() &&
                  fabsf(static_cast<float>(velocityY)) < 1.0f;
    stuckFrameCount = wedged ? stuckFrameCount + 1 : 0;
    if (stuckFrameCount > 10) {
        bub->setPosition(Vector2(bub->position.x, bub->position.y + toScalar(4.0f)));
        velocityY = toScalar(0.0f);
        stuckFrameCount = 0;
    }

    // Side walls: old game clamped instead of walling — same clamp here.
    // Clamped so the VISUAL sprite (wider than the collision box — see
    // BUB_VISUAL_OFFSET_X) stays fully on screen, not just the hitbox.
    if (bub->position.x < toScalar((float)BUB_VISUAL_OFFSET_X)) {
        bub->setPosition(Vector2(toScalar((float)BUB_VISUAL_OFFSET_X), bub->position.y));
    } else if (bub->position.x > toScalar((float)(SCREEN_W - BUB_W + BUB_VISUAL_OFFSET_X))) {
        bub->setPosition(Vector2(toScalar((float)(SCREEN_W - BUB_W + BUB_VISUAL_OFFSET_X)), bub->position.y));
    }

    // Vertical wraparound: falling fully off the bottom reappears at the top.
    if (static_cast<float>(bub->position.y) > SCREEN_H) {
        bub->setPosition(Vector2(bub->position.x, toScalar((float)(-BUB_COLLISION_H))));
        velocityY = toScalar(0.0f);
    }

    bool movingHoriz = fabsf(vx) > 1.0f;
    if (vx > 1.0f) lastFacingRight = true;
    else if (vx < -1.0f) lastFacingRight = false;
    bub->setAnimState(lastFacingRight, movingHoriz, !bub->is_on_floor());

    // A is the arcade's single action button (blow a bubble); LB
    // doubles it, matching the old game's "A/LB blow a bubble"
    // convention documented in the old bubblebobble.cpp.
    if (pendingInput.aEdge || pendingInput.lbEdge) {
        spawnCaptureBubble();
    }

    updateEnemies(dtSeconds);
    updateBubbles(dtSeconds);
    updateFood(dtSeconds);
    updateBoulders(dtSeconds);

    Scene::update(deltaTime);
}

void BubbleBobbleScene::updateEnemies(float dt) {
    if (bub == nullptr) return;
    float bubFeetX0 = static_cast<float>(bub->position.x);
    float bubFeetY0 = static_cast<float>(bub->position.y);
    unsigned long nowMs = millis();

    for (int i = 0; i < kMaxEnemies; ++i) {
        ZenChanActor* e = enemies[i];
        if (e == nullptr || !e->alive || e->trapped) continue;

        float speed = ENEMY_SPEED_BASE * (e->angry ? ANGRY_MULT : 1.0f);
        e->timeSinceLastTurn += dt;
        e->timeSinceLastJump += dt;
        e->timeSinceLastFacePlayer += dt;

        float ex = static_cast<float>(e->position.x);
        float ey = static_cast<float>(e->position.y);
        float dx = bubFeetX0 - ex;
        float dy = bubFeetY0 - ey;
        bool walkingTowardPlayer = (e->facing > 0) == (dx > 0);

        // Turn around at a solid screen wall (cooldown avoids jitter right at
        // the wall) — matches Bub's own side-wall clamp bounds (visual sprite
        // width, not just the collision box).
        bool atWall = (e->facing > 0 && ex + ZC_COLLISION_W >= (float)(SCREEN_W - ZC_VISUAL_OFFSET_X) - 1.0f) ||
                      (e->facing < 0 && ex <= (float)ZC_VISUAL_OFFSET_X + 1.0f);
        if (atWall && e->timeSinceLastTurn >= MIN_TIME_BETWEEN_WALL_TURN) {
            e->facing = -e->facing;
            e->timeSinceLastTurn = 0.0f;
            walkingTowardPlayer = !walkingTowardPlayer;
        }

        // Periodically re-bias toward the player even without hitting a
        // wall (ported from ZenChanBehaviour's "face player" timer).
        if (e->timeSinceLastFacePlayer > e->facePlayerInterval && fabsf(dx) > FACE_PLAYER_MIN_DIST) {
            if (!walkingTowardPlayer) {
                e->facing = -e->facing;
                walkingTowardPlayer = true;
            }
            e->timeSinceLastFacePlayer = 0.0f;
            e->facePlayerInterval = 2.0f + (float)(millis() % 1500) / 1000.0f;
        }

        float evx = e->facing * speed;
        float evy = e->velocityY + GRAVITY * dt;
        if (evy > MAX_FALL_SPEED) evy = MAX_FALL_SPEED;

        // Climb toward the player when directly below them with a platform
        // in reach overhead — purposeful, not a random idle hop (ported
        // from ZenChanBehaviour's jump condition).
        bool playerAbove = dy < -10.0f;
        bool onFloorNow = e->is_on_floor();
        bool platformAbove = onFloorNow && rowAbove(ey + ZC_COLLISION_H) >= 0 &&
                              platformSolidAt(currentLevelIndex, rowAbove(ey + ZC_COLLISION_H), ex, ex + ZC_COLLISION_W);
        if (onFloorNow && playerAbove && platformAbove && walkingTowardPlayer &&
            e->timeSinceLastJump > e->jumpInterval) {
            evy = ENEMY_JUMP_VELOCITY;
            e->timeSinceLastJump = 0.0f;
            e->jumpInterval = 1.0f + (float)(millis() % 500) / 1000.0f;
        }

        // Maita's real defining trait (Maita.h/MaitaBehaviour.cpp,
        // BOULDER_THROW_INTERVAL) — a periodic lobbed rock, not just a
        // faster/re-skinned Zen-Chan. Thrown toward wherever it's currently
        // facing (the reference throws toward whichever side the player is
        // on, which is what `facing` already tracks via the face-player
        // bias above).
        e->timeSinceLastThrow += dt;
        if (e->isMaita && e->timeSinceLastThrow >= e->throwInterval) {
            e->timeSinceLastThrow = 0.0f;
            e->throwInterval = 3.5f + (float)(millis() % 1000) / 1000.0f;
            spawnBoulder(ex + ZC_COLLISION_W / 2.0f, ey, (float)e->facing * BOULDER_SPEED);
        }

        Vector2 frameVelocity(toScalar(evx), toScalar(evy));
        e->setVelocity(frameVelocity);
        Vector2 resolved = e->moveAndSlide(frameVelocity, toScalar(dt));
        e->velocityY = static_cast<float>(resolved.y);

        // Side walls: same visual-sprite-stays-on-screen clamp as Bub.
        if (e->position.x < toScalar((float)ZC_VISUAL_OFFSET_X)) {
            e->setPosition(Vector2(toScalar((float)ZC_VISUAL_OFFSET_X), e->position.y));
        } else if (e->position.x > toScalar((float)(SCREEN_W - ZC_W + ZC_VISUAL_OFFSET_X))) {
            e->setPosition(Vector2(toScalar((float)(SCREEN_W - ZC_W + ZC_VISUAL_OFFSET_X)), e->position.y));
        }
        // Vertical wraparound, same as Bub.
        if (static_cast<float>(e->position.y) > SCREEN_H) {
            e->setPosition(Vector2(e->position.x, toScalar((float)(-ZC_COLLISION_H))));
            e->velocityY = 0.0f;
        }

        // Touching a live (untrapped) enemy knocks Bub away and starts a
        // brief invulnerability/blink window — real lives/HUD are still
        // Phase 4, but this needed to be an actual, VISIBLE reaction: the
        // first version teleported Bub back to its fixed spawn point, which
        // sits in the same busy area the enemies congregate in, so a hit
        // was reported live as "isn't hurting me" / "an enemy always ends
        // up on top of me" — it looked like nothing happened because
        // "nothing happened" was almost literally true (Bub reappeared
        // right back where an enemy could immediately touch it again, with
        // no cooldown to prevent an instant repeat).
        // Checked against the wider HURTBOX, not the narrow 16px physics
        // collision box — that box is trimmed 8px per side purely so Bub/
        // Zen-Chan can pass through the level's platform gaps without
        // corner-grazing (see BUB_COLLISION_W's comment), so two sprites
        // touching by physics-box measure were already visually stacked on
        // top of each other with nothing registering, reported live as
        // "stuck between the enemies... nothing happens when they touch me."
        if (nowMs >= invulnerableUntilMs &&
            aabbOverlap(hurtboxX(bubFeetX0), hurtboxY(bubFeetY0), (float)HURTBOX_W, (float)HURTBOX_H,
                        hurtboxX(static_cast<float>(e->position.x)), hurtboxY(static_cast<float>(e->position.y)),
                        (float)HURTBOX_W, (float)HURTBOX_H)) {
            constexpr float KNOCKBACK_X = 36.0f;
            constexpr float KNOCKBACK_UP = 24.0f;
            constexpr unsigned long INVULNERABLE_MS = 1500;
            float ex = static_cast<float>(e->position.x);
            float pushDir = (bubFeetX0 >= ex) ? 1.0f : -1.0f;
            float newX = bubFeetX0 + pushDir * KNOCKBACK_X;
            if (newX < (float)BUB_VISUAL_OFFSET_X) newX = (float)BUB_VISUAL_OFFSET_X;
            if (newX > (float)(SCREEN_W - BUB_W + BUB_VISUAL_OFFSET_X)) {
                newX = (float)(SCREEN_W - BUB_W + BUB_VISUAL_OFFSET_X);
            }
            float newY = bubFeetY0 - KNOCKBACK_UP;
            bub->setPosition(Vector2(toScalar(newX), toScalar(newY)));
            velocityY = toScalar(-100.0f);
            invulnerableUntilMs = nowMs + INVULNERABLE_MS;
            bubFeetX0 = newX;
            bubFeetY0 = newY;
            // loseLife() may trigger a full level reset (out of lives),
            // which invalidates `enemies`/`bub`/everything this loop is
            // touching — bail out immediately rather than continuing to
            // iterate over now-stale actors.
            if (loseLife()) return;
        }
    }

    bub->setVisible(nowMs < invulnerableUntilMs ? ((nowMs / 100) % 2 == 0) : true);
}

void BubbleBobbleScene::spawnCaptureBubble() {
    if (bub == nullptr) return;
    for (int i = 0; i < kMaxBubbles; ++i) {
        if (bubbles[i].active) continue;
        CaptureBubble& b = bubbles[i];
        b.active = true;
        b.floating = false;
        int facing = lastFacingRight ? 1 : -1;
        b.x = static_cast<float>(bub->position.x) + (facing > 0 ? BUB_COLLISION_W : -BUBBLE_W);
        b.y = static_cast<float>(bub->position.y) + (BUB_COLLISION_H - BUBBLE_H) / 2.0f;
        b.vx = facing * BUBBLE_TRAVEL_SPEED;
        b.vy = -14.0f;
        b.spawnMs = millis();
        b.travelEndMs = b.spawnMs + (unsigned long)(BUBBLE_TRAVEL_TIME * 1000);
        b.trappedEnemyIdx = -1;
        return;
    }
}

void BubbleBobbleScene::popBubble(int idx, bool chainSource) {
    CaptureBubble& b = bubbles[idx];
    if (!b.active) return;
    float cx = b.x + BUBBLE_W / 2.0f, cy = b.y + BUBBLE_H / 2.0f;
    bool hadEnemy = b.trappedEnemyIdx >= 0;
    b.active = false;
    if (hadEnemy) {
        ZenChanActor* e = enemies[b.trappedEnemyIdx];
        if (e != nullptr) {
            e->alive = false;
            e->trapped = false;
        }
        score += 100;
        // Food no longer drops per-kill — it drops all at once as the
        // bonus round after a level is cleared (matches the real game:
        // "you get 1 fruit for every enemy you kill, at the end"), so just
        // tally the kill here for startBonusRound() to use.
        ++enemiesKilledThisLevel;
        // Chain: pop any other trapped bubble close enough, once.
        if (!chainSource) {
            for (int i = 0; i < kMaxBubbles; ++i) {
                if (i == idx || !bubbles[i].active || bubbles[i].trappedEnemyIdx < 0) continue;
                float ox = bubbles[i].x + BUBBLE_W / 2.0f, oy = bubbles[i].y + BUBBLE_H / 2.0f;
                if (fabsf(ox - cx) <= CHAIN_RADIUS && fabsf(oy - cy) <= CHAIN_RADIUS) {
                    popBubble(i, true);
                }
            }
        }
    }
}

void BubbleBobbleScene::updateBubbles(float dt) {
    if (bub == nullptr) return;
    unsigned long now = millis();
    float bubX = static_cast<float>(bub->position.x);
    float bubY = static_cast<float>(bub->position.y);

    for (int i = 0; i < kMaxBubbles; ++i) {
        CaptureBubble& b = bubbles[i];
        if (!b.active) continue;

        if (!b.floating && now >= b.travelEndMs) {
            b.floating = true;
            b.vx = 0;
            b.vy = BUBBLE_RISE_SPEED;
        }

        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.x < 0.0f) b.x = 0.0f;
        if (b.x > (float)(SCREEN_W - BUBBLE_W)) b.x = (float)(SCREEN_W - BUBBLE_W);
        if (b.y < PLAYFIELD_TOP) { b.y = PLAYFIELD_TOP; b.vy = 0; }

        // An empty (untrapped) bubble can catch a live, untrapped enemy.
        if (b.trappedEnemyIdx < 0) {
            for (int j = 0; j < kMaxEnemies; ++j) {
                ZenChanActor* e = enemies[j];
                if (e == nullptr || !e->alive || e->trapped) continue;
                // Checked against the enemy's wider hurtbox, not its narrow
                // 16px physics collision box — same reasoning as the
                // player-vs-enemy touch fix: the narrow box is trimmed
                // purely for platform-gap clearance and made a visually
                // touching bubble/enemy fail to register as a catch.
                if (aabbOverlap(b.x, b.y, (float)BUBBLE_W, (float)BUBBLE_H,
                                hurtboxX(static_cast<float>(e->position.x)),
                                hurtboxY(static_cast<float>(e->position.y)),
                                (float)HURTBOX_W, (float)HURTBOX_H)) {
                    // Rises immediately (correct — it shouldn't keep flying
                    // sideways once it's caught something), but deliberately
                    // does NOT set b.floating here: that flag also gates
                    // poppability, and catching an enemy right next to Bub
                    // (the common case — you're standing close when you
                    // fire) let the bubble become poppable on the very next
                    // frame, so it popped against Bub's own hurtbox
                    // immediately. Reported live as "shooting an enemy just
                    // makes it disappear" instead of floating up for the
                    // ~1s grace period first. Leaving b.floating alone here
                    // means the ONLY place that sets it (the elapsed-time
                    // check below) still gates poppability by real time,
                    // even for a bubble that caught something mid-flight.
                    e->trapped = true;
                    b.trappedEnemyIdx = j;
                    b.vx = 0;
                    b.vy = BUBBLE_RISE_SPEED;
                    break;
                }
            }
        }

        if (now - b.spawnMs >= BUBBLE_LIFETIME_MS) {
            if (b.trappedEnemyIdx >= 0) {
                // Not popped in time — the trapped monster escapes, angrier.
                ZenChanActor* e = enemies[b.trappedEnemyIdx];
                if (e != nullptr) {
                    e->trapped = false;
                    e->angry = true;
                    e->setPosition(Vector2(toScalar(b.x + (BUBBLE_W - ZC_COLLISION_W) / 2.0f),
                                            toScalar(b.y + (BUBBLE_H - ZC_COLLISION_H) / 2.0f)));
                    e->facing = (millis() & 1) ? 1 : -1;
                    e->velocityY = 0.0f;
                }
            }
            b.active = false;
            continue;
        }

        // Player walking/jumping into a floating bubble pops it — not during
        // the initial travel arc (matches the ~1s grace period before a
        // capture bubble becomes poppable). Wider hurtbox, same reasoning
        // as the enemy-catch check just above.
        if (b.floating &&
            aabbOverlap(hurtboxX(bubX), hurtboxY(bubY), (float)HURTBOX_W, (float)HURTBOX_H,
                        b.x, b.y, (float)BUBBLE_W, (float)BUBBLE_H)) {
            popBubble(i, false);
            continue;
        }
    }
}

void BubbleBobbleScene::drawBubbles(Renderer& renderer) {
    namespace spr = bubblebobble_pr32_sprites;
    for (int i = 0; i < kMaxBubbles; ++i) {
        CaptureBubble& b = bubbles[i];
        if (!b.active) continue;
        renderer.drawSprite(spr::BB_BUBBLE, static_cast<int>(b.x), static_cast<int>(b.y), false);
        if (b.trappedEnemyIdx >= 0) {
            int ex = static_cast<int>(b.x) + (BUBBLE_W - ZC_W) / 2;
            int ey = static_cast<int>(b.y) + (BUBBLE_H - ZC_H) / 2;
            ZenChanActor* trapped = enemies[b.trappedEnemyIdx];
            bool trappedIsMaita = trapped != nullptr && trapped->isMaita;
            renderer.drawSprite(trappedIsMaita ? spr::BB_MAITA_WALK0_R : spr::BB_ZC_WALK0_R, ex, ey, false);
        }
    }
}

void BubbleBobbleScene::spawnFood(float cx, float cy) {
    for (int i = 0; i < kMaxFood; ++i) {
        if (foods[i].active) continue;
        FoodItem& f = foods[i];
        f.active = true;
        f.x = cx - FOOD_W / 2.0f;
        f.y = cy - FOOD_H / 2.0f;
        f.vy = 0.0f;
        f.onGround = false;
        f.spawnMs = millis();
        f.spriteIdx = (millis() & 1) ? 1 : 0;
        return;
    }
}

void BubbleBobbleScene::updateFood(float dt) {
    if (bub == nullptr) return;
    unsigned long now = millis();
    float bubFeetX = static_cast<float>(bub->position.x);
    float bubFeetY = static_cast<float>(bub->position.y);

    for (int i = 0; i < kMaxFood; ++i) {
        FoodItem& f = foods[i];
        if (!f.active) continue;

        if (now - f.spawnMs >= FOOD_LIFETIME_MS) {
            f.active = false;
            continue;
        }

        applyVerticalPhysics(currentLevelIndex, f.x, FOOD_W, f.y, f.vy, f.onGround, dt, FOOD_H);

        if (aabbOverlap(hurtboxX(bubFeetX), hurtboxY(bubFeetY), (float)HURTBOX_W, (float)HURTBOX_H,
                        f.x, f.y, (float)FOOD_W, (float)FOOD_H)) {
            score += FOOD_VALUES[f.spriteIdx];
            f.active = false;
        }
    }
}

void BubbleBobbleScene::drawFood(Renderer& renderer) {
    namespace spr = bubblebobble_pr32_sprites;
    for (int i = 0; i < kMaxFood; ++i) {
        FoodItem& f = foods[i];
        if (!f.active) continue;
        const pr32::graphics::Sprite4bpp& sprite =
            (f.spriteIdx == 0) ? spr::BB_FOOD_WATERMELON : spr::BB_FOOD_FRIES;
        renderer.drawSprite(sprite, static_cast<int>(f.x), static_cast<int>(f.y), false);
    }
}

void BubbleBobbleScene::spawnBoulder(float cx, float cy, float vx) {
    for (int i = 0; i < kMaxBoulders; ++i) {
        if (boulders[i].active) continue;
        Boulder& b = boulders[i];
        b.active = true;
        b.x = cx - BOULDER_W / 2.0f;
        b.y = cy - BOULDER_H / 2.0f;
        b.vx = vx;
        b.vy = 0.0f;
        b.spawnMs = millis();
        return;
    }
}

void BubbleBobbleScene::updateBoulders(float dt) {
    if (bub == nullptr) return;
    unsigned long now = millis();
    float bubFeetX = static_cast<float>(bub->position.x);
    float bubFeetY = static_cast<float>(bub->position.y);

    for (int i = 0; i < kMaxBoulders; ++i) {
        Boulder& b = boulders[i];
        if (!b.active) continue;

        if (now - b.spawnMs >= BOULDER_LIFETIME_MS) {
            b.active = false;
            continue;
        }

        b.x += b.vx * dt;
        // No arc, no bounce (see BOULDER tuning comment) — a straight roll
        // that simply vanishes once it reaches either side wall, same as a
        // real rolling hazard running out of floor.
        if (b.x < 0.0f || b.x > (float)(SCREEN_W - BOULDER_W)) {
            b.active = false;
            continue;
        }

        // Same hurtbox/knockback/invulnerability treatment as an enemy
        // touch (see updateEnemies()'s comment) — a boulder hit is real
        // damage, not just a cosmetic bounce.
        if (now >= invulnerableUntilMs &&
            aabbOverlap(hurtboxX(bubFeetX), hurtboxY(bubFeetY), (float)HURTBOX_W, (float)HURTBOX_H,
                        b.x, b.y, (float)BOULDER_W, (float)BOULDER_H)) {
            b.active = false;
            constexpr float KNOCKBACK_X = 36.0f;
            constexpr float KNOCKBACK_UP = 24.0f;
            constexpr unsigned long INVULNERABLE_MS = 1500;
            float pushDir = (b.vx >= 0.0f) ? 1.0f : -1.0f;
            float newX = bubFeetX + pushDir * KNOCKBACK_X;
            if (newX < (float)BUB_VISUAL_OFFSET_X) newX = (float)BUB_VISUAL_OFFSET_X;
            if (newX > (float)(SCREEN_W - BUB_W + BUB_VISUAL_OFFSET_X)) {
                newX = (float)(SCREEN_W - BUB_W + BUB_VISUAL_OFFSET_X);
            }
            float newY = bubFeetY - KNOCKBACK_UP;
            bub->setPosition(Vector2(toScalar(newX), toScalar(newY)));
            velocityY = toScalar(-100.0f);
            invulnerableUntilMs = now + INVULNERABLE_MS;
            // loseLife() may trigger a full level reset, invalidating `bub`/
            // `enemies`/every other array this frame's already touched —
            // bail out immediately rather than continuing this loop, same
            // discipline as updateEnemies()'s player-touch handler.
            if (loseLife()) return;
        }
    }
}

void BubbleBobbleScene::drawBoulders(Renderer& renderer) {
    // Plain filled circle, not a sprite — the shared custom sprite palette
    // is already at 15/16 slots (Bub/Zen-Chan/Maita/bubble/food), so a new
    // sprite would need yet another color merge. Drawn under the
    // Background context like PlatformActor/HUD text, same reasoning: it's
    // a plain-color primitive, not a real sprite, so it costs nothing
    // against that tight budget.
    namespace gfx = pixelroot32::graphics;
    gfx::PaletteContext bgContext = gfx::PaletteContext::Background;
    gfx::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);
    for (int i = 0; i < kMaxBoulders; ++i) {
        Boulder& b = boulders[i];
        if (!b.active) continue;
        renderer.drawFilledCircle(static_cast<int>(b.x) + BOULDER_W / 2,
                                   static_cast<int>(b.y) + BOULDER_H / 2,
                                   BOULDER_W / 2, Color::DarkRed);
    }
    renderer.setRenderContext(saved);
}

void BubbleBobbleScene::drawHUD(Renderer& renderer) {
    // Layout matches the real NES version's HUD (checked against actual
    // screenshots this session): score top-left, round number top-center
    // in a small boxed pill, lives count bottom-left corner.
    namespace gfx = pixelroot32::graphics;
    gfx::PaletteContext bgContext = gfx::PaletteContext::Background;
    gfx::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", score);
    renderer.drawText(buf, 2, 2, Color::White, 1);

    snprintf(buf, sizeof(buf), "%02d", currentLevelIndex + 1);
    int roundTextX = SCREEN_W / 2 - 6;
    renderer.drawRectangle(roundTextX - 4, 0, 24, 11, Color::White);
    renderer.drawText(buf, roundTextX, 2, Color::White, 1);

    snprintf(buf, sizeof(buf), "%d", lives);
    renderer.drawText(buf, 2, SCREEN_H - 10, Color::White, 1);

    if (paused) {
        renderer.drawText("PAUSED", SCREEN_W / 2 - 21, SCREEN_H / 2 - 4, Color::White, 1);
        // Graphical debug level select hint (temporary — see the matching
        // comment in update()'s pause branch). Yellow so it visually reads
        // as a debug/dev affordance, distinct from the plain white "PAUSED".
        renderer.drawText("ROTATE: CHANGE LEVEL", SCREEN_W / 2 - 66, SCREEN_H / 2 + 8, Color::Yellow, 1);
        renderer.drawText("A: START LEVEL", SCREEN_W / 2 - 57, SCREEN_H / 2 + 18, Color::Yellow, 1);
    } else if (bonusRoundActive) {
        renderer.drawText("BONUS ROUND", SCREEN_W / 2 - 42, 16, Color::Yellow, 1);
    }

    renderer.setRenderContext(saved);
}

bool BubbleBobbleScene::loseLife() {
    // Called once per hit (from the enemy-touch handler, which already
    // applies knockback + invulnerability) — a hit with lives remaining
    // just costs a life; only actually running out triggers a full level
    // reset. No dedicated game-over screen yet (Phase 4 scope): running out
    // just restarts clean rather than freezing on a "GAME OVER" message.
    if (lives > 0) --lives;
    if (lives <= 0) {
        score = 0;
        lives = 3;
        currentLevelIndex = 0;
        init();
        return true;
    }
    return false;
}

// Debug-only level select (see the header's comment on debugNextLevel/
// debugPrevLevel) — same "wraps, keeps score/lives, full init() rebuild"
// shape as the real advanceLevel(), just triggered directly from a serial
// char instead of clearing every enemy first. Explicitly clears
// transitioning/bonusRoundActive so jumping levels mid-transition or
// mid-bonus-round can't leave either flag stuck on in the freshly
// init()'d level.
void BubbleBobbleScene::debugNextLevel() {
    transitioning = false;
    bonusRoundActive = false;
    currentLevelIndex = (currentLevelIndex + 1) % NUM_LEVELS;
    init();
}

void BubbleBobbleScene::debugPrevLevel() {
    transitioning = false;
    bonusRoundActive = false;
    currentLevelIndex = (currentLevelIndex - 1 + NUM_LEVELS) % NUM_LEVELS;
    init();
}

void BubbleBobbleScene::advanceLevel() {
    currentLevelIndex = (currentLevelIndex + 1) % NUM_LEVELS;
    init();
}

void BubbleBobbleScene::startBonusRound() {
    constexpr unsigned long kBonusRoundMs = 5000;
    bonusRoundActive = true;
    bonusRoundEndMs = millis() + kBonusRoundMs;
    // One fruit per enemy killed this level — not a fixed count (matches
    // the real game: "you get 1 fruit for every enemy you kill, at the
    // end", reported live after the first version always dropped a fixed
    // 6 regardless of how many kills actually happened).
    int fruitCount = enemiesKilledThisLevel;
    if (fruitCount > kMaxFood) fruitCount = kMaxFood;
    for (int i = 0; i < fruitCount; ++i) {
        // Random column inside the playfield (not on the side walls),
        // dropped from the top — applyVerticalPhysics() (already driving
        // ordinary food) carries each one down to whatever real platform
        // or the floor is below it, so they land on genuinely random spots
        // in whatever this level's actual layout happens to be.
        int col = 1 + (millis() + i * 37) % (COLS - 2);
        float cx = colX(col) + TILE_SIZE / 2.0f;
        float cy = (float)PLAYFIELD_TOP + TILE_SIZE;
        spawnFood(cx, cy);
    }
    enemiesKilledThisLevel = 0;
}

void BubbleBobbleScene::draw(Renderer& renderer) {
    // While transitioning, scroll everything (current level, still-alive
    // entities, and the next level's preview built SCREEN_H below it) up
    // and off, revealing the next level rising into view from underneath —
    // "descend through the tower" as one continuous scroll, not a cut.
    // Reset back to (0,0) before the HUD so it stays fixed on screen.
    if (transitioning) {
        float progress = (float)(millis() - transitionStartMs) / (float)kTransitionMs;
        if (progress > 1.0f) progress = 1.0f;
        renderer.setDisplayOffset(0, -static_cast<int>(progress * SCREEN_H));
    } else {
        // Always reset when not transitioning (not just right as it ends):
        // the renderer's offset persists between calls, so without this,
        // the frame right after the transition flag flips back to false
        // would still render at whatever scrolled offset the last
        // transitioning frame left behind.
        renderer.setDisplayOffset(0, 0);
    }
    Scene::draw(renderer);
    drawFood(renderer);
    drawBoulders(renderer);
    drawBubbles(renderer);
    renderer.setDisplayOffset(0, 0);
    drawHUD(renderer);
}

} // namespace bubblebobble_pr32
