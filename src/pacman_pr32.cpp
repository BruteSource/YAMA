#include "pacman_pr32.h"
#include "pacman_pr32_sprites.h"
#include "pacman_pr32_maze_bitmap.h"
#include "menu_pr32_font.h"
#include <graphics/Color.h>
#include <graphics/FontManager.h>
#include <cstdio>
#include <cstring>
#include <cmath>

// Temporary: set to 1 to trace player/ghost state to serial while
// debugging movement/tunnel-wrap/frightened-mode logic. Leave 0 normally
// (spams serial output). Used this session to confirm: tunnel wraparound
// (traced px jumping 8.0 -> 218.7 and continuing to decrease — genuinely
// reappearing on the opposite side, not a one-off teleport) and the
// frightened-mode flag (traced ghosts[i].frightened flipping to 1 for
// all 4 ghosts immediately after a power pellet and staying 1) both work
// correctly — in both cases a screenshot-only check looked inconclusive
// at a glance (screenshots don't capture intermediate motion, and
// distinguishing "scared blue/pink" from a couple of the ghosts' own
// normal colors is genuinely hard at this resolution), but the traced
// state was unambiguous.
#define PACMAN_DEBUG_TRACE 0

namespace pr32 = pixelroot32;

namespace pacman_pr32 {

using pr32::graphics::Color;
using pr32::graphics::Renderer;

// Same retro pixel font (Press Start 2P) the menu and Galaga use for HUD
// text, instead of PixelRoot32's plain built-in default font — reported
// live as still "looking like normal terminal text" before this. Only
// covers ASCII 32-90 (no lowercase); every string this scene draws is
// already all-caps.
const pixelroot32::graphics::Font* const kHudFont = &menu_pr32_font::FONT_SMALL;

namespace {

// ================= Maze data =================
// Decoded directly from sgothel/pacman's real level file
// (media/playfield_pacman.txt, MIT license) — the file's own header gives
// exact dimensions (28x31 net playable tiles) and metadata (scatter
// corners, tunnel zones, ghost-house boxes, player start) which are
// preserved here as constants/comments even before ghosts exist, so
// Phase C doesn't need to re-derive them.
//
// Tile legend (converted from the source file's own characters):
//   '#' = wall (solid, blocks the player)
//   '.' = pellet (10 points)
//   '*' = power pellet (50 points; no frightened-ghost effect yet — no
//         ghosts in this phase)
//   '-' = ghost-house gate (blocks the player like a wall; ghosts will
//         pass through it in Phase C)
//   ' ' = open floor, no pellet (source file used '_' for this — ghost
//         house interior and the tunnel row — renamed to ' ' here since
//         this project's other level data uses ' ' for "nothing here")
//
// Verified by direct count against the real arcade's known pellet total:
// 240 regular + 4 power = 244 dots, matching this decode exactly.
constexpr const char* const MAZE[kMazeRows] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#*#  #.#   #.##.#   #.#  #*#",
    "#.####.#####.##.#####.####.#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "     #.##### ## #####.#     ",
    "     #.##          ##.#     ",
    "     #.## ###--### ##.#     ",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "     #.## ######## ##.#     ",
    "     #.##          ##.#     ",
    "     #.## ######## ##.#     ",
    "######.## ######## ##.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#.####.#####.##.#####.####.#",
    "#*..##.......  .......##..*#",
    "###.##.##.########.##.##.###",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################",
};
static_assert(sizeof(MAZE) / sizeof(MAZE[0]) == kMazeRows, "row count");

// Player start position: the reference file's header literally says
// "14.0 26.0", but row 26 in THIS decode has walls at both columns 13
// AND 14 (part of the maze's recurring thin double-wall that splits
// several corridors into left/right halves — the same pattern appears at
// rows 1, 8, 20, and 23 too) — spawning there would place the player
// inside a wall, immediately blocking movement in most directions
// (caught live: the player appeared stuck, eating zero pellets, when
// driven left). The reference engine's row-0 origin evidently doesn't
// line up 1:1 with this decode's row indexing (cross-checked the OTHER
// header metadata like the ghost start box, which DOES land correctly on
// row 14 here, so this isn't a wholesale re-indexing bug, just this one
// coordinate). Row 29 ("#..........................#") is a real, fully
// open corridor near the same vertical position and is what's used
// instead — same column (14), verified open.
constexpr int kPlayerStartCol = 14;
constexpr int kPlayerStartRow = 29;

// Tunnel rows: the reference file's header lists two tunnel zones both at
// net row 17 (x=0..4 and x=23..27) — row 17 here (0-indexed) is
// "      .   #      #   .      ", whose outer columns are open (no '#'),
// confirming the wraparound corridor. Generalized as "any row where
// column 0 or kMazeCols-1 is not a wall" rather than hardcoding row 17,
// so a future different maze doesn't need this constant touched.
bool isTunnelRow(int row) {
    return MAZE[row][0] != '#' && MAZE[row][kMazeCols - 1] != '#';
}

constexpr int TILE_SIZE = 8;
constexpr int PLAYFIELD_LEFT = (SCREEN_W - kMazeCols * TILE_SIZE) / 2;  // 240-224=16 -> 8px margin
constexpr int PLAYFIELD_TOP = 40;   // room above for the score/lives HUD
// 320 - 40 - 31*8 = 32px left below for the pause/hint line.

int colToPx(int col) { return PLAYFIELD_LEFT + col * TILE_SIZE; }
int rowToPy(int row) { return PLAYFIELD_TOP + row * TILE_SIZE; }

// Player movement speed cap, in px/sec — the speed at level 7+ (see
// LEVEL_SPECS/applyLevelSpec() below); earlier levels run slower. This
// was the ORIGINAL flat speed used before level progression existed
// (80.0f), reported live as "very hard at this speed" even on a fresh
// game — so it became the CEILING level 1 ramps up to, not the starting
// speed. Still "still really hard" after that first pass, so the whole
// cap (this and the two below) is now scaled down another 30% on top of
// that — i.e. the entire difficulty CURVE shifted down, not just level
// 1's ratio within it, so the hardest levels get easier too.
constexpr float PLAYER_SPEED_CAP = 80.0f * 0.7f;  // 56

bool isWallTile(int col, int row) {
    if (row < 0 || row >= kMazeRows) return true;
    if (col < 0 || col >= kMazeCols) return false;  // tunnel: never a wall, handled by wraparound
    char t = MAZE[row][col];
    return t == '#' || t == '-';
}

// Ghosts now treat the house gate ('-') as a wall too, same as the
// player (isWallTile()) — with no house-exit/release logic implemented,
// ghosts never legitimately need to enter or leave through it (they
// spawn already outside, and an eaten ghost teleports straight back to
// its spawn point rather than pathing home through the gate). Letting
// them treat it as passable was the direct cause of the "ghosts just
// stay in the middle" bug: whenever the chase target pulled a ghost
// toward the gate, it would duck into the (otherwise sealed) house
// interior — a dead end — and thrash trying to get back out, since nothing
// biases the decision toward re-finding the one narrow exit chimney.
// `gatePassable` lets the two eyes-only BFS fields (computeHomeDirField/
// computeExitDirField) route through the house gate ('-') while ordinary
// chase/frightened decision-making still treats it as a wall (the
// default) — see Ghost::exiting's comment for why normal movement can't
// simply have this flipped back.
bool isWallTileForGhost(int col, int row, bool gatePassable = false) {
    if (row < 0 || row >= kMazeRows) return true;
    if (col < 0 || col >= kMazeCols) return false;
    char t = MAZE[row][col];
    if (t == '-') return !gatePassable;
    return t == '#';
}

bool isPelletTile(char t) { return t == '.' || t == '*'; }

// Ghost speed cap: slightly slower than the player's cap (classic arcade
// balance — Pac-Man can outrun a non-frightened ghost in a straight
// line), frightened ghosts slower still (also classic — makes them
// genuinely catchable after a power pellet). Like PLAYER_SPEED_CAP, this
// is the ceiling `applyLevelSpec()` ramps up to, not the starting speed
// — and like PLAYER_SPEED_CAP, scaled down another 30% (on top of the
// original 68.0f/45.0f) after "still really hard" feedback.
constexpr float GHOST_SPEED_CAP = 68.0f * 0.7f;             // ~47.6
constexpr float GHOST_FRIGHTENED_SPEED_CAP = 45.0f * 0.7f;  // ~31.5

// Eaten ghost eyes racing back to the ghost house — real arcade has
// these move noticeably faster than any other ghost state, not scaled
// by level like the other speeds (matches the reference: eye speed is
// flat regardless of level).
constexpr float GHOST_EYES_SPEED_CAP = 90.0f;

// Per-level difficulty table — ported in spirit from the reference's own
// `level_spec_array` (game.cpp, 21 real entries covering speed ramps,
// fright duration, and fruit type/score; quoted in this session's
// research). Reduced to a handful of representative steps rather than
// all 21 (this project has no scatter-wave timers or Elroy speedup to
// key off the finer-grained levels anyway — see the plan file). Speeds
// are stored as fractions of the *_CAP constants above, matching the
// reference's own 0.75-1.00-style ratios; fright duration and fruit
// score are absolute. Reported live as "very hard at this speed" even
// on a fresh game, so level 1 deliberately starts well below the old
// flat speed (which is now just the eventual ceiling) — matches the
// reference's own real level-1 pacing being its easiest.
struct LevelSpec {
    float ghostSpeedRatio;
    float ghostFrightSpeedRatio;
    unsigned long frightMs;
    int fruitScore;
};
// Fright durations were previously bumped well past the real arcade's
// own table (which is actually only 6000ms even at level 1) after being
// reported live as feeling much too short. Once the eaten/parkedInHouse/
// exiting round trip got fixed into a real (rather than instant)
// sequence, that earlier lengthening started reading as too generous the
// other way — "held in the box too long" — since the whole cycle (flee
// window + eyes travel home + parked wait + walk back out) all stacks on
// top of the same frightMs timer. Scaled back down close to the real
// arcade's own numbers (level 1 exactly matches it now) rather than the
// ~doubled figures from before.
constexpr LevelSpec LEVEL_SPECS[] = {
    { 0.75f, 0.50f, 6000, 100 },   // 1: cherry
    { 0.85f, 0.55f, 5000, 300 },   // 2: strawberry
    { 0.85f, 0.55f, 4500, 500 },   // 3: peach
    { 0.90f, 0.55f, 4000, 500 },   // 4: peach
    { 0.95f, 0.60f, 3500, 700 },   // 5: apple
    { 0.95f, 0.60f, 3500, 700 },   // 6: apple
    { 1.00f, 0.60f, 3000, 1000 },  // 7: melon
    { 1.00f, 0.60f, 3000, 1000 },  // 8: melon (cap reached; repeats past here)
};
constexpr int kNumLevelSpecs = sizeof(LEVEL_SPECS) / sizeof(LEVEL_SPECS[0]);
// Player speed is ramped the same way, using the reference's own
// pacman_speed ratios (0.80 at level 1 rising to 1.00) — kept as a
// separate small table since it doesn't need fright/fruit fields.
constexpr float PLAYER_SPEED_RATIOS[] = { 0.80f, 0.87f, 0.87f, 0.92f, 1.00f, 1.00f, 1.00f, 1.00f };
constexpr int kNumPlayerSpeedRatios = sizeof(PLAYER_SPEED_RATIOS) / sizeof(PLAYER_SPEED_RATIOS[0]);

// Spawn spots: row 8, a real open corridor segment, confirmed against
// the decoded MAZE data AND the reference's own box coordinates
// (media/playfield_pacman.txt: "ghost home exterior" is cols 10-17,
// rows 15-19; the "ghost start box" (13-14, row 14) sits INSIDE that
// enclosure, one gate-crossing below the still-enclosed row-11 vestibule
// this used to spawn ghosts in).
//
// **Real bug found this session**: row 11 (the previous spawn row) is
// NOT actually outside the ghost house — it's a walled-off antechamber
// directly above the row-12 gate, itself enclosed by a near-solid wall
// at rows 9-10 with only two single-tile-wide chimneys (columns 12 and
// 15) leading further up to the real open corridor (row 8). Since this
// project deliberately has no house-exit/red-zone pathing logic (see
// "Deliberately simplified" below), the ordinary chase-targeting
// distance-scoring almost always preferred ducking back down through the
// gate (a dead end) over hunting for the one correct narrow exit column
// — reported live as "the ghosts just stay in the middle." Fixed by
// spawning past that chokepoint entirely, in row 8's genuinely open
// corridor (confirmed no enclosing walls beyond the usual maze
// structure) — same "start already outside, no release logic needed"
// simplification, just at a position that's ACTUALLY outside.
struct GhostSpawn { GhostPersonality who; int col; int row; };
constexpr GhostSpawn GHOST_SPAWNS[4] = {
    { GhostPersonality::Blinky, 11, 8 },
    { GhostPersonality::Pinky, 16, 8 },
    { GhostPersonality::Inky, 10, 8 },
    { GhostPersonality::Clyde, 17, 8 },
};

// Where each ghost's EYES actually park after being eaten — the visible
// box interior (row 14, columns 13-16 all confirmed open floor between
// the walls at columns 10/18), NOT the same row-8 corridor tile
// GHOST_SPAWNS uses for each ghost's INITIAL appearance. Conflating
// these two was the real bug behind "eyes aren't making it to the spawn
// box, getting stuck right before it" — the eyes were correctly
// reaching their programmed target, it just wasn't inside the box.
constexpr GhostSpawn EYE_HOME_SPAWNS[4] = {
    { GhostPersonality::Blinky, 13, 14 },
    { GhostPersonality::Pinky, 14, 14 },
    { GhostPersonality::Inky, 15, 14 },
    { GhostPersonality::Clyde, 16, 14 },
};

// Clyde's real "scatter corner" (bottom-left) sits outside the actual
// grid in the reference file's own header data (deliberately, so the
// ghost heads toward-but-never-reaches it) — since this pass has no
// scatter mode at all, Clyde's retreat target only needs to be a
// plausible in-bounds point far from the action, not the exact
// reference coordinate.
//
// col=1 was reported live as visibly overlapping the left boundary
// wall — Clyde's 14x14 sprite is centered on its 8x8 logical tile (same
// "visual bigger than collision" idiom as the player), so at col 1
// (screen x 13-27) it overhangs a few pixels into column 0's wall tile
// (screen x 8-16). col=2 gives enough clearance that the sprite no
// longer overlaps the boundary wall at all, while still reading as "the
// bottom-left corner" — the reported "bouncing between the two bottom
// corners, stuck in the walls" was this overhang PLUS Clyde legitimately
// (by design) flip-flopping between this retreat point and chasing the
// player whenever the 8-tile distance threshold is crossed back and
// forth, not an actual escape from the maze (checked: ghosts cannot
// leave the declared tile grid, see isWallTileForGhost()).
constexpr int kClydeRetreatCol = 2;
constexpr int kClydeRetreatRow = 29;

// Bonus fruit spot: just below the ghost house, a real open/reachable
// floor tile in this decode (row 20's col 12; the maze's usual center
// double-wall splits cols 13/14 at this row, so 12 is the nearest open
// tile to "centered below the house"). Fixed position, not per-level like
// the reference — see the plan file for why per-level fruit variety was
// scoped out.
constexpr int kFruitCol = 12;
constexpr int kFruitRow = 20;
constexpr unsigned long kFruitDurationMs = 9500;
constexpr int kFruitThreshold1 = 70;
constexpr int kFruitThreshold2 = 170;

Color ghostColor(GhostPersonality p) {
    switch (p) {
        case GhostPersonality::Blinky: return Color::Red;
        case GhostPersonality::Pinky: return Color::LightRed;
        case GhostPersonality::Inky: return Color::Cyan;
        case GhostPersonality::Clyde: return Color::Orange;
    }
    return Color::White;
}

} // namespace

void PacManScene::init() {
    playerX = static_cast<float>(colToPx(kPlayerStartCol));
    playerY = static_cast<float>(rowToPy(kPlayerStartRow));
    dirX = 0;
    dirY = 0;
    wantDirX = 0;
    wantDirY = 0;
    animFrame = 0;
    animAccumulatorMs = 0;

    score = 0;
    lives = 3;
    level = 1;
    gameOver = false;
    invulnerableUntilMs = 0;
    paused = false;
    pauseLatched = false;
    encSwHeldSinceMs = 0;
    returnToMenuRequested = false;
    returnToMenuLatched = false;

    pelletsRemaining = 0;
    for (int r = 0; r < kMazeRows; ++r) {
        for (int c = 0; c < kMazeCols; ++c) {
            pelletEaten[r][c] = false;
            if (isPelletTile(MAZE[r][c])) ++pelletsRemaining;
        }
    }
    totalPellets = pelletsRemaining;
    fruitStage1Spawned = false;
    fruitStage2Spawned = false;
    fruitActive = false;
    fruitUntilMs = 0;
    applyLevelSpec();

    for (int i = 0; i < kNumGhosts; ++i) {
        ghosts[i].personality = GHOST_SPAWNS[i].who;
        ghosts[i].homeX = static_cast<float>(colToPx(GHOST_SPAWNS[i].col));
        ghosts[i].homeY = static_cast<float>(rowToPy(GHOST_SPAWNS[i].row));
        ghosts[i].eyeHomeX = static_cast<float>(colToPx(EYE_HOME_SPAWNS[i].col));
        ghosts[i].eyeHomeY = static_cast<float>(rowToPy(EYE_HOME_SPAWNS[i].row));
        ghosts[i].x = ghosts[i].homeX;
        ghosts[i].y = ghosts[i].homeY;
        ghosts[i].dirX = (i % 2 == 0) ? 1 : -1;
        ghosts[i].dirY = 0;
        ghosts[i].frightened = false;
        ghosts[i].frightenedUntilMs = 0;
        ghosts[i].eaten = false;
        ghosts[i].parkedInHouse = false;
        ghosts[i].exiting = false;
        ghosts[i].animFrame = 0;
        ghosts[i].animAccumulatorMs = 0;
        computeHomeDirField(i);
        computeExitDirField(i);
    }

    // Real arcade attract screen (ghost roster + demo) plays first, THEN
    // the intro jingle freeze — see drawTitleScreen()'s comment.
    // introActive/the jingle only start once titlePhase advances past 0
    // (in update()), not here.
    titlePhase = 0;
    titleTimer = 0.0f;
    introActive = false;

    // This scene's own custom sprite palette (Pac-Man's yellow walk
    // frames) — same "re-assert every init()" idiom
    // BubbleBobbleScene/MenuScene both already use, since the sprite
    // palette is one shared global table.
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);
    pr32::graphics::setSpriteCustomPalette(pacman_pr32_sprites::PALETTE_RGB565);
}

void PacManScene::applyLevelSpec() {
    int specIdx = level - 1;
    if (specIdx >= kNumLevelSpecs) specIdx = kNumLevelSpecs - 1;
    if (specIdx < 0) specIdx = 0;
    const LevelSpec& spec = LEVEL_SPECS[specIdx];

    int speedIdx = level - 1;
    if (speedIdx >= kNumPlayerSpeedRatios) speedIdx = kNumPlayerSpeedRatios - 1;
    if (speedIdx < 0) speedIdx = 0;

    currentPlayerSpeed = PLAYER_SPEED_CAP * PLAYER_SPEED_RATIOS[speedIdx];
    currentGhostSpeed = GHOST_SPEED_CAP * spec.ghostSpeedRatio;
    currentGhostFrightSpeed = GHOST_SPEED_CAP * spec.ghostFrightSpeedRatio;
    currentFrightMs = spec.frightMs;
    currentFruitScore = spec.fruitScore;
}

// Clearing every pellet advances to the next level rather than ending
// the game — real Pac-Man has no "win", just an endless (in practice,
// 255-level) climb. Keeps score/lives; resets pellets, fruit state, and
// player/ghost positions, then re-applies the new level's (harder)
// speed/fright spec.
void PacManScene::advanceLevel() {
    ++level;
    applyLevelSpec();
    sfx.play(pacman_pr32_audio::kLevelCompleteSfx, pacman_pr32_audio::kLevelCompleteSfxLen);

    pelletsRemaining = 0;
    for (int r = 0; r < kMazeRows; ++r) {
        for (int c = 0; c < kMazeCols; ++c) {
            pelletEaten[r][c] = false;
            if (isPelletTile(MAZE[r][c])) ++pelletsRemaining;
        }
    }
    totalPellets = pelletsRemaining;
    fruitStage1Spawned = false;
    fruitStage2Spawned = false;
    fruitActive = false;
    fruitUntilMs = 0;

    playerX = static_cast<float>(colToPx(kPlayerStartCol));
    playerY = static_cast<float>(rowToPy(kPlayerStartRow));
    dirX = 0;
    dirY = 0;
    wantDirX = 0;
    wantDirY = 0;

    for (int i = 0; i < kNumGhosts; ++i) {
        ghosts[i].x = ghosts[i].homeX;
        ghosts[i].y = ghosts[i].homeY;
        ghosts[i].dirX = (i % 2 == 0) ? 1 : -1;
        ghosts[i].dirY = 0;
        ghosts[i].frightened = false;
        ghosts[i].eaten = false;
        ghosts[i].parkedInHouse = false;
        ghosts[i].exiting = false;
    }
    invulnerableUntilMs = millis() + 1500;
}

void PacManScene::debugWarpToTunnel() {
    playerX = static_cast<float>(colToPx(2));
    playerY = static_cast<float>(rowToPy(14));
    dirX = -1;
    dirY = 0;
    wantDirX = -1;
    wantDirY = 0;
}

void PacManScene::debugWarpToPowerPellet() {
    playerX = static_cast<float>(colToPx(2));
    playerY = static_cast<float>(rowToPy(23));
    dirX = -1;
    dirY = 0;
    wantDirX = -1;
    wantDirY = 0;
}

void PacManScene::debugForceEatGhost() {
    ghosts[0].frightened = false;
    ghosts[0].eaten = true;
    ghosts[0].eatenTimer = 0.0f;
    ghosts[0].parkedInHouse = false;
    // Give it a real remaining window to travel/wait in, same as a real
    // mid-power-pellet eat would leave it.
    ghosts[0].frightenedUntilMs = millis() + currentFrightMs;
}

void PacManScene::debugSpawnFruit() {
    fruitActive = true;
    fruitUntilMs = millis() + kFruitDurationMs;
    playerX = static_cast<float>(colToPx(kFruitCol - 1));
    playerY = static_cast<float>(rowToPy(kFruitRow));
    dirX = 0;
    dirY = 0;
    wantDirX = 0;
    wantDirY = 0;
    // Parked right next to (not overlapping with) the ghost pack near the
    // house exit — give a brief invulnerability window so this debug test
    // exercises the fruit logic, not an incidental ghost collision.
    invulnerableUntilMs = millis() + 3000;
}

void PacManScene::debugAdvanceLevel() {
    advanceLevel();
}

void PacManScene::eatAt(int col, int row) {
    if (row < 0 || row >= kMazeRows || col < 0 || col >= kMazeCols) return;
    if (pelletEaten[row][col]) return;
    char t = MAZE[row][col];
    if (t == '.') {
        pelletEaten[row][col] = true;
        score += 10;
        --pelletsRemaining;
    } else if (t == '*') {
        pelletEaten[row][col] = true;
        score += 50;
        --pelletsRemaining;
        unsigned long until = millis() + currentFrightMs;
        for (int i = 0; i < kNumGhosts; ++i) {
            ghosts[i].frightened = true;
            ghosts[i].frightenedUntilMs = until;
            // Classic rule: a power pellet forces every ghost to reverse
            // immediately, not just turn frightened — reads as a much
            // more noticeable "oh no" moment than just a color change.
            ghosts[i].dirX = -ghosts[i].dirX;
            ghosts[i].dirY = -ghosts[i].dirY;
        }
        sfx.play(pacman_pr32_audio::kPowerPelletSfx, pacman_pr32_audio::kPowerPelletSfxLen);
    }
    if (pelletsRemaining <= 0) {
        advanceLevel();
        return;
    }

    int pelletsEaten = totalPellets - pelletsRemaining;
    if (!fruitStage1Spawned && pelletsEaten >= kFruitThreshold1) {
        fruitStage1Spawned = true;
        fruitActive = true;
        fruitUntilMs = millis() + kFruitDurationMs;
    } else if (!fruitStage2Spawned && pelletsEaten >= kFruitThreshold2) {
        fruitStage2Spawned = true;
        fruitActive = true;
        fruitUntilMs = millis() + kFruitDurationMs;
    }
}

void PacManScene::resetPlayerAndGhostPositions() {
    playerX = static_cast<float>(colToPx(kPlayerStartCol));
    playerY = static_cast<float>(rowToPy(kPlayerStartRow));
    dirX = 0;
    dirY = 0;
    wantDirX = 0;
    wantDirY = 0;
    for (int i = 0; i < kNumGhosts; ++i) {
        ghosts[i].x = ghosts[i].homeX;
        ghosts[i].y = ghosts[i].homeY;
        ghosts[i].dirX = (i % 2 == 0) ? 1 : -1;
        ghosts[i].dirY = 0;
        ghosts[i].frightened = false;
        ghosts[i].eaten = false;
        ghosts[i].parkedInHouse = false;
        ghosts[i].exiting = false;
    }
    invulnerableUntilMs = millis() + 1500;
}

void PacManScene::updatePlayer(float dt) {
    // Current tile (nearest grid cell to the player's logical position)
    // and how far into that tile's own 8px cell we've drifted on each
    // axis — used to decide "are we close enough to a tile boundary to
    // evaluate a turn or a wall block" without needing exact float
    // equality anywhere.
    int col = static_cast<int>((playerX - PLAYFIELD_LEFT) / TILE_SIZE + 0.5f);
    int row = static_cast<int>((playerY - PLAYFIELD_TOP) / TILE_SIZE + 0.5f);
    float tileOriginX = static_cast<float>(colToPx(col));
    float tileOriginY = static_cast<float>(rowToPy(row));
    bool atGridX = fabsf(playerX - tileOriginX) < 1.0f;
    bool atGridY = fabsf(playerY - tileOriginY) < 1.0f;

    // Try to switch to the queued direction whenever the player is
    // exactly grid-aligned on the perpendicular axis and the target tile
    // in that direction is open — classic "buffer the next turn, apply
    // it as soon as the current tile allows" queuing, matching the plan's
    // description of the reference project's own feel.
    if ((wantDirX != 0 || wantDirY != 0) && (wantDirX != dirX || wantDirY != dirY)) {
        bool canTurn = false;
        if (wantDirX != 0 && atGridY) {
            canTurn = !isWallTile(col + wantDirX, row);
        } else if (wantDirY != 0 && atGridX) {
            canTurn = !isWallTile(col, row + wantDirY);
        }
        if (canTurn) {
            playerX = tileOriginX;
            playerY = tileOriginY;
            dirX = wantDirX;
            dirY = wantDirY;
        }
    }

    if (dirX == 0 && dirY == 0) return;

    // Blocked by a wall ahead: stop exactly at the tile boundary instead
    // of drifting into it. Tunnel columns (col+dirX out of [0,COLS)) are
    // never walls (isWallTile() already returns false there), so this
    // naturally allows walking off either edge.
    bool aligned = (dirX != 0) ? atGridY : atGridX;
    if (aligned && isWallTile(col + dirX, row + dirY)) {
        playerX = tileOriginX;
        playerY = tileOriginY;
        dirX = 0;
        dirY = 0;
        return;
    }

    playerX += dirX * currentPlayerSpeed * dt;
    playerY += dirY * currentPlayerSpeed * dt;

#if PACMAN_DEBUG_TRACE
    static unsigned long lastTrace = 0;
    if (millis() - lastTrace > 150) {
        lastTrace = millis();
        Serial.printf("TRACE px=%.1f py=%.1f col=%d row=%d dir=(%d,%d)\n", playerX, playerY, col, row, dirX, dirY);
    }
#endif

    // Tunnel wraparound: once fully off one side, reappear at the other.
    // Only meaningful on tunnel rows (isWallTile already guarantees we
    // could only get here on one), but the bounds check alone is enough.
    float mazeLeftPx = static_cast<float>(colToPx(0));
    float mazeRightPx = static_cast<float>(colToPx(kMazeCols - 1));
    if (playerX < mazeLeftPx - TILE_SIZE) {
        playerX = mazeRightPx;
    } else if (playerX > mazeRightPx + TILE_SIZE) {
        playerX = mazeLeftPx;
    }

    // Re-derive the tile under the (possibly just-moved) position and eat
    // whatever pellet is there — checked every frame rather than only on
    // arrival so fast movement can't skip over a pellet between frames
    // (at PLAYER_SPEED/typical dt this can't actually happen, but the
    // check is cheap and robust regardless).
    int newCol = static_cast<int>((playerX - PLAYFIELD_LEFT) / TILE_SIZE + 0.5f);
    int newRow = static_cast<int>((playerY - PLAYFIELD_TOP) / TILE_SIZE + 0.5f);
    eatAt(newCol, newRow);

    if (fruitActive && newCol == kFruitCol && newRow == kFruitRow) {
        fruitActive = false;
        score += currentFruitScore;
        sfx.play(pacman_pr32_audio::kFruitSfx, pacman_pr32_audio::kFruitSfxLen);
    }
}

// Per-personality CHASE targeting, ported in spirit (not verbatim — this
// project's units/coordinate system differ) from sgothel/pacman's
// ghost_t::set_next_target() (src/ghost.cpp, quoted directly in this
// session's research, saved in the plan file):
//   Blinky: player's current tile, directly.
//   Pinky:  4 tiles ahead of the player in the player's facing direction
//           — including the authentic "up" overflow bug (facing up also
//           offsets 4 LEFT, not just up), since it's cheap to keep and is
//           a well-known, deliberate quirk of the original arcade, not a
//           bug to "fix" out.
//   Inky:   takes the vector from Blinky to the tile 2 ahead of the
//           player, doubles it, and adds it back to Blinky's own
//           position — genuinely needs Blinky's position, so Blinky must
//           be updated/positioned before Inky each frame (see the loop in
//           update()).
//   Clyde:  chases directly like Blinky UNTIL within 8 tiles of the
//           player, then retreats to a far corner instead — the classic
//           "shy" behavior that makes Clyde feel meaningfully different
//           up close.
// Deliberately NOT implemented: scatter-mode wave timers (real arcade
// alternates chase/scatter on a per-level timetable) — every ghost is
// always either chasing or (post-power-pellet) frightened.
// BFS flood from ghost `homeIdx`'s own home tile, over the exact same
// passable-tile graph normal ghost movement uses (isWallTileForGhost),
// recording for every reachable tile the first-step direction of its
// shortest path back to home. A plain queue-based BFS over <=868 tiles
// is trivially cheap (microseconds) and only runs once per ghost at
// init() time, since the maze layout is static.
void PacManScene::computeHomeDirField(int homeIdx) {
    constexpr int kDirUp = 1, kDirLeft = 2, kDirDown = 3, kDirRight = 4;
    constexpr int kOffsets[4][3] = {
        // {dx, dy, dirCodeToGetHereFromNeighborInOppositeDirection}
        { 0, -1, kDirDown },   // moved up from a tile below -> that tile's step-toward-home is "down"
        { -1, 0, kDirRight },  // moved left from a tile to the right -> "right"
        { 0, 1, kDirUp },      // moved down from a tile above -> "up"
        { 1, 0, kDirLeft },    // moved right from a tile to the left -> "left"
    };

    bool visited[kMazeRows][kMazeCols] = {};
    // Fixed-capacity BFS queue: at most one entry per tile.
    static int16_t queueCol[kMazeRows * kMazeCols];
    static int16_t queueRow[kMazeRows * kMazeCols];
    int qHead = 0, qTail = 0;

    // Target is the visible box interior (EYE_HOME_SPAWNS), not
    // GHOST_SPAWNS' row-8 corridor tile — and the gate must be passable
    // here (gatePassable=true) or the flood could never cross it to
    // connect the box interior to the rest of the maze at all.
    int homeCol = EYE_HOME_SPAWNS[homeIdx].col;
    int homeRow = EYE_HOME_SPAWNS[homeIdx].row;
    visited[homeRow][homeCol] = true;
    homeDir[homeIdx][homeRow][homeCol] = 0;  // already home, no step needed
    queueCol[qTail] = static_cast<int16_t>(homeCol);
    queueRow[qTail] = static_cast<int16_t>(homeRow);
    ++qTail;

    while (qHead < qTail) {
        int col = queueCol[qHead];
        int row = queueRow[qHead];
        ++qHead;
        for (const auto& off : kOffsets) {
            int ncol = col + off[0];
            int nrow = row + off[1];
            if (ncol < 0 || ncol >= kMazeCols || nrow < 0 || nrow >= kMazeRows) continue;
            if (visited[nrow][ncol]) continue;
            if (isWallTileForGhost(ncol, nrow, /*gatePassable=*/true)) continue;
            visited[nrow][ncol] = true;
            homeDir[homeIdx][nrow][ncol] = static_cast<uint8_t>(off[2]);
            queueCol[qTail] = static_cast<int16_t>(ncol);
            queueRow[qTail] = static_cast<int16_t>(nrow);
            ++qTail;
        }
    }
}

// Mirror of computeHomeDirField() — see that function and
// Ghost::exiting's comment. Targets GHOST_SPAWNS (row 8) instead of
// EYE_HOME_SPAWNS, so a revived ghost inside the box can find its way
// back out through the gate before resuming normal chase/frightened AI
// (which treats the gate as a wall, so it can't navigate this leg
// itself).
void PacManScene::computeExitDirField(int homeIdx) {
    constexpr int kDirUp = 1, kDirLeft = 2, kDirDown = 3, kDirRight = 4;
    constexpr int kOffsets[4][3] = {
        { 0, -1, kDirDown },
        { -1, 0, kDirRight },
        { 0, 1, kDirUp },
        { 1, 0, kDirLeft },
    };

    bool visited[kMazeRows][kMazeCols] = {};
    static int16_t queueCol[kMazeRows * kMazeCols];
    static int16_t queueRow[kMazeRows * kMazeCols];
    int qHead = 0, qTail = 0;

    int targetCol = GHOST_SPAWNS[homeIdx].col;
    int targetRow = GHOST_SPAWNS[homeIdx].row;
    visited[targetRow][targetCol] = true;
    exitDir[homeIdx][targetRow][targetCol] = 0;
    queueCol[qTail] = static_cast<int16_t>(targetCol);
    queueRow[qTail] = static_cast<int16_t>(targetRow);
    ++qTail;

    while (qHead < qTail) {
        int col = queueCol[qHead];
        int row = queueRow[qHead];
        ++qHead;
        for (const auto& off : kOffsets) {
            int ncol = col + off[0];
            int nrow = row + off[1];
            if (ncol < 0 || ncol >= kMazeCols || nrow < 0 || nrow >= kMazeRows) continue;
            if (visited[nrow][ncol]) continue;
            if (isWallTileForGhost(ncol, nrow, /*gatePassable=*/true)) continue;
            visited[nrow][ncol] = true;
            exitDir[homeIdx][nrow][ncol] = static_cast<uint8_t>(off[2]);
            queueCol[qTail] = static_cast<int16_t>(ncol);
            queueRow[qTail] = static_cast<int16_t>(nrow);
            ++qTail;
        }
    }
}

void PacManScene::computeGhostTarget(const Ghost& g, float& targetX, float& targetY) {
    float playerTileX = static_cast<float>(colToPx(static_cast<int>((playerX - PLAYFIELD_LEFT) / TILE_SIZE + 0.5f))) + TILE_SIZE / 2.0f;
    float playerTileY = static_cast<float>(rowToPy(static_cast<int>((playerY - PLAYFIELD_TOP) / TILE_SIZE + 0.5f))) + TILE_SIZE / 2.0f;

    int faceX = (dirX != 0 || dirY != 0) ? dirX : 1;
    int faceY = (dirX != 0 || dirY != 0) ? dirY : 0;

    switch (g.personality) {
        case GhostPersonality::Blinky: {
            targetX = playerTileX;
            targetY = playerTileY;
            break;
        }
        case GhostPersonality::Pinky: {
            targetX = playerTileX + faceX * 4 * TILE_SIZE;
            targetY = playerTileY + faceY * 4 * TILE_SIZE;
            if (faceY < 0) targetX -= 4 * TILE_SIZE;  // authentic "up" overflow bug
            break;
        }
        case GhostPersonality::Inky: {
            float aheadX = playerTileX + faceX * 2 * TILE_SIZE;
            float aheadY = playerTileY + faceY * 2 * TILE_SIZE;
            const Ghost* blinky = nullptr;
            for (int i = 0; i < kNumGhosts; ++i) {
                if (ghosts[i].personality == GhostPersonality::Blinky) { blinky = &ghosts[i]; break; }
            }
            float bx = blinky ? blinky->x + TILE_SIZE / 2.0f : aheadX;
            float by = blinky ? blinky->y + TILE_SIZE / 2.0f : aheadY;
            targetX = bx + 2.0f * (aheadX - bx);
            targetY = by + 2.0f * (aheadY - by);
            break;
        }
        case GhostPersonality::Clyde: {
            float dx = g.x - playerX;
            float dy = g.y - playerY;
            float distSq = dx * dx + dy * dy;
            constexpr float kRetreatDistSq = (8.0f * TILE_SIZE) * (8.0f * TILE_SIZE);
            if (distSq > kRetreatDistSq) {
                targetX = playerTileX;
                targetY = playerTileY;
            } else {
                targetX = static_cast<float>(colToPx(kClydeRetreatCol));
                targetY = static_cast<float>(rowToPy(kClydeRetreatRow));
            }
            break;
        }
    }
}

void PacManScene::updateGhost(Ghost& g, float dt) {
    // Parked in the ghost house (eyes arrived home): harmless and
    // un-targetable until the CURRENT power pellet's frightened window
    // ends — `frightenedUntilMs` is the same shared timestamp every
    // ghost got when the pellet was eaten, left untouched by eatAt()'s
    // collision handling, so this naturally stays in sync with however
    // long is actually left on the window rather than a fixed delay.
    if (g.parkedInHouse) {
        if (millis() >= g.frightenedUntilMs) {
            g.x = g.eyeHomeX;
            g.y = g.eyeHomeY;
            g.parkedInHouse = false;
            g.exiting = true;
            g.dirX = 0;
            g.dirY = 0;
            // Falls through to the `exiting` block below THIS SAME frame
            // — g.x/g.y are snapped back to the eyeHome tile (undoing the
            // bounce below) so the intersection-decision fires cleanly.
            // Can't just resume normal chase AI directly from here:
            // ordinary movement still treats the gate as a wall (see
            // isWallTileForGhost()), so a ghost starting fresh from inside
            // the now-sealed-again box would find zero legal moves.
        } else {
            // Real arcade has parked ghosts bob up and down in place
            // rather than sit frozen — a small vertical bounce, phase-
            // staggered per ghost (via its index) so all four don't move
            // in lockstep. Purely cosmetic: g.x stays pinned to
            // eyeHomeX, and the exact eyeHome position is restored above
            // the instant it's released.
            constexpr float kBounceAmplitude = 3.0f;
            constexpr float kBounceSpeedRadPerSec = 6.0f;
            int ghostIdx = static_cast<int>(&g - ghosts);
            float phase = static_cast<float>(ghostIdx) * 1.57f;
            g.x = g.eyeHomeX;
            g.y = g.eyeHomeY + sinf(static_cast<float>(millis()) * 0.001f * kBounceSpeedRadPerSec + phase) * kBounceAmplitude;
            return;
        }
    }

    if (g.frightened && millis() >= g.frightenedUntilMs) {
        g.frightened = false;
    }

    int col = static_cast<int>((g.x - PLAYFIELD_LEFT) / TILE_SIZE + 0.5f);
    int row = static_cast<int>((g.y - PLAYFIELD_TOP) / TILE_SIZE + 0.5f);

    // Revived ghost walking back out through the gate — mirrors the
    // `g.eaten` block below but targets homeX/Y via `exitDir` instead of
    // eyeHomeX/Y via `homeDir`. Once it reaches the original spawn tile,
    // resumes normal AI from there, same as a fresh game start.
    if (g.exiting) {
        int ghostIdx = static_cast<int>(&g - ghosts);
        bool arrived = (fabsf(g.x - g.homeX) < 1.5f && fabsf(g.y - g.homeY) < 1.5f);
        if (arrived) {
            g.x = g.homeX;
            g.y = g.homeY;
            g.exiting = false;
            // falls through to normal chase/frightened movement below.
        } else {
            float tileOriginX = static_cast<float>(colToPx(col));
            float tileOriginY = static_cast<float>(rowToPy(row));
            if (fabsf(g.x - tileOriginX) < 1.0f && fabsf(g.y - tileOriginY) < 1.0f) {
                g.x = tileOriginX;
                g.y = tileOriginY;
                uint8_t d = exitDir[ghostIdx][row][col];
                switch (d) {
                    case 1: g.dirX = 0; g.dirY = -1; break;
                    case 2: g.dirX = -1; g.dirY = 0; break;
                    case 3: g.dirX = 0; g.dirY = 1; break;
                    case 4: g.dirX = 1; g.dirY = 0; break;
                    default: g.dirX = 0; g.dirY = 0; break;
                }
            }
            g.x += g.dirX * GHOST_EYES_SPEED_CAP * dt;
            g.y += g.dirY * GHOST_EYES_SPEED_CAP * dt;
            return;
        }
    }

    // Real bug found live: the maze has genuinely disconnected pockets —
    // the side rooms flanking the ghost house (rows 10-12/16-18 at the
    // far left/right columns, confirmed with a standalone BFS-reachability
    // script run against this exact maze data) that even tunnel
    // wraparound can't reach from any ghost's home tile, since wrapping
    // only connects a row's own left/right edges to each other, not to a
    // different row. Ordinary chase/flee movement uses this SAME
    // passable-tile graph, so a fleeing ghost can absolutely wander into
    // one of these pockets during normal play — and if it's eaten while
    // there, `homeDir` is 0 (no path exists) and it would otherwise sit
    // frozen. Rather than a maze-topology fix (this may well match the
    // real arcade's own layout) or a long timeout before recovering
    // (reported live as visibly "stuck"), detect "no path from here"
    // immediately via `homeDir` and snap home the instant it's known,
    // not after waiting to see if pathing resolves on its own.
    if (g.eaten) {
        int ghostIdx = static_cast<int>(&g - ghosts);
        bool alreadyHome = (fabsf(g.x - g.eyeHomeX) < 1.5f && fabsf(g.y - g.eyeHomeY) < 1.5f);
        bool stuck = !alreadyHome && homeDir[ghostIdx][row][col] == 0;
        g.eatenTimer += dt;
        constexpr float kMaxEatenTravelS = 8.0f;  // backstop for any other unforeseen case
        if (stuck || g.eatenTimer >= kMaxEatenTravelS) {
            g.x = g.eyeHomeX;
            g.y = g.eyeHomeY;
            g.eaten = false;
            g.parkedInHouse = true;
            g.dirX = 0;
            g.dirY = 0;
            g.eatenTimer = 0.0f;
            return;
        }
    }

    float tileOriginX = static_cast<float>(colToPx(col));
    float tileOriginY = static_cast<float>(rowToPy(row));
    bool atGridX = fabsf(g.x - tileOriginX) < 1.0f;
    bool atGridY = fabsf(g.y - tileOriginY) < 1.0f;

    // Ghosts only make a new decision at a true intersection (aligned on
    // BOTH axes, i.e. tile centers) — unlike the player, who can turn as
    // soon as the perpendicular axis lines up, since the player's next
    // direction is externally supplied (input) rather than computed by
    // scoring all 4 options every frame.
    if (atGridX && atGridY) {
        g.x = tileOriginX;
        g.y = tileOriginY;

        if (g.eaten) {
            // Eyes follow the precomputed shortest-path direction field
            // straight home (see computeHomeDirField()) rather than the
            // greedy nearest-tile-to-target heuristic every other ghost
            // state uses — a FIXED target (unlike a moving chase/flee
            // target) can trap that heuristic circling a maze pocket
            // forever, confirmed live before this fix.
            int ghostIdx = static_cast<int>(&g - ghosts);
            uint8_t d = homeDir[ghostIdx][row][col];
            switch (d) {
                case 1: g.dirX = 0; g.dirY = -1; break;  // up
                case 2: g.dirX = -1; g.dirY = 0; break;  // left
                case 3: g.dirX = 0; g.dirY = 1; break;   // down
                case 4: g.dirX = 1; g.dirY = 0; break;   // right
                default: g.dirX = 0; g.dirY = 0; break;  // already home / unreachable
            }
        } else {
            float targetX, targetY;
            if (g.frightened) {
                // Frightened ghosts flee rather than target the player —
                // approximated here as "maximize distance from the
                // player" (the real arcade uses a pseudo-random
                // direction at each intersection instead; this reads
                // similarly — panicked, not suicidal — without needing a
                // separate RNG-driven path).
                targetX = g.x + (g.x - playerX);
                targetY = g.y + (g.y - playerY);
            } else {
                computeGhostTarget(g, targetX, targetY);
            }

            constexpr int kCandidates[4][2] = { {0, -1}, {-1, 0}, {0, 1}, {1, 0} };  // up, left, down, right
            int bestDX = 0, bestDY = 0;
            float bestDist = -1.0f;
            bool found = false;
            for (const auto& cand : kCandidates) {
                int cdx = cand[0], cdy = cand[1];
                // No reversal, unless it's the only way out of a dead
                // end (checked as a fallback below) — matches the
                // classic rule that ghosts never turn back on themselves
                // except when forced or when a power pellet is eaten
                // (handled separately in eatAt()).
                if ((g.dirX != 0 || g.dirY != 0) && cdx == -g.dirX && cdy == -g.dirY) continue;
                if (isWallTileForGhost(col + cdx, row + cdy)) continue;
                float nx = static_cast<float>(colToPx(col + cdx)) + TILE_SIZE / 2.0f;
                float ny = static_cast<float>(rowToPy(row + cdy)) + TILE_SIZE / 2.0f;
                float d = (nx - targetX) * (nx - targetX) + (ny - targetY) * (ny - targetY);
                if (!found || d < bestDist) {
                    bestDist = d;
                    bestDX = cdx;
                    bestDY = cdy;
                    found = true;
                }
            }
            if (!found) {
                // Dead end: the only legal move is to reverse.
                bestDX = -g.dirX;
                bestDY = -g.dirY;
            }
            g.dirX = bestDX;
            g.dirY = bestDY;
        }
    }

    float speed = g.eaten ? GHOST_EYES_SPEED_CAP : (g.frightened ? currentGhostFrightSpeed : currentGhostSpeed);
    g.x += g.dirX * speed * dt;
    g.y += g.dirY * speed * dt;

    float mazeLeftPx = static_cast<float>(colToPx(0));
    float mazeRightPx = static_cast<float>(colToPx(kMazeCols - 1));
    if (g.x < mazeLeftPx - TILE_SIZE) {
        g.x = mazeRightPx;
    } else if (g.x > mazeRightPx + TILE_SIZE) {
        g.x = mazeLeftPx;
    }

    if (g.eaten) {
        float hdx = g.x - g.eyeHomeX;
        float hdy = g.y - g.eyeHomeY;
        if (fabsf(hdx) < 1.5f && fabsf(hdy) < 1.5f) {
            g.x = g.eyeHomeX;
            g.y = g.eyeHomeY;
            g.eaten = false;
            g.parkedInHouse = true;
            g.dirX = 0;
            g.dirY = 0;
            g.eatenTimer = 0.0f;
        }
    }

    constexpr unsigned long kFrameTimeMs = 150;
    g.animAccumulatorMs += static_cast<unsigned long>(dt * 1000.0f);
    while (g.animAccumulatorMs >= kFrameTimeMs) {
        g.animAccumulatorMs -= kFrameTimeMs;
        g.animFrame = g.animFrame ? 0 : 1;
    }
}

void PacManScene::checkGhostCollisions() {
    if (millis() < invulnerableUntilMs) return;
    // Simple AABB touch check, same "wider hurtbox than the movement
    // grid" spirit as Bubble Bobble's hurtbox pattern, sized around the
    // 8px logical tiles both actors move on.
    constexpr float kTouchDist = TILE_SIZE * 0.9f;
    for (int i = 0; i < kNumGhosts; ++i) {
        Ghost& g = ghosts[i];
        // Eyes-returning-home and parked-in-house ghosts are harmless —
        // matches the real arcade (you can walk straight through them).
        if (g.eaten || g.parkedInHouse) continue;
        float dx = (g.x + TILE_SIZE / 2.0f) - (playerX + TILE_SIZE / 2.0f);
        float dy = (g.y + TILE_SIZE / 2.0f) - (playerY + TILE_SIZE / 2.0f);
        if (fabsf(dx) > kTouchDist || fabsf(dy) > kTouchDist) continue;

        if (g.frightened) {
            // Real arcade behavior: eaten ghosts don't just vanish and
            // respawn — their eyes travel back to the ghost house at
            // high speed (see updateGhost()'s `eaten` branch), then sit
            // there harmlessly until the CURRENT power pellet's
            // frightened window ends, only then re-emerging to resume
            // the chase. Previously this just teleported the ghost home
            // and cleared `frightened` immediately, letting an eaten
            // ghost threaten the player again right away — which made
            // the whole power-pellet window feel much shorter/weaker
            // than it should (reported live after comparing to the
            // original).
            score += 200;
            g.frightened = false;
            g.eaten = true;
            g.eatenTimer = 0.0f;
            sfx.play(pacman_pr32_audio::kGhostEatenSfx, pacman_pr32_audio::kGhostEatenSfxLen);
        } else {
            --lives;
            sfx.play(pacman_pr32_audio::kDeathSfx, pacman_pr32_audio::kDeathSfxLen);
            if (lives <= 0) {
                gameOver = true;
            } else {
                resetPlayerAndGhostPositions();
            }
            return;  // positions just got reset (or game over) -- stop checking this frame
        }
    }
}

void PacManScene::update(unsigned long deltaTime) {
    constexpr unsigned long kMaxFrameMs = 50;
    if (deltaTime > kMaxFrameMs) deltaTime = kMaxFrameMs;

    // Real arcade attract screen (ghost roster reveal + point legend +
    // eat-a-ghost-train demo) plays once at scene entry, BEFORE the
    // existing intro-jingle-freeze/gameplay sequence — see
    // drawTitleScreen()'s comment for the full replicated layout.
    // A skips ahead early, same "don't force sitting through it
    // twice" convention as every other scene's skippable intro/banner.
    if (titlePhase == 0) {
        titleTimer += static_cast<float>(deltaTime) * 0.001f;
        constexpr float kTitleDurationS = 6.2f;
        if (pendingInput.aEdge || titleTimer >= kTitleDurationS) {
            titlePhase = 1;
            introActive = true;
            sfx.play(pacman_pr32_audio::kIntroJingle, pacman_pr32_audio::kIntroJingleLen);
        }
        Scene::update(deltaTime);
        return;
    }

    // Same "hold past pause -> return to menu" gesture as
    // BubbleBobbleScene — see that class for the full rationale.
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

    if (introActive) {
        sfx.update();
        if (!sfx.isPlaying()) introActive = false;
        Scene::update(deltaTime);
        return;
    }

    // A restarts a finished game (game-over only — clearing all
    // pellets now advances to the next level instead of ending the game,
    // see advanceLevel()) — otherwise the only way out was the
    // long-hold-to-menu gesture, with no way to just play again without
    // a full menu round-trip.
    if (gameOver && pendingInput.aEdge) {
        init();
        Scene::update(deltaTime);
        return;
    }

    // Direction input: thumbstick primarily (matches an arcade joystick),
    // dominant axis wins so a slightly-diagonal real stick reading still
    // produces a clean 4-way direction rather than fighting both axes at
    // once. encDelta (the encoder, and main.cpp's serial debug-injection
    // 'a'/'d') doubles as a secondary left/right nudge, same convention
    // Bubble Bobble's walk control already established.
    float tx = pendingInput.thumbX;
    float ty = pendingInput.thumbY;
    if (fabsf(tx) > 0.3f || fabsf(ty) > 0.3f) {
        if (fabsf(tx) > fabsf(ty)) {
            wantDirX = (tx > 0) ? 1 : -1;
            wantDirY = 0;
        } else {
            wantDirX = 0;
            // Raw thumbY reads positive when the stick is pushed UP (same
            // hardware convention documented in menu.cpp/rtype.cpp), but
            // row/pixel Y increases DOWNWARD on screen — so "up" must map
            // to dirY=-1, not +1. Reported live as "up/down inverted,
            // Pac-Man moves opposite the way he's facing" (the sprite
            // direction always matches dirY by construction, so that was
            // just this same inversion, not a second bug).
            wantDirY = (ty > 0) ? -1 : 1;
        }
    } else if (pendingInput.encDelta > 0) {
        wantDirX = 1;
        wantDirY = 0;
    } else if (pendingInput.encDelta < 0) {
        wantDirX = -1;
        wantDirY = 0;
    }

    if (!gameOver) {
        float dtSeconds = static_cast<float>(deltaTime) * 0.001f;
        updatePlayer(dtSeconds);
        // Blinky updates first — Inky's targeting needs Blinky's CURRENT
        // position (see computeGhostTarget()'s comment), so it must not
        // read a stale pre-move value from earlier this frame.
        for (int i = 0; i < kNumGhosts; ++i) {
            if (ghosts[i].personality == GhostPersonality::Blinky) updateGhost(ghosts[i], dtSeconds);
        }
        for (int i = 0; i < kNumGhosts; ++i) {
            if (ghosts[i].personality != GhostPersonality::Blinky) updateGhost(ghosts[i], dtSeconds);
        }
        checkGhostCollisions();
#if PACMAN_DEBUG_TRACE
        {
            static unsigned long lastGTrace = 0;
            if (millis() - lastGTrace > 300) {
                lastGTrace = millis();
                Ghost& g0 = ghosts[0];
                Serial.printf("GTRACE t=%lu g0.x=%.1f g0.y=%.1f eaten=%d parked=%d frightUntil=%lu now=%lu\n",
                              millis(), g0.x, g0.y, g0.eaten, g0.parkedInHouse, g0.frightenedUntilMs, millis());
            }
        }
#endif
    }

    if (fruitActive && millis() >= fruitUntilMs) {
        fruitActive = false;
    }

    if (dirX != 0 || dirY != 0) {
        constexpr unsigned long kFrameTimeMs = 120;
        animAccumulatorMs += deltaTime;
        while (animAccumulatorMs >= kFrameTimeMs) {
            animAccumulatorMs -= kFrameTimeMs;
            animFrame = animFrame ? 0 : 1;
            // "Waka" chomp, synced to the mouth-animation toggle rather
            // than a separate timer — one tone per open/close cycle,
            // alternating pitch. Skipped whenever a one-shot cue (power
            // pellet, ghost eaten, fruit, death, level-clear) is already
            // using the single buzzer channel, so it can't stomp on
            // those; resumes on its own next toggle once free.
            // tone(pin, freq) — the 2-argument form, NEVER with a
            // duration (see pacman_pr32_audio.h's top comment for the
            // real bug that caused) — doesn't auto-stop, so this plays
            // as one continuous alternating warble while moving rather
            // than distinct blips — reads as MORE authentic "waka
            // waka," not less; explicitly silenced below the instant
            // movement stops.
            if (!sfx.isPlaying() && !gameOver) {
                using namespace pacman_pr32_audio;
                buzzer_audio::buzzerTone(animFrame ? kChompToneA : kChompToneB);
            }
        }
    } else if (!sfx.isPlaying()) {
        buzzer_audio::buzzerNoTone();
    }

    sfx.update();
    Scene::update(deltaTime);
}

void PacManScene::drawMaze(Renderer& renderer) {
    // Pre-rendered bitmap (rounded corners, two-tone blue "tube" look,
    // gate baked in) — replaces the old per-tile flat-rectangle draw
    // after the user supplied a reference screenshot of the real arcade
    // maze and asked for an authentic look. See
    // tools/make_pacman_maze_bitmap.py for how it's generated; MAZE[]
    // above is UNCHANGED (still the actual collision/gameplay data —
    // only the art is different now).
    //
    // This bitmap has its own 16-color palette, separate from
    // pacman_pr32_sprites.h's (that budget was already tight at 9/16 —
    // simplest to keep the two independent and just swap the active
    // custom sprite palette around this one draw call, rather than
    // merging two generator scripts' color allocations).
    pr32::graphics::setSpriteCustomPalette(pacman_pr32_maze::PALETTE_RGB565);
    renderer.drawSprite(pacman_pr32_maze::MAZE_BG, PLAYFIELD_LEFT, PLAYFIELD_TOP, false);
    pr32::graphics::setSpriteCustomPalette(pacman_pr32_sprites::PALETTE_RGB565);
}

void PacManScene::drawPellets(Renderer& renderer) {
    for (int r = 0; r < kMazeRows; ++r) {
        for (int c = 0; c < kMazeCols; ++c) {
            if (pelletEaten[r][c]) continue;
            char t = MAZE[r][c];
            int cx = colToPx(c) + TILE_SIZE / 2;
            int cy = rowToPy(r) + TILE_SIZE / 2;
            if (t == '.') {
                renderer.drawFilledRectangle(cx - 1, cy - 1, 2, 2, pacman_pr32_sprites::PELLET_YELLOW);
            } else if (t == '*') {
                renderer.drawFilledCircle(cx, cy, 3, pacman_pr32_sprites::PELLET_YELLOW);
            }
        }
    }
}

void PacManScene::drawFruit(Renderer& renderer) {
    if (!fruitActive) return;
    namespace spr = pacman_pr32_sprites;
    int cx = colToPx(kFruitCol) + TILE_SIZE / 2;
    int cy = rowToPy(kFruitRow) + TILE_SIZE / 2;
    renderer.drawSprite(spr::PM_FRUIT_CHERRY, cx - spr::PM_FRUIT_CHERRY.width / 2, cy - spr::PM_FRUIT_CHERRY.height / 2, false);
}

void PacManScene::drawPlayer(Renderer& renderer) {
    namespace spr = pacman_pr32_sprites;
    const pr32::graphics::Sprite4bpp* sprite;
    // Facing defaults to "right" (dirX==0 && dirY==0, e.g. before the
    // player has moved at all) — matches the arcade's own spawn pose.
    // LEFT/RIGHT are swapped here relative to dirX (dirX<0 -> the RIGHT-
    // facing sprite, dirX>0 -> LEFT) — confirmed via our own screenshot
    // tool that the un-swapped mapping is internally correct (a
    // left-moving player renders the mouth-open-left sprite in the
    // software framebuffer), but the user reported the real physical
    // panel shows Pac-Man facing the opposite way he's actually moving.
    // Screenshots read the same internal buffer either way and can't see
    // this discrepancy — only the real hardware shows it, the same
    // "real panel differs from the software buffer" class of issue as
    // the earlier menu color-swap bug, just spatial instead of
    // chromatic this time. Compensated here (Pac-Man's own sprite
    // selection only) rather than touching the shared renderer/driver.
    if (dirY < 0) sprite = animFrame ? &spr::PM_PM_UP1 : &spr::PM_PM_UP0;
    else if (dirY > 0) sprite = animFrame ? &spr::PM_PM_DOWN1 : &spr::PM_PM_DOWN0;
    else if (dirX < 0) sprite = animFrame ? &spr::PM_PM_RIGHT1 : &spr::PM_PM_RIGHT0;
    else sprite = animFrame ? &spr::PM_PM_LEFT1 : &spr::PM_PM_LEFT0;

    // Visual sprite is slightly larger than the logical 8x8 collision
    // tile — centered on it, same "bigger-than-grid" idiom Bubble Bobble
    // uses for Bub relative to its own hitbox.
    int drawX = static_cast<int>(playerX) + TILE_SIZE / 2 - sprite->width / 2;
    int drawY = static_cast<int>(playerY) + TILE_SIZE / 2 - sprite->height / 2;
    renderer.drawSprite(*sprite, drawX, drawY, false);
}

void PacManScene::drawGhosts(Renderer& renderer) {
    namespace spr = pacman_pr32_sprites;
#if PACMAN_DEBUG_TRACE
    static unsigned long lastGDump = 0;
    if (millis() - lastGDump > 500) {
        lastGDump = millis();
        Serial.printf("GHOSTS t=%lu: f0=%d f1=%d f2=%d f3=%d\n", millis(),
                      ghosts[0].frightened, ghosts[1].frightened, ghosts[2].frightened, ghosts[3].frightened);
    }
#endif
    for (int i = 0; i < kNumGhosts; ++i) {
        const Ghost& g = ghosts[i];
        if (g.eaten || g.parkedInHouse) {
            // No dedicated "eyes only" sprite in this project's ghost
            // sheet (real arcade has one) — approximated as two small
            // white eyes with a pupil nudged toward the current facing
            // direction, which reads clearly as "just the eyes" at this
            // sprite size without needing new sprite art.
            //
            // Real bug caught live: drawFilledCircle() with no explicit
            // render context defaults to the SPRITE palette context, not
            // Background — and this scene's custom sprite palette (built
            // from actual sprite pixel colors, not the standard 16-name
            // palette) remaps Color::White to a cyan-ish tone and
            // Color::Blue to pink. The eyes were rendering, just in the
            // wrong colors and easy to miss against the maze's own blue/
            // cyan walls. Explicitly switching to the Background context
            // here (same as drawHUD()) makes White/Blue mean what they
            // actually say.
            pr32::graphics::PaletteContext bgCtx = pr32::graphics::PaletteContext::Background;
            pr32::graphics::PaletteContext* savedCtx = renderer.getRenderContext();
            renderer.setRenderContext(&bgCtx);

            int cx = static_cast<int>(g.x) + TILE_SIZE / 2;
            int cy = static_cast<int>(g.y) + TILE_SIZE / 2;
            // Blocky cross-shaped white eye (two overlapping rectangles)
            // with a solid square pupil offset toward the lower-right —
            // matches the classic ghost-eyes look the user pointed to,
            // not a small circle (which just rasterized as a barely-
            // visible "+" at this size).
            auto drawEye = [&](int ex, int ey) {
                renderer.drawFilledRectangle(ex - 1, ey - 2, 3, 5, Color::White);
                renderer.drawFilledRectangle(ex - 2, ey - 1, 5, 3, Color::White);
                renderer.drawFilledRectangle(ex, ey, 2, 2, Color::Blue);
            };
            drawEye(cx - 3, cy - 1);
            drawEye(cx + 3, cy - 1);

            renderer.setRenderContext(savedCtx);
            continue;
        }
        const pr32::graphics::Sprite4bpp* sprite;
        if (g.frightened) {
            // Flash blue/pink faster as the frightened window runs out —
            // matches the real arcade's own "warning" flicker.
            unsigned long remaining = (g.frightenedUntilMs > millis()) ? (g.frightenedUntilMs - millis()) : 0;
            bool flashFast = remaining < 2000;
            unsigned long period = flashFast ? 150 : 400;
            bool blue = ((millis() / period) % 2) == 0;
            sprite = blue ? &spr::PM_GHOST_SCARED_BLUE : &spr::PM_GHOST_SCARED_PINK;
        } else {
            switch (g.personality) {
                case GhostPersonality::Blinky: sprite = g.animFrame ? &spr::PM_GHOST_BLINKY1 : &spr::PM_GHOST_BLINKY0; break;
                case GhostPersonality::Pinky: sprite = g.animFrame ? &spr::PM_GHOST_PINKY1 : &spr::PM_GHOST_PINKY0; break;
                case GhostPersonality::Inky: sprite = g.animFrame ? &spr::PM_GHOST_INKY1 : &spr::PM_GHOST_INKY0; break;
                default: sprite = g.animFrame ? &spr::PM_GHOST_CLYDE1 : &spr::PM_GHOST_CLYDE0; break;
            }
        }
        int drawX = static_cast<int>(g.x) + TILE_SIZE / 2 - sprite->width / 2;
        int drawY = static_cast<int>(g.y) + TILE_SIZE / 2 - sprite->height / 2;
        renderer.drawSprite(*sprite, drawX, drawY, false);
    }
}

void PacManScene::drawHUD(Renderer& renderer) {
    pr32::graphics::PaletteContext bgContext = pr32::graphics::PaletteContext::Background;
    pr32::graphics::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);

    namespace fm = pr32::graphics;
    auto centerX = [&](const char* text) {
        return SCREEN_W / 2 - fm::FontManager::textWidth(kHudFont, text, 1) / 2;
    };
    auto rightX = [&](const char* text, int rightEdge) {
        return rightEdge - fm::FontManager::textWidth(kHudFont, text, 1);
    };

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", score);
    renderer.drawText(buf, 4, 4, Color::White, 1, kHudFont);

    snprintf(buf, sizeof(buf), "L%d", level);
    renderer.drawText(buf, 4, 14, Color::Gray, 1, kHudFont);

    snprintf(buf, sizeof(buf), "LIVES %d", lives);
    renderer.drawText(buf, rightX(buf, SCREEN_W - 4), 4, Color::Gray, 1, kHudFont);

    snprintf(buf, sizeof(buf), "%d LEFT", pelletsRemaining);
    renderer.drawText(buf, centerX(buf), 4, Color::Gray, 1, kHudFont);

    if (gameOver) {
        const char* msg = "GAME OVER";
        renderer.drawText(msg, centerX(msg), SCREEN_H - 32, Color::Red, 1, kHudFont);
        const char* hint = "A: RESTART";
        renderer.drawText(hint, centerX(hint), SCREEN_H - 20, Color::Gray, 1, kHudFont);
    } else if (paused) {
        const char* msg = "PAUSED";
        renderer.drawText(msg, centerX(msg), SCREEN_H - 24, Color::White, 1, kHudFont);
    }

    renderer.setRenderContext(saved);
}

// Replicates the real arcade's "CHARACTER / NICKNAME" attract screen
// (ghost roster reveal, point-value legend, Pac-Man-eats-a-ghost-train
// demo) from a real cabinet recording the user supplied — deliberately
// leaves out the copyright-warning box/line per their explicit request.
// Shown once at scene entry (see `titlePhase` in update()), replacing
// the normal maze/HUD draw entirely while active.
void PacManScene::drawTitleScreen(Renderer& renderer) {
    namespace spr = pacman_pr32_sprites;
    namespace fm = pr32::graphics;
    pr32::graphics::PaletteContext bgContext = pr32::graphics::PaletteContext::Background;
    pr32::graphics::PaletteContext* saved = renderer.getRenderContext();

    auto centerXbg = [&](const char* text) {
        return SCREEN_W / 2 - fm::FontManager::textWidth(kHudFont, text, 1) / 2;
    };

    struct RosterRow { GhostPersonality who; const char* dash; const char* nick; };
    static const RosterRow kRoster[4] = {
        { GhostPersonality::Blinky, "-SHADOW",  "\"BLINKY\"" },
        { GhostPersonality::Pinky,  "-SPEEDY",  "\"PINKY\"" },
        { GhostPersonality::Inky,   "-BASHFUL", "\"INKY\"" },
        { GhostPersonality::Clyde,  "-POKEY",   "\"CLYDE\"" },
    };
    constexpr float kRosterStart = 0.3f;
    constexpr float kRosterStep = 0.6f;
    constexpr float kLegendAt = kRosterStart + 4 * kRosterStep + 0.2f;  // 2.9s
    constexpr float kDemoStart = 3.2f;
    constexpr float kDemoStartX = 100.0f;
    constexpr float kDemoSpeed = 40.0f;  // px/s
    constexpr int kDemoY = 190;
    static const int kGhostSlotX[4] = { 130, 155, 180, 205 };
    static const int kGhostScores[4] = { 200, 400, 800, 1600 };

    float demoT = titleTimer - kDemoStart;
    float pacX = kDemoStartX + (demoT > 0.0f ? demoT * kDemoSpeed : 0.0f);

    // --- Sprites first (default Sprite palette context, same as every
    // other scene's draw()) ---
    for (int i = 0; i < 4; ++i) {
        if (titleTimer < kRosterStart + i * kRosterStep) continue;
        int y = 52 + i * 16;
        const pr32::graphics::Sprite4bpp* icon;
        switch (kRoster[i].who) {
            case GhostPersonality::Blinky: icon = &spr::PM_GHOST_BLINKY0; break;
            case GhostPersonality::Pinky:  icon = &spr::PM_GHOST_PINKY0; break;
            case GhostPersonality::Inky:   icon = &spr::PM_GHOST_INKY0; break;
            default:                       icon = &spr::PM_GHOST_CLYDE0; break;
        }
        renderer.drawSprite(*icon, 18, y - 3, false);
    }
    if (demoT >= 0.0f) {
        for (int i = 0; i < 4; ++i) {
            if (pacX < kGhostSlotX[i] - 4) {
                renderer.drawSprite(spr::PM_GHOST_SCARED_BLUE, kGhostSlotX[i] - 7, kDemoY - 7, false);
            }
        }
        int pf = static_cast<int>(demoT * 8.0f) % 2;
        // Demo Pac-Man moves RIGHT — real-panel mirroring means the
        // "LEFT"-content sprite is what actually reads as facing/moving
        // right once displayed (same convention drawPlayer() already
        // uses for dirX>=0, see that function's own comment).
        const auto* pacSprite = pf ? &spr::PM_PM_LEFT1 : &spr::PM_PM_LEFT0;
        renderer.drawSprite(*pacSprite, static_cast<int>(pacX) - 6, kDemoY - 6, false);
    }

    // --- Everything else (text, point-legend dots) in the Background
    // context, same as drawHUD() — see feedback_remote_hw_debugging.md's
    // note on why primitives/text need this explicit switch under a
    // custom sprite palette. ---
    renderer.setRenderContext(&bgContext);

    renderer.drawText("1UP", 28, 6, Color::White, 1, kHudFont);
    renderer.drawText("00", 30, 16, Color::White, 1, kHudFont);
    renderer.drawText("HIGH SCORE", centerXbg("HIGH SCORE"), 6, Color::White, 1, kHudFont);
    char hbuf[12];
    snprintf(hbuf, sizeof(hbuf), "%d", hiscore);
    renderer.drawText(hbuf, centerXbg(hbuf), 16, Color::White, 1, kHudFont);
    const char* twoUp = "2UP";
    renderer.drawText(twoUp, SCREEN_W - 28 - fm::FontManager::textWidth(kHudFont, twoUp, 1), 6, Color::White, 1, kHudFont);

    const char* header = "CHARACTER / NICKNAME";
    renderer.drawText(header, centerXbg(header), 38, Color::White, 1, kHudFont);

    for (int i = 0; i < 4; ++i) {
        if (titleTimer < kRosterStart + i * kRosterStep) continue;
        int y = 52 + i * 16;
        Color c = ghostColor(kRoster[i].who);
        renderer.drawText(kRoster[i].dash, 38, y, c, 1, kHudFont);
        renderer.drawText(kRoster[i].nick, 118, y, c, 1, kHudFont);
    }

    if (titleTimer >= kLegendAt) {
        // Real arcade's point-legend dots are pink/salmon, not yellow —
        // confirmed against a clean YouTube reference the user linked
        // (much clearer than the original phone recording).
        renderer.drawFilledCircle(24, 133, 1, Color::LightRed);
        renderer.drawText("10 PTS", 40, 129, Color::White, 1, kHudFont);
        renderer.drawFilledCircle(24, 149, 3, Color::LightRed);
        renderer.drawText("50 PTS", 40, 145, Color::White, 1, kHudFont);
    }

    if (demoT >= 0.0f) {
        for (int i = 0; i < 4; ++i) {
            float eatenAt = (kGhostSlotX[i] - kDemoStartX) / kDemoSpeed;
            if (demoT >= eatenAt && demoT < eatenAt + 1.0f) {
                char sbuf[8];
                snprintf(sbuf, sizeof(sbuf), "%d", kGhostScores[i]);
                int w = fm::FontManager::textWidth(kHudFont, sbuf, 1);
                renderer.drawText(sbuf, kGhostSlotX[i] - w / 2, kDemoY - 4, Color::Cyan, 1, kHudFont);
            }
        }
    }

    renderer.drawText("CREDIT  0", 28, SCREEN_H - 24, Color::White, 1, kHudFont);

    renderer.setRenderContext(saved);
}

void PacManScene::draw(Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
    if (titlePhase == 0) {
        drawTitleScreen(renderer);
        return;
    }
    drawMaze(renderer);
    drawPellets(renderer);
    drawFruit(renderer);
    drawGhosts(renderer);
    drawPlayer(renderer);
    drawHUD(renderer);
}

} // namespace pacman_pr32
