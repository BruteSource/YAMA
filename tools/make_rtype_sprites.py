"""
Authors pixel-art sprite bitmaps for R-Type (hand-drawn via PIL, not a
generative model - see conversation), converts each to RGB565, and emits
a C header the firmware includes directly.

Technique: draw shapes at 4x scale for clean proportions/gradients, downscale
with box filtering for smooth anti-aliased color blends (mimics hand-dithered
shading), then hand-place crisp 1px outline/highlight pixels at native
resolution for pixel-art readability (supersampling alone looks too soft/
vector-y for retro sprite work).
"""
import os
from PIL import Image, ImageDraw

SCALE = 4
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "rtype")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "rtype_sprites.h")
os.makedirs(OUT_DIR, exist_ok=True)

def canvas(w, h):
    return Image.new("RGBA", (w * SCALE, h * SCALE, ), (0, 0, 0, 0)),

def new_canvas(w, h):
    return Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))

def down(img, w, h):
    return img.resize((w, h), Image.BOX)

def px(img, x, y, color):
    if 0 <= x < img.width and 0 <= y < img.height:
        img.putpixel((x, y), color)

sprites = {}  # name -> (w, h, PIL.Image RGBA)

def save_sprite(name, w, h, img):
    sprites[name] = (w, h, img)
    img.save(f"{OUT_DIR}/{name}.png")

def save_sprite_rotated(name, w, h, img):
    # The game switched from a horizontal (landscape) to vertical (portrait)
    # shooter: "forward" went from +X (right) to -Y (up). ROTATE_90 is a
    # counter-clockwise rotation, which is exactly the transform that takes
    # "points right" to "points up" — applied uniformly to every direction-
    # dependent sprite (ship, drifter, turret, player bullets) so eyes/
    # barrels/noses all keep the same relationship to their new direction of
    # travel that they had to their old one, with no need to hand-redraw.
    rotated = img.transpose(Image.ROTATE_90)
    save_sprite(name, h, w, rotated)

# ============================================================= SHIP =====
def make_ship():
    W, H = 32, 18
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    WHITE = (232, 238, 248, 255)
    WHITE_SHADE = (185, 196, 214, 255)
    BLUE = (60, 150, 230, 255)
    BLUE_DK = (35, 105, 175, 255)
    RED = (222, 60, 70, 255)
    RED_DK = (165, 35, 45, 255)
    YELLOW = (255, 205, 50, 255)
    CANOPY = (110, 190, 250, 255)
    CANOPY_HI = (200, 235, 255, 255)

    midy = H * S // 2
    # Tail fins (drawn first, body overlaps their roots)
    d.polygon([(2*S, 3*S), (11*S, 5*S), (5*S, -1*S)], fill=RED)      # top fin
    d.polygon([(2*S, 15*S), (11*S, 13*S), (5*S, 19*S)], fill=RED)    # bottom fin
    d.polygon([(2*S, 3*S), (5*S, -1*S), (6*S, 0*S), (3.5*S, 3.5*S)], fill=RED_DK)
    d.polygon([(2*S, 15*S), (5*S, 19*S), (6*S, 18*S), (3.5*S, 14.5*S)], fill=RED_DK)

    # Main fuselage: tapered hull, wide at back, narrowing toward the nose
    d.polygon([
        (1*S, 5*S), (1*S, 13*S),
        (20*S, 12.5*S), (26*S, midy + 1*S/2),
        (26*S, midy - 1*S/2), (20*S, 5.5*S),
    ], fill=WHITE)
    # Belly shade (lower half slightly darker for volume)
    d.polygon([
        (1*S, 10*S), (1*S, 13*S), (20*S, 12.5*S), (20*S, 9.5*S),
    ], fill=WHITE_SHADE)

    # Nose cone
    d.polygon([(20*S, 4*S), (20*S, 14*S), (31*S, midy)], fill=WHITE)
    d.polygon([(20*S, 10*S), (20*S, 14*S), (26*S, midy + 1*S)], fill=WHITE_SHADE)
    # Yellow joint band + nose tip sensor
    d.line([(20*S, 3*S), (20*S, 15*S)], fill=YELLOW, width=S)
    d.ellipse([(28*S, midy - S), (30*S, midy + S)], fill=YELLOW)

    # Blue racing stripe down the spine
    d.line([(3*S, midy), (24*S, midy)], fill=BLUE, width=S)
    d.line([(3*S, midy - S), (22*S, midy - S)], fill=BLUE_DK, width=max(1, S // 2))

    # Canopy
    d.ellipse([(6*S, midy - 3*S), (13*S, midy + 1*S)], fill=CANOPY)
    d.ellipse([(7*S, midy - 3*S), (10*S, midy - S)], fill=CANOPY_HI)

    img = down(c, W, H)
    # Crisp hand-placed outline pass at native res for readability.
    d2 = ImageDraw.Draw(img)
    save_sprite_rotated("ship", W, H, img)

make_ship()

# ============================================================= FORCE POD ===
def make_force_pod():
    W = H = 12
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    RIM = (55, 120, 175, 255)
    BODY = (90, 180, 255, 255)
    GLOW = (170, 225, 255, 255)
    HI = (235, 250, 255, 255)
    cx, cy, r = W * SCALE / 2, H * SCALE / 2, W * SCALE / 2 - SCALE
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=RIM)
    d.ellipse([cx - r + SCALE, cy - r + SCALE, cx + r - SCALE, cy + r - SCALE], fill=BODY)
    d.ellipse([cx - r*0.35, cy - r*0.55, cx + r*0.15, cy - r*0.05], fill=GLOW)
    d.ellipse([cx - r*0.15, cy - r*0.45, cx - r*0.0, cy - r*0.2], fill=HI)
    img = down(c, W, H)
    save_sprite("force_pod", W, H, img)

make_force_pod()

# ============================================================= DRIFTER =====
def make_drifter():
    W = H = 20
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    BODY = (175, 75, 185, 255)
    BODY_DK = (120, 45, 130, 255)
    MOTTLE = (140, 55, 150, 255)
    EYE_RING = (40, 15, 45, 255)
    EYE = (235, 225, 70, 255)
    PUPIL = (35, 30, 10, 255)
    S = SCALE
    # Trailing tail lobe + head (teardrop)
    d.ellipse([1*S, 5*S, 11*S, 15*S], fill=BODY_DK)
    d.ellipse([6*S, 3*S, 18*S, 17*S], fill=BODY)
    # Mottling
    d.ellipse([7*S, 4*S, 11*S, 8*S], fill=MOTTLE)
    d.ellipse([12*S, 11*S, 16*S, 15*S], fill=MOTTLE)
    # Eye
    d.ellipse([11*S, 6*S, 17*S, 12*S], fill=EYE_RING)
    d.ellipse([12*S, 7*S, 16*S, 11*S], fill=EYE)
    d.ellipse([13.5*S, 8.5*S, 15*S, 10*S], fill=PUPIL)
    d.ellipse([12.5*S, 7.5*S, 13.5*S, 8.5*S], fill=(255, 255, 255, 255))
    img = down(c, W, H)
    save_sprite_rotated("drifter", W, H, img)

make_drifter()

# ============================================================= TURRET ======
def make_turret():
    W, H = 16, 12
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    BASE = (110, 118, 138, 255)
    BASE_DK = (75, 82, 100, 255)
    LENS = (255, 90, 110, 255)
    S = SCALE
    d.rectangle([1*S, 6*S, 15*S, 11*S], fill=BASE_DK)
    d.ellipse([4*S, 2*S, 14*S, 12*S], fill=BASE)
    d.rectangle([0*S, 5.5*S, 6*S, 7.5*S], fill=BASE)
    d.ellipse([7*S, 4.5*S, 12*S, 9.5*S], fill=BASE_DK)
    d.ellipse([8*S, 5.5*S, 11*S, 8.5*S], fill=LENS)
    img = down(c, W, H)
    save_sprite_rotated("turret", W, H, img)

make_turret()

# ============================================================= GUARD RAY ===
def make_guard_ray(name, W, H):
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    BODY = (145, 110, 165, 255)
    BODY_DK = (105, 75, 125, 255)
    ACCENT = (255, 210, 80, 255)
    CORE_OUT = (255, 140, 60, 255)
    CORE_IN = (255, 250, 210, 255)
    S = SCALE
    cx, cy = W * S / 2, H * S / 2
    halfw, halfh = W * S / 2 - 3 * S, H * S * 0.30
    # Flattened disc body
    d.ellipse([cx - halfw, cy - halfh, cx + halfw, cy + halfh], fill=BODY)
    d.ellipse([cx - halfw + S, cy - halfh + S * 0.6, cx + halfw - S, cy - S * 0.5], fill=BODY_DK)
    # Wingtips (left/right) + tail fin (bottom) -- 3 points
    d.polygon([(cx - halfw, cy - halfh * 0.6), (cx - halfw - 3*S, cy), (cx - halfw, cy + halfh * 0.6)], fill=ACCENT)
    d.polygon([(cx + halfw, cy - halfh * 0.6), (cx + halfw + 3*S, cy), (cx + halfw, cy + halfh * 0.6)], fill=ACCENT)
    d.polygon([(cx - 3*S, cy + halfh), (cx + 3*S, cy + halfh), (cx, cy + halfh + 3*S)], fill=ACCENT)
    # Glowing core
    r = min(halfw, halfh) * 0.62
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=CORE_OUT)
    d.ellipse([cx - r*0.5, cy - r*0.5, cx + r*0.5, cy + r*0.5], fill=CORE_IN)
    img = down(c, W, H)
    save_sprite(name, W, H, img)

make_guard_ray("guard_ray_main", 40, 26)
make_guard_ray("guard_ray_split", 24, 16)

# ============================================================= BULLETS =====
def make_bullet_weak():
    W, H = 10, 5
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    OUT = (230, 160, 40, 255)
    IN = (255, 240, 150, 255)
    S = SCALE
    d.ellipse([0, 0.5*S, 10*S, 4.5*S], fill=OUT)
    d.ellipse([1*S, 1.2*S, 9*S, 3.8*S], fill=IN)
    img = down(c, W, H)
    save_sprite_rotated("bullet_weak", W, H, img)

def make_bullet_charged():
    W, H = 18, 7
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    OUT = (60, 200, 165, 255)
    MID = (140, 255, 210, 255)
    IN = (240, 255, 250, 255)
    S = SCALE
    d.rounded_rectangle([0, 0.5*S, 18*S, 6.5*S], radius=3*S, fill=OUT)
    d.rounded_rectangle([1*S, 1.3*S, 17*S, 5.7*S], radius=2*S, fill=MID)
    d.line([(2*S, 3.5*S), (16*S, 3.5*S)], fill=IN, width=int(1.4*S))
    img = down(c, W, H)
    save_sprite_rotated("bullet_charged", W, H, img)

def make_bullet_force():
    W, H = 7, 5
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    OUT = (220, 130, 40, 255)
    IN = (255, 200, 110, 255)
    S = SCALE
    d.ellipse([0, 0.5*S, 7*S, 4.5*S], fill=OUT)
    d.ellipse([1*S, 1.2*S, 6*S, 3.8*S], fill=IN)
    img = down(c, W, H)
    save_sprite_rotated("bullet_force", W, H, img)

def make_enemy_bullet():
    W = H = 8
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    OUT = (150, 20, 35, 255)
    IN = (255, 100, 115, 255)
    HI = (255, 200, 200, 255)
    S = SCALE
    d.ellipse([0, 0, 8*S, 8*S], fill=OUT)
    d.ellipse([1*S, 1*S, 7*S, 7*S], fill=IN)
    d.ellipse([2.5*S, 2.5*S, 4*S, 4*S], fill=HI)
    img = down(c, W, H)
    save_sprite("enemy_bullet", W, H, img)

make_bullet_weak()
make_bullet_charged()
make_bullet_force()
make_enemy_bullet()

# ============================================================= CAPSULES ====
def make_capsule(name, icon):
    W = H = 12
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    S = SCALE
    if icon == "speed":
        BODY = (60, 190, 110, 255)
        BODY_DK = (35, 140, 80, 255)
        ICON = (235, 255, 240, 255)
    else:
        BODY = (215, 180, 45, 255)
        BODY_DK = (165, 130, 20, 255)
        ICON = (255, 250, 220, 255)
    d.rounded_rectangle([0, 0, 12*S, 12*S], radius=3*S, fill=BODY)
    d.rounded_rectangle([1*S, 1*S, 11*S, 6*S], radius=2*S, fill=BODY_DK)
    if icon == "speed":
        d.polygon([(3*S, 3*S), (3*S, 9*S), (7*S, 6*S)], fill=ICON)
        d.polygon([(6*S, 3*S), (6*S, 9*S), (10*S, 6*S)], fill=ICON)
    else:
        cx, cy, r = 6*S, 6*S, 4*S
        import math
        pts = []
        for i in range(10):
            ang = -math.pi/2 + i * math.pi / 5
            rr = r if i % 2 == 0 else r * 0.45
            pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
        d.polygon(pts, fill=ICON)
    img = down(c, W, H)
    save_sprite(name, W, H, img)

make_capsule("capsule_speed", "speed")
make_capsule("capsule_weapon", "weapon")

# ============================================================= DEBRIS ======
def make_debris(name, W, H, seed_variant):
    c = new_canvas(W, H)
    d = ImageDraw.Draw(c)
    BASE = (100, 105, 120, 255)
    LIGHT = (150, 155, 172, 255)
    DARK = (60, 64, 78, 255)
    RIVET = (40, 43, 54, 255)
    S = SCALE
    d.rounded_rectangle([0, 0, W*S - 1, H*S - 1], radius=2*S, fill=BASE)
    d.polygon([(0, 0), (W*S*0.6, 0), (0, H*S*0.6)], fill=LIGHT)
    d.polygon([(W*S, H*S), (W*S*0.4, H*S), (W*S, H*S*0.4)], fill=DARK)
    if seed_variant == 0:
        d.line([(3*S, H*S*0.5), (W*S - 3*S, H*S*0.5)], fill=DARK, width=max(1, S//2))
    elif seed_variant == 1:
        d.line([(W*S*0.5, 2*S), (W*S*0.5, H*S - 2*S)], fill=DARK, width=max(1, S//2))
    else:
        d.line([(2*S, 2*S), (W*S - 2*S, H*S - 2*S)], fill=DARK, width=max(1, S//2))
    for cx, cy in [(2*S, 2*S), (W*S-2*S, 2*S), (2*S, H*S-2*S), (W*S-2*S, H*S-2*S)]:
        d.ellipse([cx-S*0.6, cy-S*0.6, cx+S*0.6, cy+S*0.6], fill=RIVET)
    img = down(c, W, H)
    save_sprite(name, W, H, img)

make_debris("debris_0", 24, 16, 0)
make_debris("debris_1", 30, 20, 1)
make_debris("debris_2", 20, 24, 2)

# ============================================================= EXPORT ======
def to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

header_lines = []
header_lines.append("// AUTO-GENERATED by tools/make_rtype_sprites.py — do not hand-edit.")
header_lines.append("// Hand-authored pixel art (PIL script), converted to RGB565, embedded in")
header_lines.append("// flash. \"Empty\" background pixels are pre-baked to RT_BG so a plain")
header_lines.append("// tft->drawRGBBitmap() blit reads as a transparent sprite against the")
header_lines.append("// game's solid background — no per-pixel masking needed at draw time.")
header_lines.append("#pragma once")
header_lines.append("#include <Arduino.h>")
header_lines.append("")

RT_BG = (4, 6, 16)

for name, (w, h, img) in sprites.items():
    rgba = img.convert("RGBA")
    px_data = rgba.load()
    values = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px_data[x, y]
            if a < 128:
                r, g, b = RT_BG
            else:
                # Alpha-blend partially transparent edge pixels onto RT_BG so
                # anti-aliased edges don't leave a dark halo.
                if a < 255:
                    r = (r * a + RT_BG[0] * (255 - a)) // 255
                    g = (g * a + RT_BG[1] * (255 - a)) // 255
                    b = (b * a + RT_BG[2] * (255 - a)) // 255
            values.append(to_rgb565(r, g, b))
    const_name = f"RT_SPR_{name.upper()}"
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
