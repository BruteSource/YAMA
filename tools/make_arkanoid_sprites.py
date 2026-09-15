"""
Authors pixel-art sprite bitmaps for Arkanoid (hand-drawn via PIL, not a
generative model), converts each to RGB565, and emits a C header the
firmware includes directly. See tools/make_rtype_sprites.py for the base
technique this follows (4x supersampled draw -> box-filter downscale ->
RGB565, background pre-baked so a plain tft->drawRGBBitmap() blit reads as
transparent).

Researched reference (WebSearch, see conversation): the original 1986
Taito Vaus paddle is a banded sci-fi ship — red end-flanks (45-degree
bounce zones), a silver/white center strip (near-vertical bounce zone),
over a blue-toned hull — not a plain bar. Bricks come in a fixed 8-color
legend (white/orange/cyan/green/red/blue/violet/yellow), silver bricks
take multiple hits, gold/steel bricks are indestructible. Power-up
capsules are a rounded pill with a single bold letter; this project's 5
existing power-up types are mapped to the closest authentic letter+color
(S=slow/orange and P=life/gray match the original directly; E=expand/blue
stands in for widen; D=disruption/light-blue stands in for multiball;
N=narrow has no original-Arkanoid equivalent since vanilla Arkanoid didn't
have a "shrink" capsule, so it keeps a distinct but non-canonical color).

Since paddle width changes continuously with power-ups (26-70px), it's
built from 3 slices (left cap + repeatable middle tile + right cap)
composited at draw time in arkanoid.cpp, not one fixed-width bitmap.
"""
import os
import math
from PIL import Image, ImageDraw, ImageFont

SCALE = 4
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "arkanoid")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "arkanoid_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

BG = (6, 8, 22)  # matches palette.cpp's BG = rgb565(6,8,22)

def new_canvas(w, h):
    return Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))

def down(img, w, h):
    return img.resize((w, h), Image.BOX)

sprites = {}

def save_sprite(name, w, h, img):
    sprites[name] = (w, h, img)
    img.save(f"{OUT_DIR}/{name}.png")

# ============================================================= PADDLE ======
# Vaus: blue hull, red flank bands (45-degree bounce zones), silver/white
# center strip (near-vertical bounce zone). Built as 3 slices so any
# continuous paddle width can be composited at runtime.
PADDLE_H = 8
BLUE_HULL = (40, 130, 210, 255)
BLUE_HULL_DK = (20, 90, 160, 255)
RED_BAND = (225, 60, 70, 255)
RED_BAND_DK = (170, 35, 45, 255)
SILVER = (225, 232, 240, 255)
SILVER_DK = (170, 180, 195, 255)

def make_paddle_left():
    W, H = 9, PADDLE_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    # Tapered hull point (swept wingtip) in red (bounce-45 zone)
    d.polygon([(0, 2*S), (0, (H-2)*S), (6*S, (H-1)*S), (9*S, (H/2)*S), (6*S, 1*S)], fill=RED_BAND)
    d.polygon([(0, 2*S), (0, 3*S), (5*S, (H/2)*S), (0, (H-3)*S)], fill=RED_BAND_DK)
    # Blue hull top/bottom accent lines
    d.line([(1*S, 0.5*S), (7*S, 0.5*S)], fill=BLUE_HULL, width=max(1, S//2))
    d.line([(1*S, (H-0.5)*S), (7*S, (H-0.5)*S)], fill=BLUE_HULL_DK, width=max(1, S//2))
    img = down(c, W, H)
    save_sprite("paddle_left", W, H, img)

def make_paddle_right():
    W, H = 9, PADDLE_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.polygon([(9*S, 2*S), (9*S, (H-2)*S), (3*S, (H-1)*S), (0, (H/2)*S), (3*S, 1*S)], fill=RED_BAND)
    d.polygon([(9*S, 2*S), (9*S, 3*S), (4*S, (H/2)*S), (9*S, (H-3)*S)], fill=RED_BAND_DK)
    d.line([(2*S, 0.5*S), (8*S, 0.5*S)], fill=BLUE_HULL, width=max(1, S//2))
    d.line([(2*S, (H-0.5)*S), (8*S, (H-0.5)*S)], fill=BLUE_HULL_DK, width=max(1, S//2))
    img = down(c, W, H)
    save_sprite("paddle_right", W, H, img)

def make_paddle_mid():
    W, H = 4, PADDLE_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.rectangle([0, 1*S, W*S, (H-1)*S], fill=SILVER)
    d.line([(0, 1.5*S), (W*S, 1.5*S)], fill=(255, 255, 255, 255), width=max(1, S//2))
    d.line([(0, (H-1.5)*S), (W*S, (H-1.5)*S)], fill=SILVER_DK, width=max(1, S//2))
    d.line([(0, 0.5*S), (W*S, 0.5*S)], fill=BLUE_HULL, width=max(1, S//2))
    d.line([(0, (H-0.5)*S), (W*S, (H-0.5)*S)], fill=BLUE_HULL_DK, width=max(1, S//2))
    img = down(c, W, H)
    save_sprite("paddle_mid", W, H, img)

make_paddle_left()
make_paddle_right()
make_paddle_mid()

# ============================================================= BALL ========
def make_ball():
    W = H = 8
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.ellipse([0, 0, W*S, H*S], fill=(150, 150, 155, 255))          # dark rim for contrast
    d.ellipse([0.4*S, 0.4*S, (W-0.4)*S, (H-0.4)*S], fill=(238, 238, 235, 255))
    d.ellipse([2.2*S, 2.2*S, (W-0.8)*S, (H-0.8)*S], fill=(205, 205, 205, 255))  # shaded lower-right
    d.ellipse([0.8*S, 0.8*S, (W-2.2)*S, (H-2.2)*S], fill=(255, 255, 253, 255))  # lit upper-left
    d.ellipse([1.2*S, 1*S, 3.4*S, 3.2*S], fill=(255, 255, 255, 255))            # bright highlight
    img = down(c, W, H)
    save_sprite("ball", W, H, img)

make_ball()

# ============================================================= BRICKS ======
BRICK_W, BRICK_H = 28, 14
BRICK_COLORS = {
    "white":  (235, 235, 240),
    "orange": (255, 150, 50),
    "cyan":   (70, 210, 230),
    "green":  (100, 220, 100),
    "red":    (255, 70, 90),
    "blue":   (70, 130, 255),
    "violet": (200, 90, 230),
    "yellow": (255, 225, 60),
}

def lighten(c, amt):
    return tuple(min(255, v + amt) for v in c)

def darken(c, amt):
    return tuple(max(0, v - amt) for v in c)

def make_brick(name, base):
    W, H = BRICK_W, BRICK_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.rectangle([0, 0, W*S, H*S], fill=darken(base, 40))
    d.rectangle([1*S, 1*S, (W-1)*S, (H-1)*S], fill=base)
    d.line([(1*S, 1.5*S), ((W-1)*S, 1.5*S)], fill=lighten(base, 55), width=max(1, S//2))
    d.line([(1.5*S, 1*S), (1.5*S, (H-1)*S)], fill=lighten(base, 40), width=max(1, S//2))
    d.line([(1*S, (H-1.5)*S), ((W-1)*S, (H-1.5)*S)], fill=darken(base, 45), width=max(1, S//2))
    img = down(c, W, H)
    save_sprite(f"brick_{name}", W, H, img)

for name, base in BRICK_COLORS.items():
    make_brick(name, base + (255,))

def make_brick_silver(name, base):
    W, H = BRICK_W, BRICK_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.rectangle([0, 0, W*S, H*S], fill=darken(base, 45) + (255,))
    d.rectangle([1*S, 1*S, (W-1)*S, (H-1)*S], fill=base + (255,))
    # Riveted metal-panel look: diagonal shine band + corner rivets.
    d.polygon([(2*S, (H-1)*S), (2*S, (H-3)*S), ((W-2)*S, 2*S), ((W-2)*S, 4*S)],
              fill=lighten(base, 45) + (255,))
    for cx, cy in [(3*S, 3*S), ((W-3)*S, 3*S), (3*S, (H-3)*S), ((W-3)*S, (H-3)*S)]:
        d.ellipse([cx-S*0.6, cy-S*0.6, cx+S*0.6, cy+S*0.6], fill=darken(base, 60) + (255,))
    img = down(c, W, H)
    save_sprite(name, W, H, img)

make_brick_silver("brick_silver_full", (210, 215, 225))
make_brick_silver("brick_silver_cracked", (130, 135, 148))

def make_brick_steel():
    W, H = BRICK_W, BRICK_H
    base = (150, 120, 40)
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.rectangle([0, 0, W*S, H*S], fill=darken(base, 50) + (255,))
    d.rectangle([1*S, 1*S, (W-1)*S, (H-1)*S], fill=base + (255,))
    d.polygon([(2*S, (H-1)*S), (2*S, (H-3)*S), ((W-2)*S, 2*S), ((W-2)*S, 4*S)],
              fill=lighten(base, 80) + (255,))
    for cx, cy in [(3*S, 3*S), ((W-3)*S, 3*S), (3*S, (H-3)*S), ((W-3)*S, (H-3)*S)]:
        d.ellipse([cx-S*0.6, cy-S*0.6, cx+S*0.6, cy+S*0.6], fill=darken(base, 55) + (255,))
    img = down(c, W, H)
    save_sprite("brick_steel", W, H, img)

make_brick_steel()

# ============================================================= CAPSULES ====
# Letter+color mapping to the closest authentic Arkanoid capsule; see the
# module docstring for the two that deviate (N has no original equivalent).
PU_W, PU_H = 16, 9
CAPSULES = {
    "widen":  ("E", (60, 130, 230)),   # Expand (blue) - closest authentic match for "widen"
    "narrow": ("N", (210, 90, 170)),   # no original equivalent; distinct color, not canonical
    "multi":  ("D", (110, 200, 255)),  # Disruption (light blue) - closest match for multiball
    "slow":   ("S", (230, 150, 50)),   # Slow (orange) - direct match
    "life":   ("P", (170, 170, 178)),  # Player extra life (gray) - direct match
}

try:
    FONT = ImageFont.load_default()
except Exception:
    FONT = None

def make_capsule(name, letter, base):
    W, H = PU_W, PU_H
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    d.rounded_rectangle([0, 0, W*S - 1, H*S - 1], radius=3*S, fill=darken(base, 35) + (255,))
    d.rounded_rectangle([S*0.7, S*0.7, (W*S - 1) - S*0.7, (H*S - 1) - S*0.7], radius=2.5*S, fill=base + (255,))
    d.rounded_rectangle([S, S, (W-2)*S, 2.5*S], radius=1.5*S, fill=lighten(base, 45) + (255,))
    img = down(c, W, H)
    # Crisp letter at native resolution (a 4x-supersampled font would just
    # blur back down to mush at this tiny size) - draw last, unscaled.
    d2 = ImageDraw.Draw(img)
    letter_color = darken(base, 60)
    bbox = d2.textbbox((0, 0), letter, font=FONT)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d2.text(((W - tw) / 2 - bbox[0], (H - th) / 2 - bbox[1] - 1), letter, fill=letter_color, font=FONT)
    save_sprite(f"capsule_{name}", W, H, img)

for name, (letter, base) in CAPSULES.items():
    make_capsule(name, letter, base)

# ============================================================= EXPORT ======
def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

header_lines = []
header_lines.append("// AUTO-GENERATED by tools/make_arkanoid_sprites.py — do not hand-edit.")
header_lines.append("// Hand-authored pixel art (PIL script), converted to RGB565, embedded in")
header_lines.append("// flash. \"Empty\" background pixels are pre-baked to the shared BG color")
header_lines.append("// so a plain tft->drawRGBBitmap() blit reads as a transparent sprite.")
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
    const_name = f"AK_SPR_{name.upper()}"
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
