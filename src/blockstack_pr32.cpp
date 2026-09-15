#include "blockstack_pr32.h"
#include "menu_pr32_font.h"
#include <graphics/FontManager.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pr32 = pixelroot32;
using pixelroot32::graphics::Color;
using pixelroot32::graphics::Renderer;
namespace fm = pixelroot32::graphics;

namespace blockstack_pr32 {

static const pr32::graphics::Font* const kHudFont = &menu_pr32_font::FONT_SMALL;
static const pr32::graphics::Font* const kLogoFont = &menu_pr32_font::FONT_LARGE;

// Standard SRS piece geometry (row increases downward, matching this
// project's own screen-space convention throughout) — see blockstack_pr32.h.
const CellOffset kPieceShapes[kNumPieceTypes][4][4] = {
    // I (4x4 box)
    {
        { {0,1},{1,1},{2,1},{3,1} },
        { {2,0},{2,1},{2,2},{2,3} },
        { {0,2},{1,2},{2,2},{3,2} },
        { {1,0},{1,1},{1,2},{1,3} },
    },
    // J (3x3 box)
    {
        { {0,0},{0,1},{1,1},{2,1} },
        { {1,0},{2,0},{1,1},{1,2} },
        { {0,1},{1,1},{2,1},{2,2} },
        { {1,0},{1,1},{0,2},{1,2} },
    },
    // L (3x3 box)
    {
        { {2,0},{0,1},{1,1},{2,1} },
        { {1,0},{1,1},{1,2},{2,2} },
        { {0,1},{1,1},{2,1},{0,2} },
        { {0,0},{1,0},{1,1},{1,2} },
    },
    // O (2x2 box, same in every rotation)
    {
        { {0,0},{1,0},{0,1},{1,1} },
        { {0,0},{1,0},{0,1},{1,1} },
        { {0,0},{1,0},{0,1},{1,1} },
        { {0,0},{1,0},{0,1},{1,1} },
    },
    // S (3x3 box)
    {
        { {1,0},{2,0},{0,1},{1,1} },
        { {1,0},{1,1},{2,1},{2,2} },
        { {1,1},{2,1},{0,2},{1,2} },
        { {0,0},{0,1},{1,1},{1,2} },
    },
    // T (3x3 box)
    {
        { {1,0},{0,1},{1,1},{2,1} },
        { {1,0},{1,1},{2,1},{1,2} },
        { {0,1},{1,1},{2,1},{1,2} },
        { {1,0},{0,1},{1,1},{1,2} },
    },
    // Z (3x3 box)
    {
        { {0,0},{1,0},{1,1},{2,1} },
        { {2,0},{1,1},{2,1},{1,2} },
        { {0,1},{1,1},{1,2},{2,2} },
        { {1,0},{0,1},{1,1},{0,2} },
    },
};

const int kPieceBoxSize[kNumPieceTypes] = { 4, 3, 3, 2, 3, 3, 3 };

Color BlockStackScene::colorForPiece(PieceType type) const {
    switch (type) {
        case PieceType::I: return Color::Cyan;
        case PieceType::J: return Color::Blue;
        case PieceType::L: return Color::Orange;
        case PieceType::O: return Color::Yellow;
        case PieceType::S: return Color::Green;
        case PieceType::T: return Color::Purple;
        case PieceType::Z: return Color::Red;
        default: return Color::White;
    }
}

void BlockStackScene::refillBagIfNeeded() {
    if (bagIndex < kNumPieceTypes) return;
    for (int i = 0; i < kNumPieceTypes; ++i) bag[i] = static_cast<PieceType>(i);
    // Fisher-Yates shuffle.
    for (int i = kNumPieceTypes - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        PieceType t = bag[i]; bag[i] = bag[j]; bag[j] = t;
    }
    bagIndex = 0;
}

PieceType BlockStackScene::drawFromBag() {
    refillBagIfNeeded();
    return bag[bagIndex++];
}

bool BlockStackScene::pieceFits(PieceType type, int rotation, int col, int row) const {
    int box = kPieceBoxSize[static_cast<int>(type)];
    for (int i = 0; i < 4; ++i) {
        const CellOffset& off = kPieceShapes[static_cast<int>(type)][rotation][i];
        int c = col + off.col;
        int r = row + off.row;
        if (c < 0 || c >= kCols || r >= kRows) return false;
        if (r < 0) continue;  // above the visible/hidden field entirely, allowed
        if (grid[r][c] != 0) return false;
    }
    (void)box;
    return true;
}

void BlockStackScene::spawnPiece() {
    current.type = nextQueue[0];
    for (int i = 0; i < kNextQueueLen - 1; ++i) nextQueue[i] = nextQueue[i + 1];
    nextQueue[kNextQueueLen - 1] = drawFromBag();

    current.rotation = 0;
    int box = kPieceBoxSize[static_cast<int>(current.type)];
    current.col = (kCols - box) / 2;
    current.row = kHiddenRows - (box == 4 ? 2 : 2);  // spawn straddling the hidden rows, matches standard convention
    if (current.row < 0) current.row = 0;

    holdUsedThisPiece = false;
    gravityTimer = 0.0f;
    lockDelayTimer = 0.0f;
    onGround = false;

    if (!pieceFits(current.type, current.rotation, current.col, current.row)) {
        gameOver = true;
        sfx.play(blockstack_pr32_audio::kGameOverSfx, blockstack_pr32_audio::kGameOverSfxLen);
    }
}

void BlockStackScene::tryMove(int dcol, int drow) {
    if (pieceFits(current.type, current.rotation, current.col + dcol, current.row + drow)) {
        current.col += dcol;
        current.row += drow;
        if (drow != 0) {
            onGround = false;
            lockDelayTimer = 0.0f;
        }
    } else if (drow > 0) {
        onGround = true;
    }
}

void BlockStackScene::tryRotate(int dir) {
    int newRotation = (current.rotation + dir + 4) % 4;
    // Simplified wall-kick set (not the full official SRS kick table —
    // a pragmatic approximation, same spirit as Arkanoid's simplified
    // brick-collision vs. its own reference's full corner detection):
    // try the naive rotation in place first, then a small set of nearby
    // offsets so rotating near a wall or the stack doesn't just fail.
    static const int kKickTests[5][2] = { {0,0}, {-1,0}, {1,0}, {0,-1}, {-2,0} };
    for (const auto& k : kKickTests) {
        int testCol = current.col + k[0];
        int testRow = current.row + k[1];
        if (pieceFits(current.type, newRotation, testCol, testRow)) {
            current.rotation = newRotation;
            current.col = testCol;
            current.row = testRow;
            onGround = false;
            lockDelayTimer = 0.0f;
            sfx.play(blockstack_pr32_audio::kRotateSfx, blockstack_pr32_audio::kRotateSfxLen);
            return;
        }
    }
}

void BlockStackScene::lockPiece() {
    int box = kPieceBoxSize[static_cast<int>(current.type)];
    for (int i = 0; i < 4; ++i) {
        const CellOffset& off = kPieceShapes[static_cast<int>(current.type)][current.rotation][i];
        int c = current.col + off.col;
        int r = current.row + off.row;
        if (r >= 0 && r < kRows && c >= 0 && c < kCols) {
            grid[r][c] = static_cast<uint8_t>(colorForPiece(current.type)) + 1;
        }
    }
    (void)box;
    sfx.play(blockstack_pr32_audio::kLockSfx, blockstack_pr32_audio::kLockSfxLen);

    int cleared = clearLines();
    if (cleared > 0) {
        static const int kScoreTable[5] = { 0, 100, 300, 500, 800 };
        score += kScoreTable[cleared] * level;
        linesCleared += cleared;
        int newLevel = 1 + linesCleared / 10;
        if (newLevel != level) {
            level = newLevel;
            gravityIntervalS = 0.8f - (level - 1) * 0.06f;
            if (gravityIntervalS < 0.08f) gravityIntervalS = 0.08f;
            sfx.play(blockstack_pr32_audio::kLevelUpSfx, blockstack_pr32_audio::kLevelUpSfxLen);
        } else {
            switch (cleared) {
                case 1: sfx.play(blockstack_pr32_audio::kClear1Sfx, blockstack_pr32_audio::kClear1SfxLen); break;
                case 2: sfx.play(blockstack_pr32_audio::kClear2Sfx, blockstack_pr32_audio::kClear2SfxLen); break;
                case 3: sfx.play(blockstack_pr32_audio::kClear3Sfx, blockstack_pr32_audio::kClear3SfxLen); break;
                default: sfx.play(blockstack_pr32_audio::kClear4Sfx, blockstack_pr32_audio::kClear4SfxLen); break;
            }
        }
    }

    if (!gameOver) spawnPiece();
}

int BlockStackScene::clearLines() {
    flashRowCount = 0;
    for (int r = 0; r < kRows; ++r) {
        bool full = true;
        for (int c = 0; c < kCols; ++c) {
            if (grid[r][c] == 0) { full = false; break; }
        }
        if (full && flashRowCount < kMaxFlashRows) flashRows[flashRowCount++] = r;
    }
    if (flashRowCount == 0) return 0;
    flashTimer = kFlashDurationS;

    // Collapse immediately (the flash is purely cosmetic on top of the
    // already-updated grid state, not a gameplay-blocking animation).
    int writeRow = kRows - 1;
    for (int r = kRows - 1; r >= 0; --r) {
        bool full = false;
        for (int i = 0; i < flashRowCount; ++i) {
            if (flashRows[i] == r) { full = true; break; }
        }
        if (!full) {
            if (writeRow != r) {
                for (int c = 0; c < kCols; ++c) grid[writeRow][c] = grid[r][c];
            }
            --writeRow;
        }
    }
    for (int r = writeRow; r >= 0; --r) {
        for (int c = 0; c < kCols; ++c) grid[r][c] = 0;
    }
    return flashRowCount;
}

void BlockStackScene::hardDrop() {
    int dropRows = 0;
    while (pieceFits(current.type, current.rotation, current.col, current.row + dropRows + 1)) ++dropRows;
    current.row += dropRows;
    score += dropRows * 2;
    sfx.play(blockstack_pr32_audio::kHardDropSfx, blockstack_pr32_audio::kHardDropSfxLen);
    lockPiece();
}

void BlockStackScene::holdPiece() {
    if (holdUsedThisPiece) return;
    holdUsedThisPiece = true;
    PieceType prevHold = holdType;
    holdType = current.type;
    if (prevHold == PieceType::Count) {
        spawnPiece();
    } else {
        current.type = prevHold;
        current.rotation = 0;
        int box = kPieceBoxSize[static_cast<int>(current.type)];
        current.col = (kCols - box) / 2;
        current.row = kHiddenRows - 2;
        if (current.row < 0) current.row = 0;
        gravityTimer = 0.0f;
        lockDelayTimer = 0.0f;
        onGround = false;
    }
    sfx.play(blockstack_pr32_audio::kHoldSfx, blockstack_pr32_audio::kHoldSfxLen);
}

void BlockStackScene::init() {
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);

    memset(grid, 0, sizeof(grid));
    holdType = PieceType::Count;
    bagIndex = kNumPieceTypes;
    for (int i = 0; i < kNextQueueLen; ++i) nextQueue[i] = drawFromBag();
    spawnPiece();

    score = 0;
    level = 1;
    linesCleared = 0;
    gravityIntervalS = 0.8f;
    gameOver = false;
    paused = false;
    pauseLatched = false;
    returnToMenuRequested = false;
    returnToMenuLatched = false;
    dasTimer = 0.0f;
    dasDirection = 0;
    flashRowCount = 0;

    titlePhase = 0;
    titleTimer = 0.0f;
    introActive = false;

    initRain();

    Scene::init();
}

// Original invented 8x8 pictographic glyph set for the falling-code
// rain — hand-drawn ASYMMETRIC abstract shapes. An earlier version here
// used simple symmetric shapes (a circle+dot, a plain cross, a hash
// grid) which, despite being genuinely hand-drawn and not traced from
// any real alphabet, still collapsed visually into recognizable
// typographic symbols (looked like a Venus sign, a plus, a pound sign)
// — simple/symmetric pixel shapes tend to do that. These are
// deliberately irregular/asymmetric instead, closer to an invented
// alien script than a coincidental real symbol. Each row is one byte,
// bit 7 = leftmost pixel.
static const uint8_t kRainGlyphBitmaps[6][8] = {
    { 0b01100000, 0b01100000, 0b01111000, 0b00011000, 0b00011100, 0b00001100, 0b00001100, 0b00011110 },
    { 0b00011000, 0b00111100, 0b01100110, 0b11000011, 0b11111111, 0b10000001, 0b10000001, 0b11000011 },
    { 0b11110000, 0b10000000, 0b10111100, 0b10100100, 0b10100100, 0b10111100, 0b00000100, 0b00000100 },
    { 0b00001111, 0b00001001, 0b01111001, 0b01000001, 0b01000001, 0b01111111, 0b00000001, 0b00000001 },
    { 0b01111110, 0b01000010, 0b01011010, 0b01011010, 0b01000010, 0b00111100, 0b00011000, 0b00110000 },
    { 0b10000001, 0b11000011, 0b01100110, 0b00111100, 0b00011100, 0b01100110, 0b11000010, 0b10000001 },
};
constexpr int kNumRainGlyphBitmaps = 6;

void BlockStackScene::initRain() {
    static const Color kRainColors[3] = { Color::Green, Color::LightGreen, Color::Yellow };
    for (auto& s : rainStreaks) {
        s.x = static_cast<float>(rand() % SCREEN_W);
        s.y = static_cast<float>(rand() % SCREEN_H);
        s.speed = 18.0f + static_cast<float>(rand() % 22);
        s.glyphIndex = rand() % kNumRainGlyphBitmaps;
        s.glyphChangeTimer = 0.4f + static_cast<float>(rand() % 6) * 0.1f;
        s.color = kRainColors[rand() % 3];
    }
}

void BlockStackScene::updateRain(float dt) {
    for (auto& s : rainStreaks) {
        s.y += s.speed * dt;
        s.glyphChangeTimer -= dt;
        if (s.glyphChangeTimer <= 0.0f) {
            s.glyphIndex = rand() % kNumRainGlyphBitmaps;
            s.glyphChangeTimer = 0.4f + static_cast<float>(rand() % 6) * 0.1f;
        }
        if (s.y > SCREEN_H) {
            s.y = -10.0f;
            s.x = static_cast<float>(rand() % SCREEN_W);
            s.speed = 18.0f + static_cast<float>(rand() % 22);
        }
    }
}

// Draws one glyph instance. `ditherStride`>1 skips pixels in a
// checkerboard-ish pattern (only 1-in-N drawn) to fake a fading trail
// without needing extra colors — keeps every trail pixel the SAME
// green/yellow hue as its head, just sparser, which reads as a dimmer
// afterglow at this resolution.
static void drawRainGlyphAt(Renderer& renderer, const uint8_t* rows, int gx, int gy, Color color, int ditherStride) {
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        if (bits == 0) continue;
        int y = gy + row;
        if (y < 0 || y >= SCREEN_H) continue;
        for (int col = 0; col < 8; ++col) {
            if (!(bits & (0x80 >> col))) continue;
            if (ditherStride > 1 && ((col + row) % ditherStride) != 0) continue;
            renderer.drawFilledRectangle(gx + col, y, 1, 1, color);
        }
    }
}

void BlockStackScene::drawRain(Renderer& renderer) {
    constexpr int kTrailSpacing = 6;   // px between the head and each trailing echo
    constexpr int kNumTrailEchoes = 8;  // further-back echoes are cheap: each uses a
                                         // sparser dither (ditherStride=e+1), so cost
                                         // grows sub-linearly with more of them
    for (const auto& s : rainStreaks) {
        const uint8_t* rows = kRainGlyphBitmaps[s.glyphIndex];
        int gx = static_cast<int>(s.x);
        int gy = static_cast<int>(s.y);
        // Trail first (drawn behind/above), then the full-density head
        // on top, dimmer the further back each echo sits.
        for (int e = kNumTrailEchoes; e >= 1; --e) {
            drawRainGlyphAt(renderer, rows, gx, gy - e * kTrailSpacing, s.color, e + 1);
        }
        drawRainGlyphAt(renderer, rows, gx, gy, s.color, 1);
    }
}

// Loops blockstack_pr32_audio::kThemeMusic on the single shared buzzer
// voice, ducking out for one-shot SFX (see the header's comment on why).
void BlockStackScene::updateMusic() {
    if (!musicShouldPlay) {
        if (music.isPlaying()) music.stop();
        return;
    }
    if (sfx.isPlaying()) return;  // one-shot SFX has the only voice this frame
    if (!music.isPlaying()) {
        music.play(blockstack_pr32_audio::kThemeMusic, blockstack_pr32_audio::kThemeMusicLen);
    } else {
        music.update();
    }
}

void BlockStackScene::update(unsigned long deltaTime) {
    constexpr unsigned long kMaxFrameMs = 50;
    if (deltaTime > kMaxFrameMs) deltaTime = kMaxFrameMs;
    float dt = static_cast<float>(deltaTime) * 0.001f;

    updateRain(dt);  // always animates, including on the title screen/paused

    if (titlePhase == 0) {
        titleTimer += dt;
        sfx.update();
        musicShouldPlay = false;
        updateMusic();
        constexpr float kTitleDurationS = 4.0f;
        if (pendingInput.aEdge || titleTimer >= kTitleDurationS) {
            titlePhase = 1;
            sfx.play(blockstack_pr32_audio::kIntroJingle, blockstack_pr32_audio::kIntroJingleLen);
        }
        Scene::update(deltaTime);
        return;
    }

    constexpr unsigned long kPauseHoldMs = 500;
    constexpr unsigned long kReturnToMenuHoldMs = 2000;
    if (pendingInput.r3Held) {
        if (encSwHeldSinceMs == 0) encSwHeldSinceMs = millis();
        unsigned long heldMs = millis() - encSwHeldSinceMs;
        if (!pauseLatched && heldMs >= kPauseHoldMs && heldMs < kReturnToMenuHoldMs) {
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
        musicShouldPlay = false;
        updateMusic();
        Scene::update(deltaTime);
        return;
    }

    sfx.update();

    if (flashTimer > 0.0f) flashTimer -= dt;

    if (gameOver) {
        musicShouldPlay = false;
        updateMusic();
        if (pendingInput.aEdge) init();
        Scene::update(deltaTime);
        return;
    }

    musicShouldPlay = true;
    updateMusic();

    // Left/right movement: thumbstick only, with DAS (delayed auto-shift,
    // same idiom as every serious falling-block game) — the encoder used
    // to also nudge left/right here, but now rotates instead (below).
    float tx = pendingInput.thumbX;
    int dir = 0;
    if (fabsf(tx) > 0.35f) dir = (tx > 0) ? 1 : -1;

    if (dir != dasDirection) {
        dasDirection = dir;
        dasTimer = 0.0f;
        if (dir != 0) tryMove(dir, 0);
    } else if (dir != 0) {
        dasTimer += dt;
        float threshold = (dasTimer < kDasDelayS) ? kDasDelayS : kDasDelayS + kDasRepeatS;
        if (dasTimer >= threshold) {
            tryMove(dir, 0);
            dasTimer = kDasDelayS;  // re-arm for the repeat rate, not the initial delay
        }
    }
    // Encoder rotates instead of nudging left/right (user's explicit
    // preference over the redundant-with-thumbstick move it used to do).
    // Accumulate raw ticks and only rotate every kEncStepThreshold (4) of
    // them, same as menu_pr32.cpp's own encoder handling — the physical
    // encoder emits 4 raw quadrature counts per real detent, so rotating
    // on every raw tick fired up to 4 rotations per actual knob click
    // (reported live as "way too sensitive").
    encRotateAccum += static_cast<int>(pendingInput.encDelta);
    if (encRotateAccum >= kEncStepThreshold) {
        tryRotate(1);
        encRotateAccum = 0;
    } else if (encRotateAccum <= -kEncStepThreshold) {
        tryRotate(-1);
        encRotateAccum = 0;
    }

    if (pendingInput.rbEdge) tryRotate(1);
    if (pendingInput.lbEdge) tryRotate(-1);
    if (pendingInput.aEdge) holdPiece();
    // R3 (clicking the encoder in) is the "auto drop" hard-drop instead of
    // hold piece — A takes over hold piece instead, since hard drop moved
    // off it. Just `r3Edge` alone, matching every other game's convention
    // (rtype_pr32.cpp, digdug_pr32.cpp) — the old `&& !r3Held` guard here
    // could never actually pass on real hardware: r3Edge only goes true on
    // the exact frame the button transitions to pressed, and r3Held is
    // already true that same frame (it reflects current state, read right
    // after r3Edge is computed from the same press), so `!r3Held` was
    // always false whenever r3Edge was true. This silently broke hold
    // piece before (never fired from a real press) and broke hard drop the
    // same way the moment it was moved onto this binding.
    if (pendingInput.r3Edge) hardDrop();

    // thumbY follows the real thumbstick's own raw-value convention
    // (positive = physically UP, confirmed in digdug_pr32.cpp's identical
    // up/down split and main.cpp's debug-injection comment) — this was
    // checking `> 0.5f`, i.e. "stick pushed UP", so soft drop could only
    // ever fire by pushing up, never down as intended.
    bool softDrop = pendingInput.thumbY < -0.5f;
    float interval = softDrop ? gravityIntervalS * 0.1f : gravityIntervalS;
    gravityTimer += dt;
    if (gravityTimer >= interval) {
        gravityTimer = 0.0f;
        if (pieceFits(current.type, current.rotation, current.col, current.row + 1)) {
            current.row += 1;
            if (softDrop) score += 1;
        } else {
            onGround = true;
        }
    }

    if (onGround) {
        lockDelayTimer += dt;
        if (lockDelayTimer >= kLockDelayS) {
            lockPiece();
        }
    }

    Scene::update(deltaTime);
}

void BlockStackScene::drawPiece(Renderer& renderer, const ActivePiece& p, int originX, int originY, bool ghost) {
    Color c = ghost ? Color::Gray : colorForPiece(p.type);
    for (int i = 0; i < 4; ++i) {
        const CellOffset& off = kPieceShapes[static_cast<int>(p.type)][p.rotation][i];
        int c_ = p.col + off.col;
        int r_ = p.row + off.row;
        if (r_ < kHiddenRows) continue;  // don't draw cells still in the hidden spawn buffer
        int x = originX + c_ * kCellSize;
        int y = originY + (r_ - kHiddenRows) * kCellSize;
        if (ghost) {
            renderer.drawRectangle(x + 1, y + 1, kCellSize - 2, kCellSize - 2, c);
        } else {
            renderer.drawFilledRectangle(x + 1, y + 1, kCellSize - 2, kCellSize - 2, c);
            renderer.drawFilledRectangle(x + 1, y + 1, kCellSize - 2, 2, Color::White);
        }
    }
}

void BlockStackScene::drawGrid(Renderer& renderer) {
    int fieldW = kCols * kCellSize;
    int fieldH = kVisibleRows * kCellSize;
    // Frame drawn as 4 thin strips rather than one filled rect — the
    // interior is deliberately left untouched so the ambient rain
    // (already drawn earlier in draw(), before this) shows through
    // empty field cells too, per explicit request, instead of being
    // painted over by an opaque field background.
    renderer.drawFilledRectangle(kFieldLeft - 2, kFieldTop - 2, fieldW + 4, 2, Color::Gray);      // top
    renderer.drawFilledRectangle(kFieldLeft - 2, kFieldTop + fieldH, fieldW + 4, 2, Color::Gray);  // bottom
    renderer.drawFilledRectangle(kFieldLeft - 2, kFieldTop - 2, 2, fieldH + 4, Color::Gray);       // left
    renderer.drawFilledRectangle(kFieldLeft + fieldW, kFieldTop - 2, 2, fieldH + 4, Color::Gray);  // right

    for (int r = kHiddenRows; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            if (grid[r][c] == 0) continue;
            bool flashing = false;
            if (flashTimer > 0.0f) {
                for (int i = 0; i < flashRowCount; ++i) {
                    if (flashRows[i] == r) { flashing = true; break; }
                }
            }
            Color cell = flashing ? Color::White : static_cast<Color>(grid[r][c] - 1);
            int x = kFieldLeft + c * kCellSize;
            int y = kFieldTop + (r - kHiddenRows) * kCellSize;
            renderer.drawFilledRectangle(x + 1, y + 1, kCellSize - 2, kCellSize - 2, cell);
        }
    }

    if (!gameOver) {
        ActivePiece ghost = current;
        while (pieceFits(ghost.type, ghost.rotation, ghost.col, ghost.row + 1)) ++ghost.row;
        drawPiece(renderer, ghost, kFieldLeft, kFieldTop, true);
        drawPiece(renderer, current, kFieldLeft, kFieldTop, false);
    }
}

void BlockStackScene::drawHUD(Renderer& renderer) {
    // Compact right-hand column (the field now hugs the left edge at
    // full 14px-cell size — see kFieldLeft/kCellSize's comment — so
    // everything else lives in the ~86px strip to its right instead of
    // the old symmetric side-columns that made the field itself look
    // small in the middle of a mostly-empty screen).
    constexpr int kRightX = kFieldLeft + kCols * kCellSize + 6;
    char buf[16];

    renderer.drawText("NEXT", kRightX, 4, Color::White, 1, kHudFont);
    for (int i = 0; i < kNextQueueLen; ++i) {
        ActivePiece n; n.type = nextQueue[i]; n.rotation = 0; n.col = 0; n.row = kHiddenRows;
        drawPiece(renderer, n, kRightX, 16 + i * 40, false);
    }

    renderer.drawText("HOLD", kRightX, 166, Color::White, 1, kHudFont);
    if (holdType != PieceType::Count) {
        ActivePiece h; h.type = holdType; h.rotation = 0; h.col = 0; h.row = kHiddenRows;
        drawPiece(renderer, h, kRightX, 178, false);
    }

    renderer.drawText("SCORE", kRightX, 220, Color::White, 1, kHudFont);
    snprintf(buf, sizeof(buf), "%d", score);
    renderer.drawText(buf, kRightX, 230, Color::Yellow, 1, kHudFont);

    renderer.drawText("LEVEL", kRightX, 250, Color::White, 1, kHudFont);
    snprintf(buf, sizeof(buf), "%d", level);
    renderer.drawText(buf, kRightX, 260, Color::Yellow, 1, kHudFont);

    renderer.drawText("LINES", kRightX, 280, Color::White, 1, kHudFont);
    snprintf(buf, sizeof(buf), "%d", linesCleared);
    renderer.drawText(buf, kRightX, 290, Color::Yellow, 1, kHudFont);

    if (gameOver) {
        const char* msg = "GAME OVER";
        int w = fm::FontManager::textWidth(kHudFont, msg, 1);
        renderer.drawText(msg, SCREEN_W / 2 - w / 2, SCREEN_H / 2 - 4, Color::Red, 1, kHudFont);
        const char* hint = "PRESS A";
        int w2 = fm::FontManager::textWidth(kHudFont, hint, 1);
        renderer.drawText(hint, SCREEN_W / 2 - w2 / 2, SCREEN_H / 2 + 8, Color::White, 1, kHudFont);
    } else if (paused) {
        const char* msg = "PAUSED";
        int w = fm::FontManager::textWidth(kHudFont, msg, 1);
        renderer.drawText(msg, SCREEN_W / 2 - w / 2, SCREEN_H / 2 - 4, Color::White, 1, kHudFont);
    }
}

void BlockStackScene::drawTitleScreen(Renderer& renderer) {
    // draw() already filled black + drew the rain effect before calling
    // this — no redundant fill here (that would just paint over the rain).
    auto centerX = [&](const char* text, const pr32::graphics::Font* f, uint8_t size = 1) {
        return SCREEN_W / 2 - fm::FontManager::textWidth(f, text, size) / 2;
    };
    const char* title = "TETRIS";
    renderer.drawText(title, centerX(title, kLogoFont, 1), 100, Color::Cyan, 1, kLogoFont);
    const char* sub = "THE CLASSIC PUZZLE GAME";
    renderer.drawText(sub, centerX(sub, kHudFont), 140, Color::White, 1, kHudFont);
    if (fmodf(titleTimer, 1.0f) < 0.7f) {
        const char* push = "PRESS A TO START";
        renderer.drawText(push, centerX(push, kHudFont), 200, Color::Yellow, 1, kHudFont);
    }
}

void BlockStackScene::draw(Renderer& renderer) {
    // Block Stack has no custom Sprite palette of its own — every color
    // here is meant to resolve against the standard PR32 palette
    // (installed as the Background palette in init()). But any
    // primitive with no explicit context defaults to Sprite context,
    // which can be a STALE custom palette left active by whichever
    // scene was played immediately before this one (the exact bug
    // Arkanoid hit earlier this session) — switching once here for the
    // whole scene avoids it.
    pr32::graphics::PaletteContext bgContext = pr32::graphics::PaletteContext::Background;
    pr32::graphics::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);

    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);
    drawRain(renderer);

    if (titlePhase == 0) {
        drawTitleScreen(renderer);
    } else {
        drawGrid(renderer);
        drawHUD(renderer);
    }

    renderer.setRenderContext(saved);
}

} // namespace blockstack_pr32
