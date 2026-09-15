# Source of these sprite sheets

Cropped directly from [JulianRijken/BubbleBobble](https://github.com/JulianRijken/BubbleBobble)
("a fresh take on the original game, taking inspiration from both the NES
and Arcade versions"), at the user's explicit request to port from this
project for maximum accuracy instead of hand-drawn approximations.

Licensed GPL-3.0 (see that repo's `LICENSE.md`). Used here as sprite
source material for a personal, non-distributed hobby project; this
notice + the upstream URL is kept for attribution.

Files:
- `BubbleCharacter.png` — Bub, 96x64, 4x4 grid of 24x16 frames
- `Enemys.png` — enemies (Zen-Chan et al.), 256x192, 8x6 grid of 32x32 frames
- `AttackBubbleColored.png` — the player's capture-bubble projectile
  animation, 112x32, 7x2 grid of 16x16 frames (row 0 = green/Bub's color,
  row 1 = cyan/Bob's)
- `Items.png` — pickup icons, 576x64, 18x2 grid of 32x32 frames

`tools/make_bubblebobble_sprites.py` crops specific frames from these
(see that script for exact frame coordinates and which frame is which)
rather than re-hand-drawing the art, converting each crop to RGB565 the
same way every other game's sprites are generated.
