"""
Exports Bub's sprite frames for the PixelRoot32 rebuild (see
~/.claude/plans/goofy-dancing-locket.md, Phase 2) as packed 4bpp indexed
bitmaps, replacing the flat-rectangle placeholder in bubblebobble_pr32.cpp.

Same source crops/frame coordinates as the old
tools/make_bubblebobble_sprites.py (kept for the other 4 games' dormant
sibling and for Zen-Chan/bubble/food, not yet ported to this engine) —
only the OUTPUT format changes: PixelRoot32 sprites are indexed-palette
(2bpp/4bpp), not RGB565, so there's no BG-color pre-baking or masking
needed — true alpha transparency, one pixel value (0) skipped at draw time.

Format (confirmed by reading the installed engine's Renderer.cpp directly,
not assumed): row-major, 1 byte per 2 pixels, low nibble = even column,
high nibble = odd column, rowStrideBytes = ceil(width*4/8). Index 0 is
never drawn (transparent) regardless of what the palette slot holds.

Palette: PixelRoot32's Sprite4bpp::palette field is an array of `Color`
enum values, which are just indices 0-15 into whichever 16-entry RGB565
table is currently active (default engine palette, or a custom one set
via setCustomPalette()). Bub's whole sheet only has 6 solid, non-
antialiased colors total (confirmed by direct pixel scan this session),
so this script builds ONE shared custom palette across every sprite it
exports and every sprite reuses the same identity Color-index array —
no per-sprite palette remapping needed.
"""
import os
from PIL import Image, ImageOps

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REF_DIR = os.path.join(SCRIPT_DIR, "bb_ref_assets")
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "bubblebobble_pr32")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "bubblebobble_pr32_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

ALPHA_THRESHOLD = 128  # binary transparency: this art has no antialiasing

sprites = {}  # name -> (w, h, PIL.Image RGBA)


def save_sprite(name, img):
    sprites[name] = (img.width, img.height, img)
    img.save(f"{OUT_DIR}/{name}.png")


def crop(sheet_name, box, scale=1):
    img = Image.open(os.path.join(REF_DIR, sheet_name)).convert("RGBA")
    c = img.crop(box)
    if scale != 1:
        c = c.resize((c.width * scale, c.height * scale), Image.NEAREST)
    return c


# ================================================================= BUB =====
# CORRECTED this pass: BubbleCharacter.png is a 6x4 grid of 16x16 frames, not
# a 4x4 grid of 24x16 frames as the old make_bubblebobble_sprites.py assumed
# (and as this script originally copied from it). Confirmed by a column-by-
# column opacity scan: each icon's own pixels span only 16 columns, repeating
# every 16 columns (peaks at x=8, 24, 40, ...) — the old 24-wide crop box
# caught one whole icon plus the left half of its neighbor, which is exactly
# the "ghost second sprite" artifact seen on real hardware. Row height (16px)
# was already correct.
bub_walk0 = crop("BubbleCharacter.png", (0, 0, 16, 16), scale=2)
bub_walk1 = crop("BubbleCharacter.png", (16, 0, 32, 16), scale=2)
bub_jump = crop("BubbleCharacter.png", (16, 32, 32, 48), scale=2)
for name, img in [("bub_walk0", bub_walk0), ("bub_walk1", bub_walk1), ("bub_jump", bub_jump)]:
    save_sprite(f"{name}_r", img)
    save_sprite(f"{name}_l", ImageOps.mirror(img))

# =========================================================== ZEN-CHAN ======
# Enemys.png: SOURCE.md's own prose says "32x32 frames" but that's the same
# kind of documentation error BubbleCharacter.png had — a column-opacity
# scan confirms real frames are 16x16 (256/16 = 16 columns), and the OLD
# make_bubblebobble_sprites.py's actual crop *coordinates* (not its
# comment) already used 16x16 correctly, so these carry over unchanged.
#
# Raw-crop facing, corrected this pass: unlike BubbleCharacter.png (whose
# raw crop's face/eyes sit on the RIGHT, confirmed by a direct pixel look
# at bub_walk0_r.png — spikes trail on the left, face leads on the right,
# i.e. genuinely facing right), Enemys.png's raw crop has its eyes on the
# LEFT side of the sprite — it's actually drawn facing LEFT. Saving the
# raw crop as "_r" (as originally done here, copying BubbleCharacter's
# pattern without re-checking it applied to a different source sheet)
# made every Zen-Chan/Maita's "moving right" sprite visually face left and
# vice versa — reported live as enemies "moon walking" (sliding one way
# while the walk-cycle pose reads as stepping the other way). Swapped:
# raw crop is now saved as "_l", its mirror as "_r", so "_r"/"_l" mean the
# same thing (visually faces right/left) for every sprite this script
# exports, matching what bubblebobble_pr32.cpp's draw() code assumes.
zc_walk0 = crop("Enemys.png", (0, 0, 16, 16), scale=2)
zc_walk1 = crop("Enemys.png", (16, 0, 32, 16), scale=2)
for name, img in [("zc_walk0", zc_walk0), ("zc_walk1", zc_walk1)]:
    save_sprite(f"{name}_l", img)
    save_sprite(f"{name}_r", ImageOps.mirror(img))

# Maita (2nd enemy type, Components/Character/Enemys/Maita.*, GPL-3.0):
# same 256x192, 16x16-cell sheet as Zen-Chan, just row 1 instead of row 0
# (confirmed against the reference's own animation table in GameEntry.cpp:
# "zenchan_normal" = frames {0,0}/{1,0}, "maita_normal" = frames {0,1}/{1,1}
# — same two columns, one row down). Only 1 new color beyond what Bub/
# Zen-Chan/bubble/food already use (a dark magenta trim, confirmed by a
# direct pixel-color scan of this crop) — fits the shared palette with a
# slot to spare.
# Same raw-crop-faces-left correction as Zen-Chan above (same sheet, same
# orientation convention).
maita_walk0 = crop("Enemys.png", (0, 16, 16, 32), scale=2)
maita_walk1 = crop("Enemys.png", (16, 16, 32, 32), scale=2)
for name, img in [("maita_walk0", maita_walk0), ("maita_walk1", maita_walk1)]:
    save_sprite(f"{name}_l", img)
    save_sprite(f"{name}_r", ImageOps.mirror(img))

# ============================================================== BUBBLE =====
# AttackBubbleColored.png: verified 16x16 frames via the same opacity-scan
# check (matches SOURCE.md's documented size this time, no correction
# needed). Row 0 = green/Bub's color; column 3 (x=48-64) is the hollow
# "formed/floating" ring frame, same crop the old script used.
bubble = crop("AttackBubbleColored.png", (48, 0, 64, 16), scale=2)
save_sprite("bubble", bubble)

# ================================================================ FOOD =====
# Items.png: same crop coordinates the old script used (watermelon 18x16,
# fries 16x16 — not a uniform grid, found by trial crops per that script's
# own comment) — previewed directly (tools/sprite_previews/) and confirmed
# clean single icons, no bleed from a neighbor. Watermelon (100pts) + fries
# (200pts), matching the reference project's real two pickup types.
#
# Together these two sprites carry 10 distinct colors — combined with
# Bub's 5 + Zen-Chan's 3 (some already shared with Bub's) that would
# overflow the shared 15-color palette budget this script uses (one
# global custom palette shared by every sprite it exports, see the
# EXPORT section below). Rather than give food its own separate palette
# (more plumbing: PixelRoot32's palette is global unless dual-palette
# mode juggles more than the sprite/background split already in use),
# merge near-duplicate shades down to 6 colors before packing — visually
# indistinguishable at this pixel-art scale, confirmed by eye against the
# preview PNGs in tools/sprite_previews/.
_FOOD_COLOR_MERGE = {
    (255, 119, 119): (255, 0, 0),     # pink highlight -> red
    (170, 0, 0): (255, 0, 0),         # dark red -> red
    (204, 255, 0): (0, 255, 0),       # yellow-green -> green
    (255, 136, 0): (255, 187, 0),     # orange -> yellow/orange
}


def merge_colors(img):
    img = img.convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            merged = _FOOD_COLOR_MERGE.get((r, g, b))
            if merged is not None:
                px[x, y] = (merged[0], merged[1], merged[2], a)
    return img


food_watermelon = merge_colors(crop("Items.png", (304, 0, 322, 16), scale=2))
food_fries = merge_colors(crop("Items.png", (400, 0, 416, 16), scale=2))
save_sprite("food_watermelon", food_watermelon)
save_sprite("food_fries", food_fries)

# ============================================================== EXPORT =====
# Build one shared palette across every exported sprite. Slot 0 is a dummy
# (pixel value 0 is never drawn); real colors start at slot 1.
palette_rgb = [(0, 0, 0)]  # slot 0 placeholder
color_to_index = {}


def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def palette_index_for(rgb):
    if rgb not in color_to_index:
        if len(palette_rgb) >= 16:
            raise ValueError(f"Palette overflow: {rgb} would be the 17th color")
        color_to_index[rgb] = len(palette_rgb)
        palette_rgb.append(rgb)
    return color_to_index[rgb]


def pack_4bpp(img):
    w, h = img.size
    px = img.load()
    row_stride = (w * 4 + 7) // 8
    data = bytearray(row_stride * h)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            idx = 0 if a < ALPHA_THRESHOLD else palette_index_for((r, g, b))
            byte_i = y * row_stride + (x >> 1)
            if x & 1:
                data[byte_i] |= (idx & 0x0F) << 4
            else:
                data[byte_i] |= (idx & 0x0F)
    return data, row_stride


header_lines = []
header_lines.append("// AUTO-GENERATED by tools/make_bubblebobble_pr32_sprites.py — do not hand-edit.")
header_lines.append("// Bub sprite frames, packed 4bpp indexed for PixelRoot32's Sprite4bpp/drawSprite().")
header_lines.append("// Cropped from JulianRijken/BubbleBobble (GPL-3.0, see tools/bb_ref_assets/SOURCE.md).")
header_lines.append("#pragma once")
header_lines.append("#include <cstdint>")
header_lines.append("#include <graphics/Renderer.h>")
header_lines.append("")
header_lines.append("namespace bubblebobble_pr32_sprites {")
header_lines.append("")

sprite_blocks = []
for name, (w, h, img) in sprites.items():
    data, row_stride = pack_4bpp(img)
    const_name = f"BB_{name.upper()}"
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    header_lines.append(f"static const uint8_t {const_name}_DATA[] = {{ {hex_bytes} }};")
    sprite_blocks.append((const_name, w, h))
header_lines.append("")

# Palette: pad unused trailing slots with black (never referenced by any
# packed pixel value, since palette_index_for() never assigns past what's
# actually used).
while len(palette_rgb) < 16:
    palette_rgb.append((0, 0, 0))

header_lines.append("// Shared master palette across every sprite this script exports.")
header_lines.append("static const uint16_t PALETTE_RGB565[16] = {")
header_lines.append("    " + ", ".join(f"0x{to_rgb565(r, g, b):04X}" for (r, g, b) in palette_rgb))
header_lines.append("};")
header_lines.append("")
header_lines.append("// Identity index -> pixelroot32::graphics::Color mapping (Color is just a")
header_lines.append("// 0-15 palette slot index once a custom palette is active via setCustomPalette()).")
header_lines.append("static const pixelroot32::graphics::Color PALETTE_COLORS[16] = {")
header_lines.append("    " + ", ".join(f"static_cast<pixelroot32::graphics::Color>({i})" for i in range(16)))
header_lines.append("};")
header_lines.append("")

for const_name, w, h in sprite_blocks:
    header_lines.append(
        f"static const pixelroot32::graphics::Sprite4bpp {const_name} = "
        f"{{ {const_name}_DATA, PALETTE_COLORS, {w}, {h}, 16 }};"
    )
header_lines.append("")
header_lines.append("} // namespace bubblebobble_pr32_sprites")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines) + "\n")

print("Generated", len(sprites), "sprites,", len(color_to_index), "colors used (of 15 available)")
for name, (w, h, _) in sprites.items():
    print(f"  {name}: {w}x{h}")
for i, rgb in enumerate(palette_rgb):
    print(f"  slot {i}: {rgb}")
