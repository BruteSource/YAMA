// Tetris (internal codename "Block Stack" — see the file/namespace
// naming convention note in rtype_pr32.h; on-screen title is "TETRIS"
// per the user's explicit call, reversing the original name choice
// below). Mechanics (7 tetromino shapes, SRS-style rotation, 7-bag
// randomizer, hold/next queue, tiered line-clear scoring) ported
// faithfully from studying Tetro48/cambridge (cloned to
// /tmp/cambridge_ref, a real open-source "falling-block game engine"
// that itself avoids the name "Tetris" for trademark reasons — Tetris
// is unusually aggressively trademark-enforced compared to the other
// real arcade names this project uses elsewhere, which is why this
// game originally shipped under an original name when every other
// game kept its real one; noted here for the record, not reversed
// unilaterally). The mechanics themselves are generic, publicly-
// documented game rules, not copyrightable expression. All visuals are
// flat-colored primitives (no cropped sprites needed, simplest asset
// pipeline of any game in this project) and all audio is original.
//
// Same "hand-rolled logic in a plain Scene, no Actors/CollisionSystem"
// pattern as every other game here.
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "blockstack_pr32_audio.h"

namespace blockstack_pr32 {

enum class PieceType { I, J, L, O, S, T, Z, Count };
constexpr int kNumPieceTypes = static_cast<int>(PieceType::Count);

struct CellOffset { int8_t col; int8_t row; };

// Per-shape, per-rotation-state (0=spawn,1=CW,2=180,3=CCW) cell offsets
// within a bounding box (4x4 for I, 2x2 for O, 3x3 for the rest) — the
// standard SRS piece geometry, a publicly-documented rotation-system
// specification used across many independent implementations.
extern const CellOffset kPieceShapes[kNumPieceTypes][4][4];
extern const int kPieceBoxSize[kNumPieceTypes];

struct ActivePiece {
    PieceType type = PieceType::I;
    int rotation = 0;
    int col = 0;  // bounding-box top-left, in grid coordinates
    int row = 0;
};

class BlockStackScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    bool wantsReturnToMenu() const { return returnToMenuRequested; }
    void clearReturnToMenuRequest() { returnToMenuRequested = false; }

private:
    Pr32Input pendingInput;

    static constexpr int kCols = 10;
    static constexpr int kVisibleRows = 20;
    static constexpr int kHiddenRows = 2;  // spawn buffer above the visible field
    static constexpr int kRows = kVisibleRows + kHiddenRows;

    // 0 = empty, otherwise a Color enum value + 1 (so Color::Black, a
    // legitimate color, isn't confused with "empty").
    uint8_t grid[kRows][kCols] = {};

    ActivePiece current;
    PieceType holdType = PieceType::Count;  // Count = no hold piece yet
    bool holdUsedThisPiece = false;
    static constexpr int kNextQueueLen = 3;
    PieceType nextQueue[kNextQueueLen];

    // 7-bag randomizer: shuffle one of each piece per bag, refill when
    // exhausted — guarantees no long droughts of any one shape.
    PieceType bag[kNumPieceTypes];
    int bagIndex = kNumPieceTypes;  // forces an initial shuffle
    void refillBagIfNeeded();
    PieceType drawFromBag();
    void spawnPiece();

    bool pieceFits(PieceType type, int rotation, int col, int row) const;
    void lockPiece();
    int clearLines();  // returns number of lines cleared, collapses the grid
    void tryRotate(int dir);  // dir = +1 CW, -1 CCW
    void tryMove(int dcol, int drow);
    void hardDrop();
    void holdPiece();

    float gravityTimer = 0.0f;
    float gravityIntervalS = 0.8f;
    float lockDelayTimer = 0.0f;
    bool onGround = false;
    static constexpr float kLockDelayS = 0.5f;

    float dasTimer = 0.0f;  // delayed-auto-shift for held left/right
    int dasDirection = 0;
    static constexpr float kDasDelayS = 0.18f;
    static constexpr float kDasRepeatS = 0.045f;

    // The physical encoder emits 4 raw quadrature counts per real mechanical
    // detent (confirmed by menu_pr32.cpp's own kEncStepThreshold=4, needed
    // for the exact same reason) -- rotating on every raw encDelta tick
    // without accumulating first fires up to 4 rotations per actual knob
    // click, which is what made encoder-rotate feel "way too sensitive"
    // when it was first wired up directly.
    int encRotateAccum = 0;
    static constexpr int kEncStepThreshold = 4;

    int score = 0;
    int level = 1;
    int linesCleared = 0;
    bool gameOver = false;
    bool paused = false;
    unsigned long encSwHeldSinceMs = 0;
    bool pauseLatched = false;
    bool returnToMenuLatched = false;
    bool returnToMenuRequested = false;

    // Brief flash on the rows just cleared, purely cosmetic.
    static constexpr int kMaxFlashRows = 4;
    int flashRows[kMaxFlashRows];
    int flashRowCount = 0;
    float flashTimer = 0.0f;
    static constexpr float kFlashDurationS = 0.15f;

    // Startup animation (title -> ready) — same titlePhase/timer idiom
    // as every other game here.
    int titlePhase = 0;
    float titleTimer = 0.0f;
    bool introActive = false;
    void drawTitleScreen(pixelroot32::graphics::Renderer& renderer);

    void drawGrid(pixelroot32::graphics::Renderer& renderer);
    void drawPiece(pixelroot32::graphics::Renderer& renderer, const ActivePiece& p, int originX, int originY, bool ghost);
    void drawHUD(pixelroot32::graphics::Renderer& renderer);
    pixelroot32::graphics::Color colorForPiece(PieceType type) const;

    // Ambient background: dim falling-glyph "code rain" columns using
    // an ORIGINAL invented 8x8 pictographic glyph set (hand-drawn
    // abstract symbols, not real letters or any real writing system —
    // see kRainGlyphBitmaps in the .cpp), neon green/yellow tones only.
    // Drawn full-screen, UNDER the field's stacked blocks/current piece
    // but with no opaque field-background fill on top of it anymore, so
    // it shows through empty field cells too, not just the margins.
    struct RainStreak { float x; float y; float speed; int glyphIndex; float glyphChangeTimer; pixelroot32::graphics::Color color; };
    static constexpr int kNumRainStreaks = 40;
    RainStreak rainStreaks[kNumRainStreaks];
    void initRain();
    void updateRain(float dt);
    void drawRain(pixelroot32::graphics::Renderer& renderer);

    // Sized to actually use the display. Height is the binding
    // constraint (20 visible rows must fit in the 320px screen), not
    // width — a 10-wide/20-tall board is a fixed 1:2 ratio with square
    // cells, so pushing cell size up scales both dimensions together
    // automatically. 15px is the largest that fits: 20*15=300 field
    // height + the 2px border on top/bottom (304) + a few px of gap
    // still comes in under 320, whereas 16px (320 exactly) leaves zero
    // room for the border at all. kFieldLeft is chosen so the whole
    // composition (field + the NEXT/HOLD/stats column to its right,
    // which tops out at ~64px wide for a 4-wide piece preview) sits
    // roughly centered in the 240px screen, rather than pinned to the
    // left edge with all the leftover width dumped on the right (an
    // earlier pass here did exactly that, reported live as "a lot of
    // empty space on the right").
    static constexpr int kCellSize = 15;
    static constexpr int kFieldLeft = 10;
    static constexpr int kFieldTop = 6;  // top of the VISIBLE field

    blockstack_pr32_audio::NoteSequencer sfx;
};

} // namespace blockstack_pr32
