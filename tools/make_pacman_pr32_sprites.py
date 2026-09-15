"""
Exports Pac-Man's own walk-animation frames as packed 4bpp indexed
bitmaps for PixelRoot32, from the sgothel/pacman reference project's
sprite sheet (media/tiles_all.png, MIT-licensed code / the sheet itself
credited to spriters-resource.com "Superjustinbros", assumed public
domain — see /tmp/claude-scratch/pacman_ref/media/tiles_all.txt).

Same crop->pack_4bpp->export pipeline as
tools/make_bubblebobble_pr32_sprites.py (see that script's docstring for
the packed-format rationale) — only the source sheet and crop boxes
differ.

Sheet layout (confirmed by direct per-cell visual inspection this
session, NOT by trusting the sheet's own tiles_all.txt description, which
turned out to have left/right and up/down's actual pixel content
mislabeled): line index 2 (y=28..40, 13px cells) holds Pac-Man's own 8
walk frames, 13x13 each:
  cols 0-1: facing RIGHT (open, closed)
  cols 2-3: facing LEFT  (open, closed)
  cols 4-5: facing UP    (open, closed)
  cols 6-7: facing DOWN  (open, closed)
"""
import os
from PIL import Image

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REF_SHEET = "/tmp/claude-scratch/pacman_ref/media/tiles_all.png"
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "pacman_pr32")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "pacman_pr32_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

ALPHA_THRESHOLD = 128
CELL = 13
SCALE = 1
ROW_Y = 28  # line2's y-offset in the sheet (14+14 from lines 0/1's 14px each)

sprites = {}


def save_sprite(name, img):
    sprites[name] = (img.width, img.height, img)
    img.save(f"{OUT_DIR}/{name}.png")


sheet = Image.open(REF_SHEET).convert("RGBA")

DIRS = [("right", 0), ("left", 2), ("up", 4), ("down", 6)]
for name, col0 in DIRS:
    for frame in (0, 1):
        col = col0 + frame
        box = (col * CELL, ROW_Y, col * CELL + CELL, ROW_Y + CELL)
        crop = sheet.crop(box).resize((CELL * SCALE, CELL * SCALE), Image.NEAREST)
        save_sprite(f"pm_{name}{frame}", crop)

# ================================================================ GHOSTS ===
# 4 ghosts, lines 3-6 of the sheet (14x14 cells, 4 frames each) — real,
# correctly-colored per personality (confirmed by direct visual check:
# blinky=red, clyde=orange, inky=cyan, pinky=pink, matching the classic
# arcade exactly). Only frames 0/1 used (a simple 2-frame skirt-wave
# animation, direction-agnostic) — the sheet's 4 frames likely also vary
# eye-direction per column, but that's a polish detail skipped for this
# pass; frames 0/1 read fine as a generic walk cycle regardless of facing.
GHOST_CELL = 14
GHOST_ROWS = [("blinky", 41), ("clyde", 55), ("inky", 69), ("pinky", 83)]
for name, y in GHOST_ROWS:
    for frame in (0, 1):
        box = (frame * GHOST_CELL, y, frame * GHOST_CELL + GHOST_CELL, y + GHOST_CELL)
        crop = sheet.crop(box).resize((GHOST_CELL * SCALE, GHOST_CELL * SCALE), Image.NEAREST)
        save_sprite(f"ghost_{name}{frame}", crop)

# Frightened (power-pellet) ghost look — line0, cells 10/11 (blue/pink
# flashing warning frames), y=0, same 14x14 cell size as the ghosts above.
for idx, tag in ((10, "blue"), (11, "pink")):
    box = (idx * GHOST_CELL, 0, idx * GHOST_CELL + GHOST_CELL, GHOST_CELL)
    crop = sheet.crop(box).resize((GHOST_CELL * SCALE, GHOST_CELL * SCALE), Image.NEAREST)
    save_sprite(f"ghost_scared_{tag}", crop)

# Bonus fruit — line0, cell 0 (the first of 10 fruit frames; the classic
# cherry, confirmed by visual inspection) — same line/cell size as the
# scared-ghost frames above.
box = (0, 0, GHOST_CELL, GHOST_CELL)
crop = sheet.crop(box).resize((GHOST_CELL * SCALE, GHOST_CELL * SCALE), Image.NEAREST)
save_sprite("fruit_cherry", crop)

# ============================================================== EXPORT =====
palette_rgb = [(0, 0, 0)]
color_to_index = {}


# R/B swapped before packing — same real-panel-only color mismatch this
# session already found and worked around for the menu's own sprites
# (make_menu_pr32_sprites.py): a software screenshot of the internal
# buffer looks correct either way, but the actual physical panel shows
# chromatic colors with red/blue swapped specifically for indexed
# Sprite4bpp draws. Bubble Bobble's own separate copy of this helper is
# deliberately left un-swapped since its colors are confirmed correct on
# hardware.
def to_rgb565(r, g, b):
    return ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3)


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
header_lines.append("// AUTO-GENERATED by tools/make_pacman_pr32_sprites.py — do not hand-edit.")
header_lines.append("// Pac-Man walk-cycle frames, packed 4bpp indexed for PixelRoot32.")
header_lines.append("// Cropped from sgothel/pacman (MIT; sheet itself credited to")
header_lines.append("// spriters-resource.com, assumed public domain — see that repo's")
header_lines.append("// media/tiles_all.txt).")
header_lines.append("#pragma once")
header_lines.append("#include <cstdint>")
header_lines.append("#include <graphics/Renderer.h>")
header_lines.append("")
header_lines.append("namespace pacman_pr32_sprites {")
header_lines.append("")

sprite_blocks = []
for name, (w, h, img) in sprites.items():
    data, row_stride = pack_4bpp(img)
    const_name = f"PM_{name.upper()}"
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    header_lines.append(f"static const uint8_t {const_name}_DATA[] = {{ {hex_bytes} }};")
    sprite_blocks.append((const_name, w, h))
header_lines.append("")

# Reserved explicitly (not just whatever happened to fall out of the
# sprite crops) — Renderer PRIMITIVE draws (drawFilledRectangle/
# drawFilledCircle for pellets) resolve named Color:: constants through
# whichever CUSTOM sprite palette is currently active, not a fixed
# built-in table, so "Color::Yellow" while this palette is loaded means
# "whatever ended up in slot 8", not real yellow — confirmed live via a
# screenshot showing pellets as pale lavender instead of yellow. Pack a
# real yellow here and export its actual index as a named Color constant
# so callers don't have to guess/rely on the shared engine enum.
PELLET_YELLOW_INDEX = palette_index_for((255, 255, 0))

while len(palette_rgb) < 16:
    palette_rgb.append((0, 0, 0))

header_lines.append("static const uint16_t PALETTE_RGB565[16] = {")
header_lines.append("    " + ", ".join(f"0x{to_rgb565(r, g, b):04X}" for (r, g, b) in palette_rgb))
header_lines.append("};")
header_lines.append("")
header_lines.append("static const pixelroot32::graphics::Color PALETTE_COLORS[16] = {")
header_lines.append("    " + ", ".join(f"static_cast<pixelroot32::graphics::Color>({i})" for i in range(16)))
header_lines.append("};")
header_lines.append("")
header_lines.append(
    "// Use this for any Renderer PRIMITIVE draw (drawFilledRectangle/"
    "drawFilledCircle/etc.) that needs real yellow while this scene's custom"
    " sprite palette is active — see PELLET_YELLOW_INDEX's comment above."
)
header_lines.append(
    f"static constexpr pixelroot32::graphics::Color PELLET_YELLOW = "
    f"static_cast<pixelroot32::graphics::Color>({PELLET_YELLOW_INDEX});"
)
header_lines.append("")

for const_name, w, h in sprite_blocks:
    header_lines.append(
        f"static const pixelroot32::graphics::Sprite4bpp {const_name} = "
        f"{{ {const_name}_DATA, PALETTE_COLORS, {w}, {h}, 16 }};"
    )
header_lines.append("")
header_lines.append("} // namespace pacman_pr32_sprites")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines) + "\n")

print("Generated", len(sprites), "sprites,", len(color_to_index), "colors used (of 15 available)")
for name, (w, h, _) in sprites.items():
    print(f"  {name}: {w}x{h}")
