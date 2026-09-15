"""
R-Type-style shmup sprite set for PixelRoot32, adapted from the dormant
pre-Scene-rebuild game (tools/make_rtype_sprites.py + src/rtype.cpp,
excluded from the build). Same shapes/silhouettes/color choices as that
original hand-drawn art (confirmed original work, not traced from real
R-Type sprites), but re-authored for a completely different rendering
constraint: the old version blitted raw RGB565 bitmaps directly
(tft->drawRGBBitmap()) with smooth box-filter-antialiased gradients —
PixelRoot32 only supports palette-indexed Sprite4bpp (one shared
16-color table per game, same as every other _pr32 game here), which
can't represent that kind of smooth per-pixel shading. So this is flat-
shaded at native resolution (2-3 tones per shape via ImageDraw
primitives directly, no 4x supersample/downscale pass) instead of
antialiased — a real technique change, not a redesign; same silhouettes
and hues throughout, aggressively reusing colors across the whole
roster to fit the 16-slot budget (see PALETTE comment below).

IP note: the dormant version named things after real Irem R-Type
property in comments/identifiers (ship modeled on "the real R-9," pod
called "Force," enemies described as "Bydo-style," boss "Guard Ray").
None of that naming is repeated here — this file only ever says "ship,"
"pod"/"drone," "drifter," "turret," "debris." The boss gets its own
separate original design in make_rtype_pr32_boss.py, not this file.

Same packed-4bpp/shared-palette export pipeline as every other _pr32
sprite script (see make_arkanoid_pr32_boss.py's docstring for why).
"""
import os
from PIL import Image, ImageDraw

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "rtype_pr32")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "rtype_pr32_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

ALPHA_THRESHOLD = 128
sprites = {}  # name -> (w, h, PIL.Image RGBA)


def save_sprite(name, img):
    sprites[name] = (img.width, img.height, img)
    img.save(f"{OUT_DIR}/{name}.png")


# ============================================================ PALETTE ======
# One shared 16-slot table (15 usable + transparent) across the WHOLE
# roster — aggressive reuse across shapes to fit the budget, same
# constraint every other _pr32 game's sprite script works under.
BLACK      = (24, 22, 28)       # outlines, pupils, rivets
WHITE      = (232, 238, 248)    # ship hull
WHITE_SHADE= (176, 188, 208)    # ship hull shade
BLUE       = (70, 155, 230)     # ship stripe, pod/drone body, canopy
BLUE_DARK  = (40, 105, 175)     # ship stripe shade, pod/drone rim
RED        = (222, 65, 75)      # ship fin tips
YELLOW     = (255, 205, 55)     # ship band, drifter eye, weak bullet, weapon capsule
PURPLE     = (175, 78, 185)     # drifter body
PURPLE_DK  = (122, 48, 132)     # drifter body shade
GRAY       = (112, 118, 136)    # turret/debris base
GRAY_DARK  = (72, 77, 94)       # turret/debris shade
PINK_RED   = (255, 92, 112)     # turret lens, enemy bullet
TEAL       = (140, 255, 210)    # charged bullet
TEAL_DARK  = (65, 195, 160)     # charged bullet shade
GREEN      = (75, 205, 130)     # speed capsule


def rect(d, x0, y0, x1, y1, fill):
    d.rectangle([x0, y0, x1, y1], fill=(*fill, 255))


def ell(d, x0, y0, x1, y1, fill):
    d.ellipse([x0, y0, x1, y1], fill=(*fill, 255))


def poly(d, pts, fill):
    d.polygon(pts, fill=(*fill, 255))


# =============================================================== SHIP ======
# Rotated 90 degrees from the dormant version's own landscape-authored
# art (same "points right" -> "points up" transform it already used —
# see save_sprite_rotated()'s comment in the old script) since this
# rebuild draws it upright to begin with, at native resolution.
def make_ship():
    W, H = 18, 32
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    midx = W // 2
    # Tail fins (bottom, since "forward" is up) — drawn first, hull overlaps roots
    poly(d, [(3, 29), (5, 20), (10, 27)], RED)
    poly(d, [(15, 29), (13, 20), (8, 27)], RED)
    poly(d, [(3, 29), (5, 20), (6, 21), (4, 28)], BLACK)
    poly(d, [(15, 29), (13, 20), (12, 21), (14, 28)], BLACK)
    # Tapered hull: wide at the tail, narrowing to the nose
    poly(d, [(4, 26), (14, 26), (13, 8), (5, 8)], WHITE)
    rect(d, 6, 18, 12, 25, WHITE_SHADE)
    # Nose
    poly(d, [(5, 8), (13, 8), (midx, 1)], WHITE)
    # Yellow joint band
    rect(d, 5, 7, 13, 9, YELLOW)
    # Blue racing stripe down the spine
    rect(d, midx - 1, 9, midx + 1, 24, BLUE)
    rect(d, midx - 1, 9, midx, 17, BLUE_DARK)
    # Canopy
    ell(d, midx - 3, 11, midx + 3, 17, BLUE)
    ell(d, midx - 2, 11, midx, 14, WHITE_SHADE)
    save_sprite("ship", img)


make_ship()


# ============================================================ POD/DRONE ====
def make_drone():
    W = H = 12
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ell(d, 0, 0, W - 1, H - 1, BLUE_DARK)
    ell(d, 1, 1, W - 2, H - 2, BLUE)
    ell(d, 3, 2, 6, 5, WHITE_SHADE)
    save_sprite("drone", img)


make_drone()


# ============================================================== DRIFTER ====
def make_drifter():
    W = H = 20
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Teardrop body (trailing tail + rounded head)
    ell(d, 1, 6, 12, 16, PURPLE_DK)
    ell(d, 6, 2, 19, 18, PURPLE)
    # Eye
    ell(d, 11, 5, 17, 11, BLACK)
    ell(d, 12, 6, 16, 10, YELLOW)
    ell(d, 13, 7, 15, 9, BLACK)
    save_sprite("drifter", img)


make_drifter()


# =============================================================== TURRET ====
def make_turret():
    W, H = 16, 12
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rect(d, 1, 6, 14, 11, GRAY_DARK)
    ell(d, 4, 1, 13, 11, GRAY)
    ell(d, 7, 4, 12, 9, GRAY_DARK)
    ell(d, 8, 5, 11, 8, PINK_RED)
    save_sprite("turret", img)


make_turret()


# =============================================================== BULLETS ===
def make_bullet_weak():
    W, H = 5, 10
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ell(d, 0, 0, W - 1, H - 1, YELLOW)
    save_sprite("bullet_weak", img)


def make_bullet_charged():
    W, H = 7, 18
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=3, fill=(*TEAL_DARK, 255))
    d.rounded_rectangle([1, 2, W - 2, H - 3], radius=2, fill=(*TEAL, 255))
    save_sprite("bullet_charged", img)


def make_bullet_drone():
    W, H = 5, 7
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ell(d, 0, 0, W - 1, H - 1, BLUE)
    save_sprite("bullet_drone", img)


def make_enemy_bullet():
    W = H = 8
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    ell(d, 0, 0, W - 1, H - 1, BLACK)
    ell(d, 1, 1, W - 2, H - 2, PINK_RED)
    save_sprite("enemy_bullet", img)


make_bullet_weak()
make_bullet_charged()
make_bullet_drone()
make_enemy_bullet()

# =============================================================== CAPSULES ==
def make_capsule(name, body, body_dk, accent, arrow):
    W = H = 12
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=3, fill=(*body, 255))
    d.rounded_rectangle([1, 1, W - 2, 5], radius=2, fill=(*body_dk, 255))
    if arrow:
        poly(d, [(3, 3), (3, 9), (7, 6)], accent)
        poly(d, [(6, 3), (6, 9), (10, 6)], accent)
    else:
        poly(d, [(6, 2), (8, 5), (6, 4), (4, 5)], accent)
        ell(d, 4, 5, 8, 9, accent)
    save_sprite(name, img)


make_capsule("capsule_speed", GREEN, GRAY_DARK, WHITE, True)
make_capsule("capsule_weapon", YELLOW, GRAY_DARK, WHITE, False)

# ================================================================ DEBRIS ===
def make_debris(name, W, H, variant):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=2, fill=(*GRAY, 255))
    poly(d, [(0, 0), (int(W * 0.6), 0), (0, int(H * 0.6))], WHITE_SHADE)
    poly(d, [(W - 1, H - 1), (int(W * 0.4), H - 1), (W - 1, int(H * 0.4))], GRAY_DARK)
    if variant == 0:
        rect(d, 2, H // 2 - 1, W - 3, H // 2, GRAY_DARK)
    elif variant == 1:
        rect(d, W // 2 - 1, 2, W // 2, H - 3, GRAY_DARK)
    else:
        d.line([(2, 2), (W - 3, H - 3)], fill=(*GRAY_DARK, 255), width=2)
    for cx, cy in [(2, 2), (W - 3, 2), (2, H - 3), (W - 3, H - 3)]:
        ell(d, cx - 1, cy - 1, cx + 1, cy + 1, BLACK)
    save_sprite(name, img)


make_debris("debris_0", 24, 16, 0)
make_debris("debris_1", 20, 24, 1)
make_debris("debris_2", 22, 18, 2)

# ================================================================ EXPORT ===
palette_rgb = [(0, 0, 0)]
color_to_index = {}


def to_rgb565(r, g, b):
    # Same deliberate R/B pre-swap every custom _pr32 sprite palette in
    # this project uses to compensate the confirmed real-panel R/B swap
    # (see make_menu_pr32_sprites.py's to_rgb565 comment for the full
    # story) — NOT applied to the shared/default engine palette.
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
    return data


header_lines = []
header_lines.append("// AUTO-GENERATED by tools/make_rtype_pr32_sprites.py — do not hand-edit.")
header_lines.append("// Original flat-shaded shmup sprite set, packed 4bpp indexed for")
header_lines.append("// PixelRoot32's Sprite4bpp (see this script's own docstring).")
header_lines.append("#pragma once")
header_lines.append("#include <cstdint>")
header_lines.append("#include <graphics/Renderer.h>")
header_lines.append("")
header_lines.append("namespace rtype_pr32_sprites {")
header_lines.append("")

sprite_blocks = []
for name, (w, h, img) in sprites.items():
    data = pack_4bpp(img)
    const_name = f"RT_{name.upper()}"
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    header_lines.append(f"static const uint8_t {const_name}_DATA[] = {{ {hex_bytes} }};")
    sprite_blocks.append((const_name, w, h))
header_lines.append("")

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

for const_name, w, h in sprite_blocks:
    header_lines.append(
        f"static const pixelroot32::graphics::Sprite4bpp {const_name} = "
        f"{{ {const_name}_DATA, PALETTE_COLORS, {w}, {h}, 16 }};"
    )
header_lines.append("")
header_lines.append("} // namespace rtype_pr32_sprites")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines) + "\n")

print(f"Generated {len(sprites)} sprites, {len(color_to_index)} colors used (of 15 available)")
for name, (w, h, _) in sprites.items():
    print(f"  {name}: {w}x{h}")
