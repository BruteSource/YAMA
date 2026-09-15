// Arkanoid's own custom sprite palette — 16 RGB565 values, one per
// standard Color:: enum slot (same order Color.h defines them: Black,
// White, Navy, Blue, Cyan, DarkGreen, Green, LightGreen, Yellow, Orange,
// LightRed, Red, DarkRed, Purple, Magenta, Gray).
//
// Needed because Arkanoid never had its own custom palette (unlike
// Bubble Bobble/Pac-Man/Galaga, each of which calls
// setSpriteCustomPalette() + enableDualPaletteMode(true) in their own
// init()) — so its Sprite-context drawing was silently riding on
// whatever palette a PREVIOUSLY played scene left installed (a global,
// not-reset-between-scenes setting), and its Background-context drawing
// rode on the engine's raw, un-compensated PR32 palette. Confirmed live
// on real hardware: HUD text meant to be red rendered blue, a brick row
// meant to be yellow rendered cyan — an R<->B channel swap, exactly the
// same real-panel hardware quirk every other scene's own sprite
// generator script already compensates for by pre-swapping its palette
// values (see e.g. tools/make_galaga_pr32_sprites.py's to_rgb565()).
//
// Values below were generated the same way: pick the RGB color actually
// wanted, then pack it through that same swapped bit-layout
// ((b&0xF8)<<8)|((g&0xFC)<<3)|(r>>3) so the panel's hardware swap
// un-swaps it back to the intended hue. Since every Arkanoid draw call
// now goes through THIS palette (installed once in init()), no
// Background-context switching is needed anywhere in this scene —
// default Sprite context is correct everywhere.
#pragma once
#include <cstdint>

namespace arkanoid_pr32_palette {

static const uint16_t PALETTE_RGB565[16] = {
    0x0000,  // Black
    0xFFFF,  // White
    0x6800,  // Navy       (dark blue - arena floor dark tile, paddle shadow)
    0xE2C0,  // Blue       (arena floor bright tile)
    0xDEE0,  // Cyan       (ball, paddle tip accent)
    0x02C0,  // DarkGreen
    0x0640,  // Green      (brick green row)
    0x7FEF,  // LightGreen
    0x065C,  // Yellow     (brick yellow row)
    0x037C,  // Orange     (paddle end caps)
    0x6B7F,  // LightRed
    0x10BB,  // Red        (HUD text, brick red row, TAITO)
    0x000F,  // DarkRed
    0xB011,  // Purple
    0xB019,  // Magenta    (brick pink row)
    0x94B2,  // Gray       (walls, silver brick, HUD lives/round)
};

} // namespace arkanoid_pr32_palette
