"""
Draws an ORIGINAL stage-boss creature for the R-Type-style shmup,
entirely from scratch with PIL primitives — not a copy of the dormant
version's "Guard Ray" (a real R-Type III boss name/likeness; see
rtype_pr32.h's comment for why that wasn't reused). Only the general
FIGHT STRUCTURE carries over (patrol/lob -> settle-and-home -> split
into 3 at low HP — a common phase-based shmup-boss convention, not
unique expression), not the visual design.

Design: a dark-steel "sentinel core" — a diamond-shaped central hull
with a glowing amber eye, flanked by two smaller wing-pods (which
visually foreshadow the 3-way split: center + 2 pods becomes 3 separate
units once it splits). 4 frames: idle pulse (2), hit-flash, low-HP
(redder core) — same frame set/roles as make_arkanoid_pr32_boss.py's
boss, own separate palette (this game's own dedicated boss palette,
switched to/from only while the boss is on screen, same "switch, draw,
switch back" pattern used throughout this project).
"""
import os
from PIL import Image, ImageDraw

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(SCRIPT_DIR, "sprite_previews", "rtype_pr32")
HEADER_PATH = os.path.join(SCRIPT_DIR, "..", "src", "rtype_pr32_boss.h")
os.makedirs(OUT_DIR, exist_ok=True)

# Main unit and the 3 post-split units share this file but are drawn at
# two different sizes (matches the dormant version's own two hitbox/
# sprite sizes: whole ~40x26, split ~24x16).
MAIN_W, MAIN_H = 40, 28
SPLIT_W, SPLIT_H = 22, 16


def draw_boss(W, H, core_color, flash=False):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = W / 2, H / 2

    hull = (255, 255, 255, 255) if flash else (70, 78, 90, 255)
    hull_dark = (255, 255, 255, 255) if flash else (45, 51, 60, 255)
    trim = (255, 255, 255, 255) if flash else (170, 200, 210, 255)

    # Diamond central hull.
    halfw, halfh = W * 0.22, H * 0.42
    d.polygon([(cx, cy - halfh), (cx + halfw, cy), (cx, cy + halfh), (cx - halfw, cy)], fill=hull)
    d.polygon([(cx, cy), (cx + halfw, cy), (cx, cy + halfh), (cx - halfw, cy)], fill=hull_dark)

    # Two wing-pods (foreshadow the 3-way split).
    pod_r = H * 0.22
    for side in (-1, 1):
        px = cx + side * (halfw + pod_r * 0.9)
        d.ellipse([px - pod_r, cy - pod_r, px + pod_r, cy + pod_r], fill=hull)
        d.ellipse([px - pod_r * 0.5, cy - pod_r * 0.5, px + pod_r * 0.5, cy + pod_r * 0.5], fill=hull_dark)
        if not flash:
            d.ellipse([px - pod_r * 0.25, cy - pod_r * 0.25, px + pod_r * 0.25, cy + pod_r * 0.25], fill=trim)

    # Trim accents on the hull tips.
    if not flash:
        d.ellipse([cx - 2, cy - halfh - 2, cx + 2, cy - halfh + 2], fill=trim)
        d.ellipse([cx - 2, cy + halfh - 2, cx + 2, cy + halfh + 2], fill=trim)

    # Glowing eye-core.
    r = min(halfw, halfh) * 0.55
    core_c = (255, 255, 255, 255) if flash else core_color
    d.ellipse([cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2], fill=(20, 18, 22, 255) if not flash else (255, 255, 255, 255))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=core_c)
    d.ellipse([cx - r * 0.4, cy - r * 0.4, cx + r * 0.4, cy + r * 0.4], fill=(255, 255, 255, 255))

    return img


AMBER = (255, 175, 60, 255)
AMBER_BRIGHT = (255, 205, 110, 255)
RED_LOW = (235, 70, 60, 255)

frames_main = {
    "main_idle_a": draw_boss(MAIN_W, MAIN_H, AMBER),
    "main_idle_b": draw_boss(MAIN_W, MAIN_H, AMBER_BRIGHT),
    "main_hit": draw_boss(MAIN_W, MAIN_H, None, flash=True),
    "main_low_hp": draw_boss(MAIN_W, MAIN_H, RED_LOW),
}
frames_split = {
    "split_idle_a": draw_boss(SPLIT_W, SPLIT_H, AMBER),
    "split_idle_b": draw_boss(SPLIT_W, SPLIT_H, AMBER_BRIGHT),
    "split_hit": draw_boss(SPLIT_W, SPLIT_H, None, flash=True),
    "split_low_hp": draw_boss(SPLIT_W, SPLIT_H, RED_LOW),
}

all_frames = {**frames_main, **frames_split}
for name, img in all_frames.items():
    img.save(os.path.join(OUT_DIR, f"boss_{name}.png"))


def to_rgb565(r, g, b):
    return ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3)


ALPHA_THRESHOLD = 128
palette_rgb = [(0, 0, 0)]
color_to_index = {(0, 0, 0): 0}


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


frame_data = {name: pack_4bpp(img) for name, img in all_frames.items()}
while len(palette_rgb) < 16:
    palette_rgb.append((0, 0, 0))

header_lines = []
header_lines.append("// AUTO-GENERATED by tools/make_rtype_pr32_boss.py — do not hand-edit.")
header_lines.append("// An ORIGINAL boss creature design (not the dormant version's real-IP")
header_lines.append("// \"Guard Ray\" likeness) — see this script's docstring.")
header_lines.append("#pragma once")
header_lines.append("#include <cstdint>")
header_lines.append("#include <graphics/Renderer.h>")
header_lines.append("")
header_lines.append("namespace rtype_pr32_boss {")
header_lines.append("")
for name, data in frame_data.items():
    hex_bytes = ", ".join(f"0x{b:02X}" for b in data)
    header_lines.append(f"static const uint8_t {name.upper()}_DATA[] = {{ {hex_bytes} }};")
header_lines.append("")
header_lines.append("static const uint16_t PALETTE_RGB565[16] = {")
header_lines.append("    " + ", ".join(f"0x{to_rgb565(r, g, b):04X}" for (r, g, b) in palette_rgb))
header_lines.append("};")
header_lines.append("")
header_lines.append("static const pixelroot32::graphics::Color PALETTE_COLORS[16] = {")
header_lines.append("    " + ", ".join(f"static_cast<pixelroot32::graphics::Color>({i})" for i in range(16)))
header_lines.append("};")
header_lines.append("")
for name in all_frames:
    w, h = all_frames[name].size
    header_lines.append(
        f"static const pixelroot32::graphics::Sprite4bpp {name.upper()} = "
        f"{{ {name.upper()}_DATA, PALETTE_COLORS, {w}, {h}, 16 }};"
    )
header_lines.append("")
header_lines.append("} // namespace rtype_pr32_boss")

with open(HEADER_PATH, "w") as f:
    f.write("\n".join(header_lines) + "\n")

print(f"Generated {len(all_frames)} boss frames, {len(color_to_index)} colors used (of 15 available)")
