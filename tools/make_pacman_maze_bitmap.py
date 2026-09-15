"""
Renders the Pac-Man maze WALLS as a single pre-baked bitmap, replacing
the old per-tile flat-rectangle rendering in PacManScene::drawMaze()
with something that actually looks like the real arcade maze: rounded
convex corners and a two-tone (bright outline + dark navy fill) "tube"
look, matching a reference screenshot the user supplied.

This does NOT change the maze's actual tile topology/collision data —
MAZE[] below is a straight copy of the same array in pacman_pr32.cpp
(the single source of truth for gameplay) and MUST be kept in sync by
hand if that array ever changes; this script only produces artwork.

Technique (all at 8x supersample, then downscaled with LANCZOS so the
rounded corners/edges are informed by high-res antialiasing before being
flattened to a small hard-edged palette — same idea used for the menu
logo earlier this session, just applied to shapes instead of text):
  1. Build a binary "outer" mask: union of every '#' tile's full 8x8
     square, then round each tile's OWN corner wherever neither
     orthogonal neighbor is also a wall (a true convex/exterior corner —
     an inner L/T junction stays square, matching the real maze).
  2. Erode that mask (PIL MinFilter) to get an "inner" mask — the erosion
     band becomes the bright outline, the eroded interior becomes the
     dark navy fill.
  3. Downscale both masks to the real render resolution and threshold,
     then flatten to 3 flat colors (navy fill, bright outline, black).
  4. Bake the ghost-house gate ('-') as a distinct pink bar on top,
     same look as the old per-frame draw call it replaces.
Exported as one big Sprite4bpp (224x248, matching the playfield exactly)
so PacManScene::drawMaze() becomes a single drawSprite() call.
"""
import os
from PIL import Image, ImageDraw, ImageFilter

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "pacman_pr32_maze_bitmap.h")
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "pacman_pr32")
os.makedirs(OUT_DIR, exist_ok=True)

# Must match pacman_pr32.cpp's MAZE[] exactly.
MAZE = [
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
]
ROWS = len(MAZE)
COLS = 28
TILE = 8
SUPER = 8
TPX = TILE * SUPER  # 64

# Colors sampled directly from the user's reference screenshot.
OUTER_BLUE = (36, 36, 255)
INNER_NAVY = (0, 0, 90)
GATE_PINK = (255, 109, 109)


def is_wall(r, c):
    if r < 0 or r >= ROWS or c < 0 or c >= COLS:
        return False
    return MAZE[r][c] == '#'


def build_outer_mask():
    W, H = COLS * TPX, ROWS * TPX
    img = Image.new("L", (W, H), 0)
    draw = ImageDraw.Draw(img)

    # Pass 1: union of full tile squares.
    for r in range(ROWS):
        for c in range(COLS):
            if MAZE[r][c] != '#':
                continue
            x0, y0 = c * TPX, r * TPX
            draw.rectangle([x0, y0, x0 + TPX - 1, y0 + TPX - 1], fill=255)

    # Pass 2: round true convex corners (neither orthogonal neighbor is
    # a wall) — done after all base squares are drawn so a cut can't be
    # refilled by a later neighboring tile's rectangle.
    R = int(TPX * 0.42)
    for r in range(ROWS):
        for c in range(COLS):
            if MAZE[r][c] != '#':
                continue
            x0, y0 = c * TPX, r * TPX
            x1, y1 = x0 + TPX, y0 + TPX
            up, down = is_wall(r - 1, c), is_wall(r + 1, c)
            left, right = is_wall(r, c - 1), is_wall(r, c + 1)

            if not up and not left:  # top-left
                draw.rectangle([x0, y0, x0 + R, y0 + R], fill=0)
                draw.ellipse([x0, y0, x0 + 2 * R, y0 + 2 * R], fill=255)
            if not up and not right:  # top-right
                draw.rectangle([x1 - R, y0, x1, y0 + R], fill=0)
                draw.ellipse([x1 - 2 * R, y0, x1, y0 + 2 * R], fill=255)
            if not down and not left:  # bottom-left
                draw.rectangle([x0, y1 - R, x0 + R, y1], fill=0)
                draw.ellipse([x0, y1 - 2 * R, x0 + 2 * R, y1], fill=255)
            if not down and not right:  # bottom-right
                draw.rectangle([x1 - R, y1 - R, x1, y1], fill=0)
                draw.ellipse([x1 - 2 * R, y1 - 2 * R, x1, y1], fill=255)
    return img


def main():
    outer = build_outer_mask()
    # Erode for the inner (navy) mask — the erosion band left behind is
    # the bright outline. Kernel size chosen so the final downscaled
    # outline is ~1.5-2px thick.
    erode_px = int(TPX * 0.22)
    k = erode_px * 2 + 1
    inner = outer.filter(ImageFilter.MinFilter(k))

    final_w, final_h = COLS * TILE, ROWS * TILE
    outer_small = outer.resize((final_w, final_h), Image.LANCZOS).point(lambda p: 255 if p >= 128 else 0)
    inner_small = inner.resize((final_w, final_h), Image.LANCZOS).point(lambda p: 255 if p >= 128 else 0)

    img = Image.new("RGBA", (final_w, final_h), (0, 0, 0, 255))
    px = img.load()
    op = outer_small.load()
    ip = inner_small.load()
    for y in range(final_h):
        for x in range(final_w):
            if ip[x, y] >= 128:
                px[x, y] = (*INNER_NAVY, 255)
            elif op[x, y] >= 128:
                px[x, y] = (*OUTER_BLUE, 255)

    # Bake the ghost-house gate on top, same look/position as the old
    # per-frame draw call it replaces (a thin bar across the doorway).
    draw = ImageDraw.Draw(img)
    for r in range(ROWS):
        for c in range(COLS):
            if MAZE[r][c] == '-':
                x0, y0 = c * TILE, r * TILE
                draw.rectangle([x0, y0 + TILE // 2 - 1, x0 + TILE - 1, y0 + TILE // 2], fill=(*GATE_PINK, 255))

    img.save(f"{OUT_DIR}/maze_bg.png")

    # ---- export as a single Sprite4bpp, same pack_4bpp convention as
    # the other make_pacman_pr32_*.py scripts ----
    ALPHA_THRESHOLD = 128
    palette_rgb = [(0, 0, 0)]
    color_to_index = {(0, 0, 0): 0}

    def to_rgb565(r, g, b):
        return ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3)

    def palette_index_for(rgb):
        if rgb not in color_to_index:
            if len(palette_rgb) >= 16:
                raise ValueError(f"Palette overflow: {rgb} would be the 17th color")
            color_to_index[rgb] = len(palette_rgb)
            palette_rgb.append(rgb)
        return color_to_index[rgb]

    w, h = img.size
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

    while len(palette_rgb) < 16:
        palette_rgb.append((0, 0, 0))

    lines = []
    lines.append("// AUTO-GENERATED by tools/make_pacman_maze_bitmap.py — do not hand-edit.")
    lines.append("// Pre-rendered maze wall bitmap (rounded, two-tone) replacing the old")
    lines.append("// per-tile flat-rectangle drawMaze(). MAZE[] topology is unchanged —")
    lines.append("// this is art only. Has its OWN 16-color palette (separate from")
    lines.append("// pacman_pr32_sprites.h's) since baking it into that one would blow the")
    lines.append("// shared budget; PacManScene applies this palette only while drawing")
    lines.append("// the maze background, then restores the sprite palette.")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("#include <graphics/Renderer.h>")
    lines.append("")
    lines.append("namespace pacman_pr32_maze {")
    lines.append("")
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    lines.append(f"static const uint8_t MAZE_BG_DATA[] = {{ {hex_bytes} }};")
    lines.append("")
    lines.append("static const uint16_t PALETTE_RGB565[16] = {")
    lines.append("    " + ", ".join(f"0x{to_rgb565(r, g, b):04X}" for (r, g, b) in palette_rgb))
    lines.append("};")
    lines.append("")
    lines.append("static const pixelroot32::graphics::Color PALETTE_COLORS[16] = {")
    lines.append("    " + ", ".join(f"static_cast<pixelroot32::graphics::Color>({i})" for i in range(16)))
    lines.append("};")
    lines.append("")
    lines.append(
        f"static const pixelroot32::graphics::Sprite4bpp MAZE_BG = "
        f"{{ MAZE_BG_DATA, PALETTE_COLORS, {w}, {h}, 16 }};"
    )
    lines.append("")
    lines.append("} // namespace pacman_pr32_maze")

    with open(HEADER_PATH, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Generated maze bitmap {w}x{h}, {len(color_to_index)} colors used (of 15 available)")


if __name__ == "__main__":
    main()
