// Pac-Man clone on PixelRoot32 (see ~/.claude/plans/goofy-dancing-locket.md,
// Phase B). Maze decoded directly from the sgothel/pacman reference
// project's real level data (media/playfield_pacman.txt, MIT-licensed) —
// see pacman_pr32.cpp for the exact provenance and tile-character legend.
//
// Movement deliberately stays OFF PixelRoot32's physics/collision system
// entirely (same decision Bubble Bobble settled on for its own tile-based
// logic) — Pac-Man and (later) the ghosts are hand-managed grid actors,
// checked directly against the maze array, not KinematicActors. Pellets
// are plain per-tile boolean state, drawn as primitives, not sprites or
// Actors — same "manual data, not an engine entity" pattern Bubble
// Bobble's CaptureBubble/FoodItem already established.
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "pacman_pr32_audio.h"

namespace pacman_pr32 {

constexpr int kMazeCols = 28;
constexpr int kMazeRows = 31;

// Blinky/Pinky/Inky/Clyde each get real, distinct CHASE targeting logic
// (see updateGhost()'s comment) — ported in spirit from sgothel/pacman's
// ghost.cpp (this session's research, quoted in the plan file), not
// copied verbatim (different engine/units). Deliberately simplified from
// the reference for this first pass: no scatter-mode wave timers (ghosts
// are always in chase/frightened mode, never retreat to a corner on a
// timer) and no ghost-house release counters (all 4 just start outside
// the house already moving) — see the plan file for why these were
// judged not worth porting yet.
enum class GhostPersonality { Blinky, Pinky, Inky, Clyde };

struct Ghost {
    GhostPersonality personality;
    float x = 0.0f;
    float y = 0.0f;
    int dirX = 0;
    int dirY = 0;
    bool frightened = false;
    unsigned long frightenedUntilMs = 0;
    int animFrame = 0;
    unsigned long animAccumulatorMs = 0;

    // Real arcade behavior: eating a frightened ghost doesn't just erase
    // it — its eyes race back to the ghost house at high speed, then it
    // sits there (harmless, un-targetable) until the CURRENT power
    // pellet's frightened window ends, only then re-emerging to resume
    // the chase. `eaten` = eyes still traveling home; `parkedInHouse` =
    // arrived and waiting. `homeX/homeY` cache this ghost's own spawn
    // tile (set once in init()/resetPlayerAndGhostPositions()) as the
    // eyes' travel target, since Ghost itself doesn't otherwise know its
    // own spawn point after the fact.
    bool eaten = false;
    bool parkedInHouse = false;
    float homeX = 0.0f;
    float homeY = 0.0f;
    // The visual ghost-house BOX interior tile (distinct from `homeX/Y`,
    // which is actually GHOST_SPAWNS' row-8 corridor position — see
    // pacman_pr32.cpp's history on why ghosts spawn outside the gated
    // house entirely). Eaten eyes should travel to a spot INSIDE the box
    // the player can see, not back to that outside corridor — using
    // homeX/Y for eyes' travel target was the real bug behind "eyes
    // aren't making it to the spawn box, getting stuck right before it":
    // they were correctly arriving at their real target, which just
    // wasn't inside the box at all.
    float eyeHomeX = 0.0f;
    float eyeHomeY = 0.0f;
    // True while a revived ghost is walking back OUT through the gate
    // (from eyeHomeX/Y to homeX/Y) before resuming normal AI — mirrors
    // the eaten/eyeHome trip via `exitDir`, needed because ordinary
    // chase-target movement still treats the gate as a wall (see
    // isWallTileForGhost()) to avoid ghosts erroneously ducking back into
    // the house mid-chase; a ghost starting a fresh chase from INSIDE the
    // now-sealed-again box would otherwise find zero legal moves.
    bool exiting = false;
    // Safety net: the eyes' route home reuses the same greedy "pick the
    // neighbor tile closest to the target" logic as normal chase/flee
    // (no real pathfinding) — confirmed live that this can get trapped
    // circling a small maze loop forever for certain starting positions,
    // since a fixed target (unlike a moving player) never breaks the
    // cycle by itself. This counts up while eaten; if it's taking
    // unreasonably long, updateGhost() just snaps the eyes home directly
    // rather than risk them being stuck mid-maze forever.
    float eatenTimer = 0.0f;
};

class PacManScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    // Same long-hold-R3-past-pause convention as
    // BubbleBobbleScene::wantsReturnToMenu() — see that class for the
    // full rationale.
    bool wantsReturnToMenu() const { return returnToMenuRequested; }
    void clearReturnToMenuRequest() { returnToMenuRequested = false; }

    // Debug-only: warps the player directly onto the tunnel row heading
    // left, so the wraparound mechanic can be verified without first
    // navigating the full maze from spawn. Remove once confirmed working
    // (this one's purely a same-session verification aid, unlike Bubble
    // Bobble's level-select which the user wanted kept around longer).
    void debugWarpToTunnel();
    // Debug-only: warps the player directly next to a power pellet, for
    // deterministically testing the frightened-ghost mechanic.
    void debugWarpToPowerPellet();
    // Debug-only: force-spawns the bonus fruit next to the player
    // immediately, bypassing the real 70/170-pellets-eaten threshold, for
    // testing the fruit render/eat/score/expire logic without a long
    // scripted pellet-eating run first.
    void debugSpawnFruit();
    // Debug-only: jumps straight to the next level (same effect as
    // clearing every pellet), for testing level-progression speed ramps
    // without a full 244-pellet playthrough per level.
    void debugAdvanceLevel();
    // Debug-only: instantly puts ghost 0 into the eaten (eyes-returning)
    // state regardless of proximity/frightened state, for deterministic
    // same-session testing of the eyes-travel-home/park/release cycle
    // without needing to actually corner a fleeing frightened ghost.
    void debugForceEatGhost();

private:
    Pr32Input pendingInput;

    void updatePlayer(float dt);
    void eatAt(int col, int row);
    void advanceLevel();
    void applyLevelSpec();
    void updateGhost(Ghost& g, float dt);
    void computeGhostTarget(const Ghost& g, float& targetX, float& targetY);
    // Precomputes, for ghost `homeIdx`, the shortest-path "which way to
    // step from here" direction for every reachable maze tile toward
    // that ghost's own home/spawn tile (a BFS flood from home outward,
    // direction field cached in `homeDir`). Used only by eaten-ghost
    // eyes routing — see updateGhost()'s comment on why the normal
    // greedy nearest-tile-to-target heuristic isn't safe for a FIXED
    // target (it can loop forever around certain maze pockets, confirmed
    // live). Computed once per ghost in init(), since the maze layout
    // never changes.
    void computeHomeDirField(int homeIdx);
    // Mirror of computeHomeDirField(), but the BFS target is GHOST_SPAWNS
    // (the row-8 corridor tile) instead of the ghost-house box interior —
    // gives a revived ghost (see Ghost::exiting) a path back OUT through
    // the gate to resume normal chase from the same tile it originally
    // started from.
    void computeExitDirField(int homeIdx);
    void checkGhostCollisions();
    void resetPlayerAndGhostPositions();
    void drawMaze(pixelroot32::graphics::Renderer& renderer);
    void drawPellets(pixelroot32::graphics::Renderer& renderer);
    void drawPlayer(pixelroot32::graphics::Renderer& renderer);
    void drawGhosts(pixelroot32::graphics::Renderer& renderer);
    void drawFruit(pixelroot32::graphics::Renderer& renderer);
    void drawHUD(pixelroot32::graphics::Renderer& renderer);

    // Player position: pixel coordinates of the top-left of Pac-Man's
    // LOGICAL 8x8 collision tile (matching the maze grid exactly) — the
    // drawn sprite is larger (26x26) and centered on this box, same
    // "visual bigger than collision" idiom Bubble Bobble used for Bub.
    float playerX = 0.0f;
    float playerY = 0.0f;
    int dirX = 0;
    int dirY = 0;
    int wantDirX = 0;
    int wantDirY = 0;
    int animFrame = 0;
    unsigned long animAccumulatorMs = 0;

    int score = 0;
    int lives = 3;
    int pelletsRemaining = 0;
    int totalPellets = 0;
    bool gameOver = false;
    unsigned long invulnerableUntilMs = 0;

    // Level progression: real Pac-Man has no separate maze layouts, only
    // a per-level difficulty/fruit table (see LEVEL_SPECS in the .cpp) —
    // clearing all pellets advances the level and re-lays the SAME maze,
    // it never "ends" the game. `level` persists across a life loss
    // (only a full restart/init() resets it to 1) and picks the active
    // row of LEVEL_SPECS via applyLevelSpec(), clamped to the last entry
    // once past the table's length (matching the reference's own
    // eventual plateau).
    int level = 1;
    float currentPlayerSpeed = 0.0f;
    float currentGhostSpeed = 0.0f;
    float currentGhostFrightSpeed = 0.0f;
    unsigned long currentFrightMs = 0;
    int currentFruitScore = 0;

    // Bonus fruit: real thresholds from the reference (game.hpp,
    // fruit_1_eaten=70 / fruit_2_eaten=170 pellets eaten) — each fires
    // once per level (not just once per game — `advanceLevel()` resets
    // these two flags). Position is fixed (just below the ghost house, a
    // verified open/reachable tile), not per-level like the reference.
    bool fruitStage1Spawned = false;
    bool fruitStage2Spawned = false;
    bool fruitActive = false;
    unsigned long fruitUntilMs = 0;

    static constexpr int kNumGhosts = 4;
    Ghost ghosts[kNumGhosts];

    bool paused = false;
    bool pauseLatched = false;
    unsigned long encSwHeldSinceMs = 0;
    bool returnToMenuRequested = false;
    bool returnToMenuLatched = false;

    // Per-tile "already eaten" state — parallel to the maze's own pellet
    // markers, reset every init(). Not stored IN the maze array itself so
    // the maze data can stay a plain immutable `const char* const[]` like
    // Bubble Bobble's LEVELS.
    bool pelletEaten[kMazeRows][kMazeCols] = {};

    // Shortest-path direction field toward each ghost's own home tile —
    // see computeHomeDirField()'s comment. 0=none/unreachable,
    // 1=up, 2=left, 3=down, 4=right (matches the same up/left/down/right
    // ordering used by updateGhost()'s own candidate list).
    uint8_t homeDir[kNumGhosts][kMazeRows][kMazeCols] = {};
    // Same shape, opposite trip — see computeExitDirField()/Ghost::exiting.
    uint8_t exitDir[kNumGhosts][kMazeRows][kMazeCols] = {};

    // Sound — piezo buzzer only, Pac-Man scene exclusively (see
    // pacman_pr32_audio.h's own comment for why simple tone() sequences
    // instead of the engine's real sample-based AudioEngine). `sfx` is
    // the single shared monophonic channel for the intro jingle and every
    // one-shot cue; `introActive` freezes gameplay updates while the
    // intro jingle plays, matching the real arcade holding everything
    // still at the start of a game.
    pacman_pr32_audio::NoteSequencer sfx;
    bool introActive = false;

    // Real arcade attract screen: "CHARACTER / NICKNAME" ghost roster
    // reveal + the point-value legend + the Pac-Man-eats-a-ghost-train
    // demo animation, replicated from a real cabinet recording (user-
    // supplied video), shown once each time this scene is entered,
    // BEFORE the existing intro-jingle-freeze/gameplay sequence starts.
    // `titlePhase` 0 = title screen showing, 1 = normal (title finished,
    // falls through to the existing introActive flow). Purely
    // time-driven (`titleTimer`), no separate state struct needed.
    int titlePhase = 0;
    float titleTimer = 0.0f;
    int hiscore = 0;  // shown on the title screen's "HIGH SCORE" line

    void drawTitleScreen(pixelroot32::graphics::Renderer& renderer);
};

} // namespace pacman_pr32
