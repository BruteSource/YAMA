"""
Authors Bubble Bobble sprite bitmaps by CROPPING frames directly from the
reference sheets in tools/bb_ref_assets/ (see SOURCE.md there) instead of
hand-drawing approximations — done this way at the user's explicit request
after two hand-drawn passes still didn't read as accurate enough. Each
crop is converted to RGB565 and emitted as a C header, using the same
technique as every other game's sprites (see tools/make_rtype_sprites.py):
background pre-baked to this game's BG color so a plain
tft->drawRGBBitmap() blit reads as transparent, no per-pixel masking.

Frame coordinates (native pixels, top-left origin) — determined by
visually inspecting each sheet with pixel-grid overlays this session
(an initial guess at uniform 32x32 cells for both Enemys.png and
Items.png was wrong; both are actually 16x16 per icon, confirmed by
gridded crops before settling on these):
  BubbleCharacter.png (96x64, 4x4 grid of 24x16):
    (0,0)-(24,16)   walk/idle frame A
    (24,0)-(48,16)  walk/idle frame B (near-identical blink variant)
    (24,32)-(48,48) a distinct mouth-open expression, used for the jump
                    frame — Bub has no separate limb/jump art in this
                    sheet (it's a limbless blob, animated by expression
                    only), so this is picked for *some* visual variety
                    in the air rather than being a literal jump pose
  Enemys.png (256x192, 16x16 per icon) — Zen-Chan is the first enemy,
  light-blue color variant, top-left corner:
    (0,0)-(16,16)   walk frame A
    (16,0)-(32,16)  walk frame B
  AttackBubbleColored.png (112x32, 7x2 grid of 16x16) — capture-bubble
  projectile animation, row 0 = green (Bub's color):
    (48,0)-(64,16)  the hollow "formed/floating" ring frame
  Items.png (576x64, 16x16 per icon, tightly packed/variable-width, NOT
  a uniform grid — found by trial crops, not by column math):
    (304,0)-(322,16)  watermelon (18px wide)
    (400,0)-(416,16)  fries

Bub (24x16) and Zen-Chan (16x16) are both upscaled 2x uniformly (not
independently) so their *relative* native proportions (Bub wider than
Zen-Chan, matching the source) are preserved rather than flattened by
picking a different scale factor per sprite.

Left/right-facing versions are produced by mirroring each crop
(ImageOps.mirror) rather than sourcing separate frames, same as the
previous hand-drawn passes.
"""
import os
from PIL import Image, ImageOps

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REF_DIR = os.path.join(SCRIPT_DIR, "bb_ref_assets")
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "bubblebobble")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "bubblebobble_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

BG = (6, 8, 22)  # matches palette.cpp's BG, same background used by every game

sprites = {}

def save_sprite(name, img):
    sprites[name] = (img.width, img.height, img)
    img.save(f"{OUT_DIR}/{name}.png")

def crop(sheet_name, box, scale=1):
    img = Image.open(os.path.join(REF_DIR, sheet_name)).convert("RGBA")
    c = img.crop(box)
    if scale != 1:
        c = c.resize((c.width * scale, c.height * scale), Image.NEAREST)
    return c

# ============================================================= BUB =========
bub_walk0 = crop("BubbleCharacter.png", (0, 0, 24, 16), scale=2)
bub_walk1 = crop("BubbleCharacter.png", (24, 0, 48, 16), scale=2)
bub_jump = crop("BubbleCharacter.png", (24, 32, 48, 48), scale=2)
for name, img in [("bub_walk0", bub_walk0), ("bub_walk1", bub_walk1), ("bub_jump", bub_jump)]:
    save_sprite(f"{name}_r", img)
    save_sprite(f"{name}_l", ImageOps.mirror(img))

# ========================================================== ZEN-CHAN =======
zc_walk0 = crop("Enemys.png", (0, 0, 16, 16), scale=2)
zc_walk1 = crop("Enemys.png", (16, 0, 32, 16), scale=2)
for name, img in [("zenchan_walk0", zc_walk0), ("zenchan_walk1", zc_walk1)]:
    save_sprite(f"{name}_r", img)
    save_sprite(f"{name}_l", ImageOps.mirror(img))

# ========================================================== BUBBLE =========
bubble = crop("AttackBubbleColored.png", (48, 0, 64, 16), scale=2)
save_sprite("bubble", bubble)

# ============================================================ FOOD =========
food_watermelon = crop("Items.png", (304, 0, 322, 16), scale=2)
food_fries = crop("Items.png", (400, 0, 416, 16), scale=2)
save_sprite("food_watermelon", food_watermelon)
save_sprite("food_fries", food_fries)

# ======================================================== PLATFORM =========
# Kept hand-drawn (not from the reference repo) — these were validated
# earlier this session directly against real NES gameplay screenshots
# (retrogameman.com) and were never part of the "looks nothing like it"
# feedback, which was specifically about the characters/enemies.
SCALE = 4

def new_canvas(w, h):
    return Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))

def down(img, w, h):
    return img.resize((w, h), Image.BOX)

def lighten(c, amt):
    return tuple(min(255, v + amt) for v in c)

def darken(c, amt):
    return tuple(max(0, v - amt) for v in c)

from PIL import ImageDraw

PLAT_W, PLAT_H = 16, 8
PLAT_DARK = (25, 95, 30)
PLAT_CREAM = (230, 235, 165)
PLAT_LIME = (140, 225, 60)

def draw_platform_tile():
    W, H = PLAT_W, PLAT_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.rectangle([0, 0, W * S, H * S], fill=PLAT_DARK + (255,))
    d.rectangle([0, 0, W * S, 1.6 * S], fill=PLAT_CREAM + (255,))
    for fx in (4, 12):
        d.polygon([(fx * S, 2.5 * S), (fx * S + 3 * S, 4 * S), (fx * S, 6.5 * S), (fx * S - 3 * S, 4 * S)],
                   fill=PLAT_LIME + (255,))
        d.polygon([(fx * S, 2.5 * S), (fx * S + 3 * S, 4 * S), (fx * S, 4 * S)],
                   fill=lighten(PLAT_LIME, 35) + (255,))
    img = down(c, W, H)
    save_sprite("platform", img)

draw_platform_tile()

WALL_SIZE = 16
WALL_A = (225, 215, 150)
WALL_B = (35, 110, 40)

def draw_wall_tile():
    W = H = WALL_SIZE
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    half = (W * S) / 2.0
    d.rectangle([0, 0, W * S, H * S], fill=WALL_A + (255,))
    for qx, qy in [(0, 0), (half, half)]:
        d.rectangle([qx, qy, qx + half, qy + half], fill=WALL_B + (255,))
        pad = half * 0.18
        d.ellipse([qx + pad, qy + pad, qx + half - pad, qy + half - pad],
                   fill=lighten(WALL_B, 30) + (255,))
    img = down(c, W, H)
    save_sprite("wall", img)

draw_wall_tile()

# ============================================================= EXPORT ======
def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

header_lines = []
header_lines.append("// AUTO-GENERATED by tools/make_bubblebobble_sprites.py — do not hand-edit.")
header_lines.append("// Character/enemy/bubble/food sprites are cropped from JulianRijken/BubbleBobble")
header_lines.append("// (GPL-3.0, see tools/bb_ref_assets/SOURCE.md), converted to RGB565. Platform/")
header_lines.append("// wall tiles are hand-drawn (validated against real NES screenshots). \"Empty\"")
header_lines.append("// background pixels are pre-baked to the shared BG color so a plain")
header_lines.append("// tft->drawRGBBitmap() blit reads as a transparent sprite.")
header_lines.append("#pragma once")
header_lines.append("#include <Arduino.h>")
header_lines.append("")

for name, (w, h, img) in sprites.items():
    rgba = img.convert("RGBA")
    px_data = rgba.load()
    values = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px_data[x, y]
            if a < 128:
                r, g, b = BG
            elif a < 255:
                r = (r * a + BG[0] * (255 - a)) // 255
                g = (g * a + BG[1] * (255 - a)) // 255
                b = (b * a + BG[2] * (255 - a)) // 255
            values.append(to_rgb565(r, g, b))
    const_name = f"BB_SPR_{name.upper()}"
    header_lines.append(f"const int {const_name}_W = {w};")
    header_lines.append(f"const int {const_name}_H = {h};")
    header_lines.append(f"const uint16_t {const_name}[{w*h}] PROGMEM = {{")
    for row in range(0, len(values), 12):
        chunk = values[row:row+12]
        header_lines.append("  " + ", ".join(f"0x{v:04X}" for v in chunk) + ",")
    header_lines.append("};")
    header_lines.append("")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines))

print("Generated", len(sprites), "sprites")
for name, (w, h, _) in sprites.items():
    print(f"  {name}: {w}x{h}")
