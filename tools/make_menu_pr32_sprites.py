"""
Exports the arcade menu's background + logo as packed 4bpp indexed
bitmaps for PixelRoot32 (see tools/make_bubblebobble_pr32_sprites.py,
whose crop->pack->export pipeline this copies exactly — only the source
assets and output names differ).

Background: a small (48x48) SEAMLESSLY TILEABLE pattern generated locally
with PIL, in the spirit of the iconic "Jazz"/Memphis-style 90s paper-cup
pattern (purple/teal squiggles, triangles, moons on cream) the user asked
for by description — deliberately NOT a copy of that actual (likely
trademarked) commercial design, just the same visual vocabulary of shapes/
colors, redrawn from scratch. Every motif is placed with margin so nothing
crosses a tile edge, which makes the tile trivially seamless without any
cross-boundary blending math. MenuScene draws this tile repeated across
the screen with a scrolling offset for an "infinite scroll" background
(see menu_pr32.cpp) — small tile size keeps that per-frame re-tiling cheap.
Logo: generated locally with PIL (no external asset), quantized down to
just 2 colors (fill + outline) so it costs almost nothing against the
shared palette after the background tile's own colors.

Same packed format as every other sprite in this project (row-major,
1 byte/2px, low nibble = even column, index 0 = transparent) and same
one-shared-16-slot-palette approach (see the Bubble Bobble script's own
docstring for why) — MenuScene::init() sets this as the active custom
SPRITE palette (mirroring BubbleBobbleScene::init()'s exact idiom) since
it's a completely separate game/menu palette from Bubble Bobble's.
"""
import os
import colorsys
from PIL import Image, ImageDraw, ImageFont, ImageChops

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "menu_pr32")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "menu_pr32_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

# Shared 6-step ROYGBV rainbow — defined once here (not per-section) so
# the panel border's swatch tiles (below) and the logo's fill (further
# down) reuse the EXACT same RGB tuples, which dedupes them to the same
# palette slots instead of spending new ones (this palette sits at its
# 15/15 slot cap already; any accidental near-duplicate would overflow).
RAINBOW_HUES = [0, 36, 72, 140, 210, 270]  # red, orange, yellow, green, blue, violet
RAINBOW_COLORS = [
    tuple(int(c * 255) for c in colorsys.hsv_to_rgb(h / 360.0, 0.85, 1.0))
    for h in RAINBOW_HUES
]

ALPHA_THRESHOLD = 128
SCREEN_W = 240
SCREEN_H = 320

sprites = {}  # name -> (w, h, PIL.Image RGBA)


def save_sprite(name, img):
    sprites[name] = (img.width, img.height, img)
    img.save(f"{OUT_DIR}/{name}.png")


# ========================================================== BACKGROUND =====
# Removed for now (plain Color::Black fill drawn directly in
# menu_pr32.cpp instead — a neutral color, safe through either palette
# context, no sprite needed) — was the stippled diagonal tile pattern;
# see git history if reviving it. Dropping it also freed 3 palette slots
# (white/teal/purple) now spent on the animated logo's frames instead.

# ============================================================= SWATCHES ====
# Small solid-color 8x8 tiles for the menu's panel border/interior and
# selection highlight — drawn via drawSprite() (tiled to fill a rectangle,
# see menu_pr32.cpp) instead of Renderer's plain Color-enum primitives
# (drawFilledRectangle(..., Color::Cyan) etc). Those primitives resolve
# through the engine's shared DEFAULT palette (same table Bubble Bobble
# uses), which is off-limits to touch here (Bubble Bobble's colors are
# already confirmed correct) — going through this script's own
# (R/B-pre-swap-compensated) palette instead means these UI colors can be
# fixed without touching anything shared.
for name, rgb in [
    ("swatch_cyan", (0, 194, 255)),
    ("swatch_orange", (255, 159, 28)),
    ("swatch_navy", (20, 24, 46)),
    ("swatch_red", (222, 42, 42)),      # pleasing primary red/green/blue for
    ("swatch_green", (46, 196, 92)),    # the color-bar background phase --
    ("swatch_blue", (56, 118, 230)),    # not neon/harsh, still clearly RGB
]:
    save_sprite(name, Image.new("RGBA", (8, 8), (*rgb, 255)))

# One 8x8 swatch per rainbow step, reusing RAINBOW_COLORS' exact tuples
# (see its comment above) — lets the game-select panel's border animate
# through the same rainbow as the logo (menu_pr32.cpp draws it tile-by-
# tile around the perimeter, picking a swatch per tile from its position
# + the current logo animation phase) at zero extra palette cost.
for i, rgb in enumerate(RAINBOW_COLORS):
    save_sprite(f"swatch_rainbow{i}", Image.new("RGBA", (8, 8), (*rgb, 255)))

# ============================================================= CHEVRONS ====
# Small up/down scroll-hint arrows for the game list (see menu_pr32.cpp).
# Drawn as real sprite bitmaps with a baked-in palette color, NOT
# Color::Yellow through drawText/drawFilledRectangle -- that was tried
# first and reported showing up BLUE on real hardware (matching the
# panel border's cyan, not yellow) once this menu's custom sprite
# palette is active. Whatever's actually happening internally there, a
# pre-baked sprite is the one color mechanism confirmed correct on real
# hardware all session (see swatch_cyan/orange/navy above), so that's
# what this uses instead of chasing the Color-enum path further.
CHEVRON_W, CHEVRON_H = 14, 8
CHEVRON_YELLOW = (255, 216, 40)
CHEVRON_OUTLINE = (20, 20, 20)  # same tuple the logo's OUTLINE uses -- dedupes to the same palette slot, no new color spent


def make_chevron(pointing_up):
    img = Image.new("RGBA", (CHEVRON_W, CHEVRON_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if pointing_up:
        pts = [(CHEVRON_W / 2, 0), (CHEVRON_W - 1, CHEVRON_H - 1), (0, CHEVRON_H - 1)]
    else:
        pts = [(0, 0), (CHEVRON_W - 1, 0), (CHEVRON_W / 2, CHEVRON_H - 1)]
    d.polygon(pts, fill=(*CHEVRON_YELLOW, 255), outline=(*CHEVRON_OUTLINE, 255))
    return img


save_sprite("chevron_up", make_chevron(True))
save_sprite("chevron_down", make_chevron(False))

# =============================================================== LOGO ======
# Generated locally (no external asset). Press Start 2P (Google Fonts,
# SIL OFL 1.1, see tools/fonts/OFL.txt) -- the canonical 8-bit arcade
# pixel font, whose blocky letterforms read as crisp and intentional at
# this resolution -- kept from the earlier solid-purple version. This
# pass replaces the flat fill with a left-to-right rainbow: instead of
# one FILL color, each fill pixel's color is chosen by its OWN x
# position (not the whole logo's average), quantized to the nearest of
# a fixed 6-color ROYGBV sweep -- a real per-pixel gradient, not a
# uniform tint. Composited by DIRECT layering (paint shadow, then
# outline over it, then the rainbow fill on top) rather than PIL's alpha
# blending + nearest-of-3-colors snap the old solid-color version used --
# that snap-to-nearest trick stops being reliable once the fill target
# itself varies pixel-by-pixel (an anti-aliased edge pixel could
# ambiguously snap to a rainbow color from an unrelated x position
# instead of the outline/shadow it was actually blended with), so this
# version decides each pixel's category from three separate coverage
# masks instead of guessing from blended RGB values.
LOGO_W, LOGO_H = 220, 32
OUTLINE = (20, 20, 20)
SHADOW = (10, 6, 16)
text = "ESP32 ARCADE"
font_size = LOGO_W // len(text)
font = ImageFont.truetype(os.path.join(SCRIPT_DIR, "fonts", "PressStart2P-Regular.ttf"), font_size)

bbox_probe = ImageDraw.Draw(Image.new("RGBA", (1, 1))).textbbox((0, 0), text, font=font)
tw, th = bbox_probe[2] - bbox_probe[0], bbox_probe[3] - bbox_probe[1]
tx = (LOGO_W - tw) // 2 - bbox_probe[0]
ty = (LOGO_H - th) // 2 - bbox_probe[1]


def render_mask(dx, dy):
    """White text at the given offset -> alpha channel is a coverage mask."""
    m = Image.new("RGBA", (LOGO_W, LOGO_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(m)
    d.text((tx + dx, ty + dy), text, font=font, fill=(255, 255, 255, 255))
    return m.split()[3]  # alpha channel


fill_mask = render_mask(0, 0)
shadow_mask = render_mask(2, 2)
outline_mask = Image.new("L", (LOGO_W, LOGO_H), 0)
for ox in (-1, 0, 1):
    for oy in (-1, 0, 1):
        if ox or oy:
            outline_mask = ImageChops.lighter(outline_mask, render_mask(ox, oy))

# 6-step ROYGBV rainbow, one solid color per step -- NOT a continuous
# hue sweep (would blow the shared 16-slot palette) but wide, evenly
# spaced bands. Animated by generating N frames, each with the band
# boundaries shifted along x by a phase offset -- same 6 output colors
# in every frame (palette cost stays fixed regardless of frame count),
# but the boundaries sliding frame-to-frame reads as a smooth flowing
# sweep once cycled at runtime, the same principle as a scrolling LED
# marquee. Phase advances a full 360 degrees over N_FRAMES, so frame N
# lands back exactly on frame 0's phase -- a seamlessly looping cycle,
# not just N independent images.
# RAINBOW_HUES/RAINBOW_COLORS now defined once near the top of this
# script (shared with the border swatches above).
SWEEP_SPAN_DEG = 300.0  # matches the original hues' 0-270 spread, plus one step's gap
N_FRAMES = 16


def nearest_rainbow_color(hue):
    def circular_dist(a, b):
        d = abs(a - b) % 360.0
        return min(d, 360.0 - d)
    best_i = min(range(len(RAINBOW_HUES)), key=lambda i: circular_dist(hue, RAINBOW_HUES[i]))
    return RAINBOW_COLORS[best_i]


def rainbow_color_for_x(x, phase_deg):
    frac = max(0.0, min(1.0, x / (LOGO_W - 1)))
    hue = (frac * SWEEP_SPAN_DEG + phase_deg) % 360.0
    return nearest_rainbow_color(hue)


fpx, spx, opx = fill_mask.load(), shadow_mask.load(), outline_mask.load()
logo_frame_names = []
for frame in range(N_FRAMES):
    phase_deg = frame * (360.0 / N_FRAMES)
    logo = Image.new("RGBA", (LOGO_W, LOGO_H), (0, 0, 0, 0))
    lpx = logo.load()
    for y in range(LOGO_H):
        for x in range(LOGO_W):
            if fpx[x, y] >= 128:
                lpx[x, y] = (*rainbow_color_for_x(x, phase_deg), 255)
            elif opx[x, y] >= 128:
                lpx[x, y] = (*OUTLINE, 255)
            elif spx[x, y] >= 128:
                lpx[x, y] = (*SHADOW, 255)
    name = f"logo_f{frame:02d}"
    save_sprite(name, logo)
    logo_frame_names.append(name)

# ============================================================== EXPORT =====
palette_rgb = [(0, 0, 0)]  # slot 0 placeholder (never drawn -- alpha 0)
color_to_index = {}


def to_rgb565(r, g, b):
    # R/B swapped here (only in THIS script's copy of this helper — Bubble
    # Bobble's identical-looking to_rgb565() in
    # make_bubblebobble_pr32_sprites.py is untouched and its colors are
    # confirmed correct on hardware, so this is NOT a global display/panel
    # fix, deliberately). The menu's own sprite colors (background tile,
    # logo) came out with red and blue visibly swapped on the real screen
    # (confirmed by the user against the actual physical panel, not just a
    # photo/lighting artifact) even though a fresh-boot software screenshot
    # of the exact same framebuffer looked correct — i.e. whatever's
    # different lives in the real SPI pixel-push path, not in how these
    # RGB565 values are computed or read back for screenshots. Rather than
    # touch TFT_eSPI's global color-order config (which would also change
    # Bubble Bobble's already-correct colors), pre-swap R/B here so this
    # script's OWN output ends up looking right after that same real-panel
    # transform is applied. If a future full test-pattern check shows the
    # real transform isn't a plain R<->B swap, revisit this.
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
header_lines.append("// AUTO-GENERATED by tools/make_menu_pr32_sprites.py — do not hand-edit.")
header_lines.append("// UI swatches + an animated rainbow logo (N_FRAMES phase-shifted bitmaps,")
header_lines.append("// cycled at runtime by menu_pr32.cpp), packed 4bpp indexed for Sprite4bpp.")
header_lines.append("#pragma once")
header_lines.append("#include <cstdint>")
header_lines.append("#include <graphics/Renderer.h>")
header_lines.append("")
header_lines.append("namespace menu_pr32_sprites {")
header_lines.append("")

sprite_blocks = []
for name, (w, h, img) in sprites.items():
    data, row_stride = pack_4bpp(img)
    const_name = f"MENU_{name.upper()}"
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    header_lines.append(f"static const uint8_t {const_name}_DATA[] = {{ {hex_bytes} }};")
    sprite_blocks.append((const_name, w, h))
header_lines.append("")

while len(palette_rgb) < 16:
    palette_rgb.append((0, 0, 0))

header_lines.append("// Shared master palette across every sprite this script exports.")
header_lines.append("static const uint16_t PALETTE_RGB565[16] = {")
header_lines.append("    " + ", ".join(f"0x{to_rgb565(r, g, b):04X}" for (r, g, b) in palette_rgb))
header_lines.append("};")
header_lines.append("")
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

header_lines.append(f"static const int LOGO_FRAME_COUNT = {len(logo_frame_names)};")
header_lines.append("static const pixelroot32::graphics::Sprite4bpp* const LOGO_FRAMES[] = {")
frame_refs = ", ".join(f"&MENU_{n.upper()}" for n in logo_frame_names)
header_lines.append(f"    {frame_refs}")
header_lines.append("};")
header_lines.append("")
header_lines.append("} // namespace menu_pr32_sprites")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines) + "\n")

print("Generated", len(sprites), "sprites,", len(color_to_index), "colors used (of 15 available)")
for name, (w, h, _) in sprites.items():
    print(f"  {name}: {w}x{h}")
for i, rgb in enumerate(palette_rgb):
    print(f"  slot {i}: {rgb}")
