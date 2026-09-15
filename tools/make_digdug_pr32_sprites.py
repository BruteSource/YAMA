"""
Dig-Dug-style tunneling game sprite set for PixelRoot32 (7th game — see
~/.claude/plans/goofy-dancing-locket.md). Reference used only for
GENERIC mechanics (grid digging, pump-inflate-to-burst, falling rocks) —
https://github.com/l-hackari/digdug, a C/Allegro remake that directly
uses real Namco IP (title "Dig Dug", enemy names "Pooka"/"Fygar", with
no license file). None of that repo's art or code is reused here — all
sprites below are freshly authored flat-shaded shapes for this project's
own palette-indexed Sprite4bpp pipeline, same technique as every other
_pr32 sprite script (see make_rtype_pr32_sprites.py's docstring for the
full rationale).

IP note: the menu title stays "DIG DUG" (user's explicit call — most of
this project's other menu entries keep their real arcade titles too),
but the enemy characters get original names/designs, not "Pooka"/
"Fygar" — the round ground-crawler is called "Grub" and the fire-
breathing type is called "Drake" throughout this project's code.

Same packed-4bpp/shared-palette export pipeline as every other _pr32
sprite script (see make_arkanoid_pr32_boss.py's docstring for why).
"""
import os
from PIL import Image, ImageDraw

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "digdug_pr32")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "digdug_pr32_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

ALPHA_THRESHOLD = 128
sprites = {}  # name -> (w, h, PIL.Image RGBA)


def save_sprite(name, img):
    sprites[name] = (img.width, img.height, img)
    img.save(f"{OUT_DIR}/{name}.png")


# ============================================================ PALETTE ======
# One shared 16-slot table (15 usable + transparent) across the WHOLE
# roster, same budget/reuse discipline every other _pr32 sprite script
# works under. Lands at exactly 15/15 used.
BLACK        = (24, 22, 28)      # outlines, pupils, cracks
DIRT_LIGHT   = (150, 96, 58)     # upper dirt band fill
DIRT_LIGHT_DK= (110, 68, 40)     # upper dirt band shade/texture flecks
DIRT_DEEP    = (120, 60, 70)     # deeper dirt band fill
DIRT_DEEP_DK = (85, 40, 50)      # deeper dirt band shade/texture flecks
ROCK_GRAY    = (150, 150, 158)   # rock fill
ROCK_GRAY_DK = (95, 95, 104)     # rock shade/cracks
SUIT_BLUE    = (60, 150, 220)    # player suit
SUIT_BLUE_DK = (35, 100, 165)    # player suit shade
ACCENT_YEL   = (240, 200, 60)    # player helmet/belt, burst spark
GRUB_GREEN   = (90, 200, 110)    # grub body
GRUB_GREEN_DK= (55, 150, 75)     # grub body shade
DRAKE_PURPLE = (190, 90, 210)    # drake body
DRAKE_PURPLE_DK=(135, 55, 155)   # drake body shade
WHITE        = (232, 238, 248)   # eyes/highlights


def rect(d, x0, y0, x1, y1, fill):
    d.rectangle([x0, y0, x1, y1], fill=(*fill, 255))


def ell(d, x0, y0, x1, y1, fill):
    d.ellipse([x0, y0, x1, y1], fill=(*fill, 255))


def poly(d, pts, fill):
    d.polygon(pts, fill=(*fill, 255))


# =========================================================== DIRT TILES ====
def make_dirt_tile(name, fill, fill_dk):
    W = H = 16
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rect(d, 0, 0, W - 1, H - 1, fill)
    # A few texture flecks, deterministic (no rand — keep it reproducible)
    for (x, y) in [(2, 3), (11, 2), (6, 7), (13, 10), (2, 12), (9, 13)]:
        rect(d, x, y, x + 1, y + 1, fill_dk)
    save_sprite(name, img)


make_dirt_tile("dirt_a", DIRT_LIGHT, DIRT_LIGHT_DK)
make_dirt_tile("dirt_b", DIRT_DEEP, DIRT_DEEP_DK)

# =============================================================== ROCK ======
def make_rock():
    W = H = 14
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    poly(d, [(1, 9), (3, 2), (10, 1), (13, 7), (10, 13), (3, 13)], ROCK_GRAY)
    poly(d, [(3, 13), (10, 13), (13, 7), (9, 9), (5, 12)], ROCK_GRAY_DK)
    d.line([(4, 5), (7, 8)], fill=(*BLACK, 255), width=1)
    d.line([(9, 4), (7, 8)], fill=(*BLACK, 255), width=1)
    save_sprite("rock", img)


make_rock()

# ============================================================== PLAYER =====
def make_player():
    W, H = 14, 14
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Legs
    rect(d, 3, 10, 5, 13, SUIT_BLUE_DK)
    rect(d, 8, 10, 10, 13, SUIT_BLUE_DK)
    # Body
    d.rounded_rectangle([2, 5, 11, 11], radius=2, fill=(*SUIT_BLUE, 255))
    rect(d, 5, 7, 8, 10, SUIT_BLUE_DK)
    # Belt
    rect(d, 2, 9, 11, 10, ACCENT_YEL)
    # Helmet/head
    ell(d, 3, 0, 10, 7, ACCENT_YEL)
    ell(d, 4, 1, 9, 6, WHITE)
    ell(d, 5, 3, 7, 5, BLACK)
    save_sprite("player", img)


make_player()

# ======================================================= INFLATE SPRITES ===
def make_inflatable(prefix, body, body_dk, horns):
    # 3 inflate stages: small (normal) -> medium -> large (about to burst).
    for stage, size in enumerate((12, 17, 22)):
        W = H = size
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        pad = 1
        ell(d, pad, pad, W - 1 - pad, H - 1 - pad, body_dk)
        ell(d, pad + 1, pad + 1, W - 2 - pad, H - 3 - pad, body)
        if horns:
            hs = max(2, size // 8)
            poly(d, [(W * 0.28, pad + 1), (W * 0.22, 0), (W * 0.36, pad + 2)], body_dk)
            poly(d, [(W * 0.72, pad + 1), (W * 0.78, 0), (W * 0.64, pad + 2)], body_dk)
        # Eyes
        ex = W * 0.32
        ey = H * 0.38
        er = max(2, size // 7)
        ell(d, ex - er, ey - er, ex + er, ey + er, WHITE)
        ell(d, ex - er * 0.5, ey - er * 0.5, ex + er * 0.5, ey + er * 0.5, BLACK)
        ex2 = W * 0.68
        ell(d, ex2 - er, ey - er, ex2 + er, ey + er, WHITE)
        ell(d, ex2 - er * 0.5, ey - er * 0.5, ex2 + er * 0.5, ey + er * 0.5, BLACK)
        save_sprite(f"{prefix}_{stage}", img)


make_inflatable("grub", GRUB_GREEN, GRUB_GREEN_DK, horns=False)
make_inflatable("drake", DRAKE_PURPLE, DRAKE_PURPLE_DK, horns=True)

# =============================================================== BURST =====
def make_burst():
    W = H = 16
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = cy = W / 2
    for ang_deg in range(0, 360, 45):
        import math
        a = math.radians(ang_deg)
        x1, y1 = cx + math.cos(a) * 2, cy + math.sin(a) * 2
        x2, y2 = cx + math.cos(a) * 7, cy + math.sin(a) * 7
        d.line([(x1, y1), (x2, y2)], fill=(*ACCENT_YEL, 255), width=2)
    ell(d, cx - 2, cy - 2, cx + 2, cy + 2, WHITE)
    save_sprite("burst", img)


make_burst()

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
header_lines.append("// AUTO-GENERATED by tools/make_digdug_pr32_sprites.py — do not hand-edit.")
header_lines.append("// Original flat-shaded tunneling-game sprite set, packed 4bpp indexed")
header_lines.append("// for PixelRoot32's Sprite4bpp (see this script's own docstring).")
header_lines.append("#pragma once")
header_lines.append("#include <cstdint>")
header_lines.append("#include <graphics/Renderer.h>")
header_lines.append("")
header_lines.append("namespace digdug_pr32_sprites {")
header_lines.append("")

sprite_blocks = []
for name, (w, h, img) in sprites.items():
    data = pack_4bpp(img)
    const_name = f"DD_{name.upper()}"
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
header_lines.append("} // namespace digdug_pr32_sprites")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines) + "\n")

print(f"Generated {len(sprites)} sprites, {len(color_to_index)} colors used (of 15 available)")
for name, (w, h, _) in sprites.items():
    print(f"  {name}: {w}x{h}")
