# EncoderColorESP32Test — Handheld Game Console

Custom ESP32 board: 2.0" 240x320 ST7789 SPI color TFT + EC11 rotary encoder
module (with onboard KEY0 button) + 2 hand-wired tactile buttons + a piezo
buzzer. What started as a hardware bring-up test (still in
`reference/hardware_test.cpp`) is now a small multi-game console — see
Firmware below.

> ## ⚠ Current state: temporarily a single-game PixelRoot32 build
> The menu and 4 of the 5 games described below (Arkanoid/Tetris/
> Bust-a-Move/R-Type) are **set aside, not deleted** — their source is
> untouched in `src/`, just excluded from the active build via
> `platformio.ini`'s `src_filter`. Bubble Bobble is being rebuilt from
> scratch on the [PixelRoot32](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine)
> engine (real dirty-region rendering + tile/physics primitives) instead
> of this project's hand-rolled direct-to-display approach, after three
> rounds of rendering bugs (flicker, background art getting deleted,
> entities stomping each other) on the old approach. Deliberately only
> ONE display-driver library runs at a time — no switching logic between
> PixelRoot32's own `TFT_eSPI` driver and this project's normal
> `Adafruit_ST7789` pipeline. **`src/main.cpp` currently hosts the
> PixelRoot32 engine directly, not the menu/game-registry loop described
> in "Firmware" below** — that section still describes the *dormant*
> architecture, accurate for when the other games return. Full plan,
> API notes, and everything learned integrating PixelRoot32 (including a
> load-bearing `platformio.ini` platform-version pin — see the comment
> above `platform =` there before "helpfully" updating it):
> `~/.claude/plans/goofy-dancing-locket.md`. Bringing the other 4 games
> back later is a one-line `src_filter` edit plus restoring
> `src/main.cpp`/`src/hardware.cpp` from `.bak_pre_pixelroot32/` (or just
> re-adding the menu bootstrap) — nothing about them was removed.

## Board

The display/encoder combo is a common "2.0 inch TFT + EC11 rotary encoder"
module (ST7789 driver, 240x320, SPI). It has a 12-pin header exposing both
the display and encoder/button signals:

```
GND VDD SCL SDA RES DC CS BLK TRA TRB PSH KO
```

`SCL`/`SDA` here are SPI SCK/MOSI (not I2C). The module also has its own
onboard tactile button silkscreened **KEY0**, wired to the `KO` header pin —
**not** a ground/common return, despite that being a common mistake in
auto-generated wiring charts (see Gotchas below).

## Confirmed working pinout

| Signal | ESP32 Pin | Notes |
|---|---|---|
| TFT SDA / MOSI | D23 | Hardware VSPI MOSI |
| TFT SCK | D18 | Hardware VSPI SCK (default, not always in vendor wiring lists but required) |
| TFT RES | D4 | Digital output |
| TFT DC | D2 | Digital output |
| TFT CS | D15 | Hardware VSPI CS |
| TFT BLK | D21 (PIN_BACKLIGHT) | Backlight, now PWM-dimmable via an S8550 PNP high-side switch — was direct-to-3V3/always-on before this was added; see hardware.h's PIN_BACKLIGHT comment |
| Encoder A (TRA) | **D13** | INPUT_PULLUP. **Not D12** — see Gotchas |
| Encoder B (TRB) | D14 | INPUT_PULLUP |
| Encoder push (PSH) — **R3** | D27 | INPUT_PULLUP — the encoder shaft's integrated switch |
| KEY0 (KO) — **A** | **D26** | INPUT_PULLUP — the module's separate onboard button (still physically silkscreened "KEY0" on the board itself; **A** is this project's own gamepad-style logical name for it), **not** a ground return |
| Hand-wired Button 1 — **RB** | D32 | INPUT_PULLUP — in-game "fire/confirm" button |
| Hand-wired Button 2 — **LB** | D33 | INPUT_PULLUP |
| Button common | GND | Return line for the two hand-wired buttons |
| Piezo buzzer | D19 | Output, `tone()`. Also VSPI MISO, unused since the display is write-only, so no conflict |
| Thumbstick X | D34 | ADC1, input-only — no WiFi/ADC2 conflict since WiFi is never enabled here |
| Thumbstick Y | D35 | ADC1, input-only, same reasoning |
| Thumbstick push switch — **L3** | D25 | INPUT_PULLUP, same active-low pattern as the other buttons |

All digital inputs are active-low (pressed = reading LOW) using internal
pull-ups. The thumbstick's two axes are analog (`analogRead`), smoothed and
deadzoned in `hardwareReadThumbstick()` (`hardware.cpp`) — the ESP32's ADC
is inherently noisy, so raw readings jitter regardless of which pin you use;
this isn't fixable by picking a different GPIO.
KEY0, ENC SW, and BTN1 currently run a widened 150ms software debounce
(vs. 25ms default) — see Gotchas #6.

## Gotchas discovered during bring-up

1. **Never wire anything with an external pull-up to GPIO12.** GPIO12
   (MTDI) is an ESP32 boot-strapping pin that selects flash voltage
   (high = 1.8V, low = 3.3V) at every reset, including entering flash
   download mode. The encoder module's onboard pull-up on TRA held it high,
   which selected 1.8V on a board with a 3.3V flash chip and broke flashing
   entirely (`esptool` reported "Failed to communicate with the flash chip"
   / "Flash voltage set by a strapping pin: 1.8V"). Fixed by moving encoder
   A from D12 to D13. If re-wiring from scratch, avoid D12 for anything with
   an external pull resistor.
2. **KO is not a common/ground pin.** An AI-generated wiring chart labeled
   `KO` as "Board Button Common → GND". In reality `KO` is the active-low
   signal line for the onboard **KEY0** button (same on-board pull-up
   pattern as PSH/TRA/TRB). It must go to a spare GPIO (D26), not GND.
   Wiring it to GND leaves that button permanently disconnected from the
   MCU.
3. A marginal/cold solder joint on the D26 connection caused heavy input
   chatter (hundreds of thousands of spurious press/release events per
   test run) on both KEY0 and its physically adjacent pin, ENC SW (D27).
   Resoldering both ends resolved it. If you ever see a button "machine
   gunning" PRESSED/released in the serial log, suspect a physical
   connection near that pin before suspecting firmware debounce logic —
   the debounce code itself was verified correct (BTN1/BTN2 on D32/D33
   never showed this behavior).
4. Other strapping pins to keep in mind if the layout ever changes: D0
   (boot mode), D2 (must be low/floating at boot — currently used for DC,
   which is fine since it's driven only after boot), D5, D15 (used for CS,
   fine for the same reason). Avoid routing external pull-ups/pull-downs to
   D0/D2/D12/D15 during power-on.
5. This ST7789 panel needs `tftReal.invertDisplay(true)` for correct colors.
   If colors ever look inverted (red renders as cyan, etc.) after a library
   update, flip that call. Display is currently `setRotation(2)` — portrait,
   240x320, but flipped 180° from the panel's native orientation (tried the
   controls from the other physical side of the board).
6. KEY0/ENC SW (D26/D27) and, once, BTN1 (D32) have each shown live
   electrical chatter (continuous spurious press events with nothing
   touching them) at different points — not a firmware bug each time this
   was checked. KEY0/ENC SW were fixed once by resoldering; it's possible
   handling the board while wiring the buzzer disturbed something nearby.
   Debounce was widened to 150ms as a stopgap, but that itself started
   costing real gameplay responsiveness (missed quick taps/rotates), so
   it's been **pulled back to 40ms**. User's working theory, not yet
   confirmed: the chatter may only happen while USB-connected to a PC
   (electrical noise from that connection) and hasn't caused a problem
   during actual untethered gameplay. Testing at 40ms; if chatter
   reappears in real play (not just while tethered for development), the
   debounce may need to go back up, but don't assume that's the fix
   without checking the physical connections first.
7. Don't call `btStop()` or `WiFi.mode(WIFI_OFF)` to save power on this
   board. Neither radio is ever initialized here (Arduino-ESP32 doesn't
   auto-start them), so both are already fully off by default — explicitly
   "disabling" them made things worse: `btStop()` boot-looped the board
   (crashes stopping a controller that was never started), and including
   `<WiFi.h>` at all cost ~600KB of flash pulling in the WiFi/TLS stack
   just to say "off." Simplest and safest is to not touch either API.
8. The serial screenshot tool **cannot verify which `setRotation()` value
   is physically correct** on this panel. It captures whatever the game
   code logically draws through `tft->...`, in the same top-left-origin
   coordinate space no matter which rotation constant is active — so a
   90°-off or upside-down physical mount looks *identical* in every
   screenshot. Getting `hardwareSetOrientation()`'s rotation constants
   right needs eyes on the real screen; R-Type's landscape rotation was
   wrong (rotation 3) on the first attempt and only rotation 1 was
   confirmed correct once the user physically looked at the board.
9. Unconditionally erasing-then-redrawing a sprite every single frame
   (even when it hasn't moved) can look like a visible flicker once
   there's motion elsewhere on screen drawing attention to that area —
   R-Type's own ship did this from the start, but it only became a
   noticeable complaint once the starfield around it was actually
   animating (see the bitmap-sprites/R-Type section for the fix: only
   erase+redraw when position or visibility actually changed). Worth
   checking for in any future game that flickers despite nothing being
   "wrong" with the sprite itself.

## Global controls (every game)

- **L3 (thumbstick push-switch) held 2 full seconds** forces an immediate
  return to the game-select menu, from **any** game, in **any** state —
  independent of each game's own pause/`wantsExit` logic. This is tracked
  globally in `main.cpp`'s `loop()`, not per-game, specifically as a
  universal escape hatch in case a game-specific exit path is confusing or
  a future game logic bug ever strands the player. Debug serial: `l` =
  tap, `L` = toggle a sustained hold (needed to test the 2-second timer
  remotely, same pattern as `S`/`W`).
- Everything else follows the per-game button-priority convention
  described under Firmware below.

## Firmware

`src/main.cpp` is the shell for a small multi-game console:

- **hardware.h/.cpp** — pin definitions, display init, encoder ISR
  (interrupt-driven full-step quadrature decode), debounced `Button`
  struct. Exposes `tft`, a `Adafruit_GFX*` pointer normally aimed at the
  real display (`tftReal`) but temporarily redirectable to an offscreen
  canvas for screenshot capture (see below) — every game draws through
  `tft->...` so it doesn't know or care which.
- **palette.h/.cpp** — shared background/text/title colors, `rgb565()`
  (standalone RGB888→RGB565 packer — deliberately not `tft->color565()`,
  since that's an `Adafruit_SPITFT`-only method the screenshot canvas
  doesn't have), `lighten()`/`darken()` for bevel shading, `drawCentered()`.
- **input.h**, **apps.h**, **menu.h/.cpp** — per-frame `InputState`, the
  `GameEntry` registry (`init`/`update`/`onExit`/`renderSnapshot` per
  game), and the game-select launcher screen.
- **sound.h/.cpp** — short SFX via the piezo buzzer (`tone()`/`noTone()`,
  non-blocking) plus tiny blocking arpeggios for rare events (level clear,
  game over).
- **arkanoid.cpp**, **tetris.cpp**, **bustamove.cpp**, **rtype.cpp**,
  **bubblebobble.cpp** — the games themselves. Each owns its state in an
  anonymous namespace and exposes `<name>Init/Update/OnExit/RenderSnapshot`.
  Add a new game by copying this pattern and adding one row to
  `games_registry.cpp`.

**Game-select menu**: turn the encoder (or push the thumbstick up/down) to
choose, **KEY0 to play** the highlighted entry (the onboard button, more
comfortable than the encoder's push-switch — this used to be ENC_SW).
KEY0 is not globally intercepted while at the menu (only while
`topState==AT_GAME`), so this needed no architecture change, just
`menu.cpp`'s confirm check. One consequence for remote/debug testing:
sending the debug `s` (KEY0) character is no longer a safe unconditional
"reset to the menu" — if already at the menu it launches the selected
game instead of doing nothing, since KEY0 does double duty (confirm at
the menu, back-to-menu inside a game); check which state you're in first.

### Button priority convention (applies to every game, including future ones)

This is a deliberate, standing rule, not per-game happenstance — apply it
to any new game unless told otherwise:

1. **KEY0** (the onboard button) — the main/most-frequent action button
   (fire, rotate, primary interact — whatever that is per game).
2. **BTN1 / BTN2** (the two hand-wired buttons) — secondary actions,
   whatever else a game needs.
3. **Thumbstick push-switch** — tertiary.
4. **ENC_SW** (the encoder's push-switch) — last resort, reserved for
   genuinely infrequent actions, since it's physically awkward to press
   without disturbing the rotation.

Movement/aim is governed separately (see the thumbstick-vs-encoder
paragraph below) — this list is about discrete press **actions**, not the
continuous move axis.

Since KEY0 is now the main action button, it can no longer *also* be the
"instant exit to menu" button the way it used to be everywhere — that
would fire both at once. Every game now uses `GameEntry.wantsExit`
(`apps.h`) to relocate the exit: **KEY0 only returns to the menu from that
game's pause screen** (added to Arkanoid/Tetris/Bust-a-Move for exactly
this reason — Tetris and Bust-a-Move didn't have a pause screen before;
see `tetrisWantsExit()`/`bustamoveWantsExit()`/`arkanoidWantsExit()` /
`rtypeWantsExit()`), plus from GAME_OVER for Tetris/Bust-a-Move specifically
(no conflicting KEY0 use there, so no need to force a pause detour first).
Pausing itself: **Arkanoid** kept its original tap-to-pause (ENC_SW edge,
unchanged); **Tetris and Bust-a-Move** are new pause screens using
ENC_SW **held** ~500ms (`PAUSE_HOLD_MS`), matching R-Type's convention.

**Menu is the one exception** to the whole scheme above: navigate with
the thumbstick *or* the encoder (both scroll), confirm with KEY0 *or*
ENC_SW (both work) — BTN1/BTN2 aren't used at the menu at all. Don't
apply the priority list there.

Per-game specifics:
- **Arkanoid**: KEY0 = launch ball. BTN1/BTN2 still nudge the paddle
  (held) as a secondary way to move it. ENC_SW tap = pause/resume/
  restart (unchanged from before), and now also exits from the pause
  screen via KEY0.
- **Tetris**: KEY0 = rotate. BTN1 = soft drop (hold), BTN2 = hard drop.
  ENC_SW held = pause; ENC_SW tap = resume (while paused) or restart
  (from GAME_OVER, unchanged).
- **Bust-a-Move**: KEY0 = fire. BTN1 = restart from GAME_OVER (unchanged,
  wasn't touched). ENC_SW held = pause; ENC_SW tap = resume.
- **R-Type**: already matched this convention before the others were
  updated to it — KEY0 = fire (hold to charge), ENC_SW tap = weapon
  cycle, ENC_SW held = pause. No changes needed here.
- **Bubble Bobble**: KEY0 (or BTN2) = blow bubble — the original arcade's
  one action button. BTN1 (or thumbstick pushed up) = jump, mirroring the
  original's real joystick-up-to-jump control. ENC_SW held = pause;
  ENC_SW tap = resume, or restart from GAME_OVER.

Movement/aim axis (separate from the button priority above): primary is
the thumbstick's X, **except Arkanoid, which takes both the thumbstick AND
the encoder** (additive — either one moves the paddle). Tetris and
Bust-a-Move's encoder rotation was fully replaced for movement — it does
nothing in either now; this was a deliberate per-game choice (Arkanoid's
paddle benefits from the encoder's fine analog feel enough to justify
keeping both; the other two didn't need two inputs doing the same job).
- **Tetris**: DAS-style (delayed auto-shift) — crossing the stick's
  trigger threshold shifts the piece once immediately, then repeats at a
  fixed interval for as long as it's held past that threshold, same feel
  as holding a D-pad direction in classic Tetris (`THUMB_SHIFT_TRIGGER`/
  `THUMB_SHIFT_REPEAT_MS` in `tetris.cpp`).
- **Bust-a-Move**: direct analog angular velocity (`updateAim()` in
  `bustamove.cpp`) — simpler than the encoder's tick-timing acceleration
  curve since the stick already gives a continuous deflection, not
  discrete ticks.
- **Arkanoid**: thumbstick X added straight onto the existing encoder/
  BTN1/BTN2 paddle control in `updatePaddle()` — purely additive.
- **Bubble Bobble**: thumbstick X and encoder rotation both walk Bub
  left/right (additive, like Arkanoid) — BTN1/BTN2 are needed for
  jump/bubble instead, so there was no button budget left to dedicate a
  pair to paddle-style nudging the way Arkanoid does.

### R-Type

A look-and-feel homage to *R-Type III: The Third Lightning* (SNES, 1993),
Stage 1 ("Catapult Dimension" — drifting space junk, ending in its boss
"Guard Ray") only; stages 2-6 are future work.

**It's a vertical shooter, in portrait** — same orientation as every other
game (no `hardwareSetOrientation()` call needed; `GameEntry.landscape` is
`false` for it). It started out landscape/horizontal like the real R-Type,
but was switched to portrait/vertical so the thumbstick — mounted for
portrait use — could drive it naturally. Concretely: the ship flies "up"
the screen, confined to a limited-range lower zone (`SHIP_Y_MIN/MAX`);
enemies, debris, and the boss spawn near the top and drift down; player
bullets fire upward (`vy < 0`); X is now the full-width lateral dodge axis.
The landscape→portrait switch was a straightforward axis relabel (X
"forward" became Y "forward", old Y "lateral" became new X "lateral") — see
`hardware.h`'s `hardwareSetOrientation()` and Gotcha #8 if a future game
ever needs the landscape path again; the mechanism is still there, just
unused right now.

Controls are deliberately different from the other three, since a shmup
needs full 2D movement plus a dedicated fire button, and this board only
has 2 hand-wired buttons + 1 encoder + 1 uncomfortable push-button + KEY0
+ the thumbstick:

- **Encoder rotate** = ship X (left/right, full width).
- **BTN1 held** = ship down, **BTN2 held** = ship up (limited range, near
  the bottom of the screen).
- **Thumbstick**: both axes drive ship X/Y additively on top of the
  encoder/buttons above — nothing above was removed, the stick is just
  another way to move at the same time. Both axes were confirmed inverted
  from the physical wiring during testing; see `THUMB_X_SIGN`/
  `THUMB_Y_SIGN` in `updateShipMovement()` (`rtype.cpp`) if either ever
  needs re-flipping (e.g. after rewiring).
- **KEY0 = fire**: hold to charge, release to fire (quick tap = weak shot,
  released after a longer hold = a piercing charged beam). This means
  **KEY0 does not return to the menu while R-Type is playing** — it's
  repurposed as the fire button instead (see `GameEntry.wantsExit` below).
- **ENC_SW tap** = cycle weapon type. **ENC_SW held (~500ms)** = pause.
  From the pause screen: KEY0 tap exits to the menu, ENC_SW tap resumes.
- The Force Pod is always docked just above the nose and auto-fires on its
  own — no manual dock/launch control in this phase.

Stage 1 ends with a boss fight against **Guard Ray**: it patrols the upper
part of the screen lobbing 3-way spread shots down at the player,
periodically "settles" lower (closer to the player) to fire homing
missiles instead, and splits into 3 weaker copies (spread out laterally)
at half HP — simplified from the real SNES boss (no background
depth-scaling), same beats. Beating it (or the fight ending) shows a
"STAGE 1 CLEAR" banner and auto-returns to the menu.

**Fixed a real bug behind a reported "rolling flicker"**: `eraseAndDrawStars()`
existed but was never actually called from either per-frame update loop
(`updateWorld()`/`updateBossFight()`) — the starfield was drawn once at
launch and never touched again. Every enemy/debris/bullet erase-rect that
happened to pass over a star's position wiped it out permanently (nothing
ever redrew it), so a trail of missing stars followed every moving object
down the screen — a "wave" of stars vanishing in sync with object motion,
which reads as a rolling flicker. Fixed by actually calling
`eraseAndDrawStars(dt)` in both loops. Also made `drawHUD()` skip its
full-strip redraw when score/weapon/lives haven't changed (`force=false`
at the two per-frame call sites; every other call site keeps the default
`force=true` so a HUD redraw is never accidentally skipped right after a
full screen wipe) — it was unconditionally clearing and redrawing that
strip every single frame regardless, which didn't help.

**`GameEntry.wantsExit`** (`apps.h`): the mechanism that lets a game take
over KEY0. It's `nullptr` for the other 3 games, which keeps today's
behavior (`main.cpp` exits to the menu the instant `in.key0Edge` is true,
without the game ever seeing it). R-Type sets it to `rtypeWantsExit`, which
`main.cpp` calls every frame instead — it only returns true from the pause
screen (KEY0 tap) or automatically after the stage-clear banner finishes.

### Bubble Bobble

Phase 1: single-player Bub, one enemy type (Zen-Chan), 3 platform layouts.
Deferred to later: 2-player/Bob, EXTEND letters, special water/fire/
lightning bubbles, additional enemy types, and the boss.

This game went through 3 full passes before landing on the approach
below — worth knowing the history since it explains some of the more
unusual-looking code/comments still in `bubblebobble.cpp`:
1. Hand-authored pixel art + invented enemy AI, based on text research
   and a couple of screenshots. Looked and behaved generically enough
   that the user asked for a redo against real reference material.
2. Hand-authored art redrawn against real sprite-sheet rips and gameplay
   screenshots (closer, but still not accurate enough per feedback), plus
   a first attempt at "smarter" enemy AI (confine each enemy to pacing
   its own platform) that turned out to be *wrong* — real Zen-Chan walks
   off ledges and falls, it doesn't pace one shelf. Around this point
   erasing a moving sprite's old position was also changed to restore any
   platform/wall tile it overlapped (fixing bubbles/walls getting
   permanently deleted), which introduced a *worse* bug: with several
   entities moving in the same frame, one entity's restore-on-erase could
   redraw a background tile on top of another entity that had already
   drawn there earlier that frame — a stomp, worse the more entities were
   on screen, which read as constant flicker plus a real slowdown (the
   restore also re-scanned every tile on every single erase call).
3. **The user pointed at [JulianRijken/BubbleBobble](https://github.com/JulianRijken/BubbleBobble)
   ("a fresh take on the original, taking inspiration from both the NES
   and Arcade versions") and asked for a real port instead of more
   guessing.** That project's full engine (Box2D physics, a GameObject/
   Component/state-machine architecture, built for desktop/web) can't
   literally run on a 320KB-RAM microcontroller with no OS drawing
   directly to SPI with no framebuffer — so this is a *logic and asset*
   port: sprites cropped directly from its Asset PNGs, level layouts
   decoded from its actual level-data image, and enemy AI/bubble-timing
   rules read out of its source and reimplemented in this project's
   simpler kinematic style, not a literal architecture transplant. GPL-3.0;
   see `tools/bb_ref_assets/SOURCE.md` for attribution. The user also
   pointed at [PixelRoot32](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine),
   a real ESP32-native 2D engine with exactly the dirty-region renderer
   and tile/one-way-platform physics this game needed — evaluated and
   deliberately not adopted *yet*: it wants to own the whole scene/main
   loop and requires project-wide C++17 + `-fno-exceptions`, which is a
   much bigger, riskier swap than fixing the existing per-entity
   renderer turned out to need. **If this build still feels wrong after
   testing, that's the agreed fallback — see this game's plan file.**

**The actual rendering fix** (`bubblebobble.cpp`): stopped trying to patch
the background from inside each entity's own erase call. Each entity now
only erases its own old position (plain background fill, `eraseRect()`)
if it moved; then, once per frame, after every entity has had its turn,
`redrawWorld()` draws the platform/wall layer and then every currently-
active entity on top of it, in one uninterrupted pass. Since that
redraw-everything pass happens *after* all erasing for the frame is
done, one entity's draw can never land on top of another's before that
other entity gets its own turn — the stomping/flicker is structurally
not possible, and it's less total drawing work per frame than the old
per-entity restore-and-rescan besides.

**Physics** (new to this codebase — no earlier game needed gravity or
jumping): `applyVerticalPhysics()` is shared by the player, enemies, and
food. It applies constant gravity, then (only while falling, `vy > 0`)
scans the level's platform rows for the first one crossed that frame with
a solid tile under the entity's X range, and lands on it. Platforms only
ever stop a *falling* entity from above; jumping up passes straight
through, matching the original's one-way-ledge feel. Because gravity
nudges `vy` slightly positive every frame even while standing still, the
same landing scan re-fires and re-confirms "still standing" each frame —
no separate `isGrounded()` check was needed. Falling fully off the bottom
of the playfield wraps back to the top (**vertical** wraparound,
confirmed via research — the original wraps top/bottom, not left/right,
the opposite of most platformers); the left/right screen edges are solid.

**Level layouts**: ASCII-art platform rows (`'1'`=solid, `'.'`=gap), same
hand-authoring convenience as Arkanoid's `LAYOUT_*` arrays, but the 3
layouts themselves are transcribed from the real level data in the
reference project's `Assets/Levels.png` (each real level is a 32x28-tile
color-coded image — blue=wall, green=platform, decoded this session) —
reproduced at this game's coarser 15x8 grid (structure matched, not a
pixel-exact copy, the two grids don't share a scale). One real level had
vertical maze walls this engine's horizontal-only one-way platforms can't
reproduce, so that one's approximated as stacked horizontal segments
instead of its original hollow-room shape.

**Controls**: thumbstick X + encoder both walk (additive, like Arkanoid);
thumbstick pushed up *or* BTN1 jumps — the stick-up trigger is
edge-latched the same way `menu.cpp` latches a thumbstick push for menu
navigation, so holding it doesn't repeat-jump. KEY0/BTN2 blow a bubble in
the direction Bub is facing. See the button-priority write-up above for
why KEY0 = bubble and BTN1 = jump.

**Bubble mechanic**, timings matched to the reference's `CaptureBubble`
constants: a blown bubble drifts forward for `BUBBLE_TRAVEL_TIME` (0.9s,
their `DURATION_BEFORE_FLOATING`) — during this window it can catch an
enemy but the player can't pop it yet — then switches to floating
(`BUB_FLOAT`), poppable from then on, until it's popped, hits the
playfield ceiling and hovers, or reaches `BUBBLE_LIFETIME_MS` (8s, their
`MAX_TIME_TILL_POP`) with nothing having popped it. A caught enemy is
hidden from the world and drawn as an overlay inside the bubble sprite
(no second "occupied bubble" bitmap needed). Popping a bubble with an
enemy in it (walk or jump into it) destroys the enemy for points and
drops a food item; popping also chain-pops any other trapped bubble
within `CHAIN_RADIUS`. An enemy not popped in time breaks free instead,
angrier (`ANGRY_MULT` speed) for the rest of the level. Food is
Watermelon (100pts) / Fries (200pts) — the reference's actual 2 pickup
types and point values, not invented ones — with a 10s lifetime
(`Pickup::LIFE_TIME`).

Touching an active (untrapped) enemy costs a life, same as every other
game's `loseLife()` pattern, with a brief blinking invulnerability window
on respawn (same idea as R-Type's `invulnerable`/`shipBlinkOn`).

**Enemy AI**, ported from the reference's `ZenChanBehaviour` (previously
invented/wrong — see the history above): walks until a wall-turn check
(with a 1s cooldown so it doesn't jitter right at the wall) flips its
direction; otherwise it walks straight off the end of its own platform
and falls to whatever's below, same as a normal platformer enemy, rather
than pacing one shelf forever. Every 2-3.5s (randomized) it re-biases
toward the player's side even without hitting a wall. It only climbs
toward the player specifically — not a random idle hop — when it's
directly below them, already walking their direction, there's a platform
within jump reach overhead, and it's been at least 1.0-1.5s (randomized)
since its last jump.

### Bitmap sprites

**Every game's sprites are real bitmaps now** — none of the 5 games draws
its characters/objects with live `fillTriangle`/`fillCircle` vector calls
anymore (menu chrome, HUD text, particles, and simple UI overlays still
do — bitmaps replaced *character/object* art specifically, not everything
on screen). Each game has its own `tools/make_<name>_sprites.py` script
that hand-authors pixel art with PIL (there's no image-generation model
available in this environment — this is pixel art placed programmatically:
shapes drawn at 4x scale for clean proportions, downscaled with box
filtering for smooth shading, sometimes a few crisp pixels touched up by
hand afterward for readability) and emits a `src/<name>_sprites.h`:
`const uint16_t <PREFIX>_SPR_<NAME>[]` RGB565 arrays plus `_W`/`_H` size
constants, read straight from flash via `tft->drawRGBBitmap()`. Every
sprite's "empty" pixels are pre-baked to that game's background color, so
a plain rectangular blit reads as a transparent sprite with no per-pixel
masking needed — the same erase-rect-then-redraw technique every game
already used, just drawing a bitmap instead of shapes at the end. Preview
PNGs land in `tools/sprite_previews/<name>/` for a quick look without
flashing.

Research for each game's look came from WebSearch (a research subagent,
specifically) before drawing anything, at the user's request — see each
script's module docstring for citations/details and what was faithfully
reproduced vs. adapted:

- **Arkanoid** (`make_arkanoid_sprites.py`): the paddle is the actual
  "Vaus" from the 1986 original — a banded sci-fi ship (red-flank 45°
  bounce zones, silver/white center near-vertical bounce zone, blue hull),
  not a plain bar. Since paddle width changes continuously with
  power-ups (26-70px), it's built from 3 slices (left cap + repeating
  middle tile + right cap) composited at runtime in `drawPaddle()`, not
  one fixed-width bitmap. Bricks use the real fixed color legend, plus
  distinct silver (multi-hit, riveted metal panel) and steel/gold
  (indestructible) variants. Power-up capsules are remapped to the
  closest authentic letter+color (see the docstring for which one, N,
  has no real Arkanoid equivalent and was invented).
- **Tetris** (`make_tetris_sprites.py`): uses the modern "Guideline"
  7-color scheme (cyan/yellow/purple/green/red/blue/orange), which is
  what's actually recognized as "the Tetris colors" today even though the
  real 1989 NES cartridge cycled a 3-color palette every 10 levels rather
  than using fixed per-shape colors. Combined with NES-style beveled
  block-cell rendering (solid fill + a lighter inset "gem" facet) for the
  authentic retro texture. Two sizes: the main board/falling-piece cell
  and a smaller one for the "next piece" preview.
- **Bust-a-Move** (`make_bustamove_sprites.py`): bubbles are solid-hue
  glossy spheres with an upper-left specular highlight and a thin dark
  outline — no printed numbers/symbols, matching the 1994 original, where
  color alone distinguishes bubbles. The existing vector bubbles were
  already close to this look, so this was mostly a bitmap-ification pass
  with a bit more shading depth than cheap vector circles could manage,
  not a redesign. (The turret itself was deliberately left generic/
  mechanical rather than adding the original's Bub/Bob dinosaur
  characters — those are a specific licensed IP, not just a generic
  genre convention.)
- **R-Type** (`make_rtype_sprites.py`): ship, Force Pod, enemies, Guard
  Ray (both sizes), bullets, capsules, and debris. Debris switched from
  continuously-random rectangle sizes to 3 fixed hand-drawn variants
  (`RT_SPR_DEBRIS_0/1/2`), since a bitmap sprite needs a fixed size —
  picked at random on spawn instead. Every sprite was originally drawn
  "facing right" (landscape/horizontal ship). When the game switched to
  portrait/vertical, direction-dependent sprites (ship, drifter, turret,
  and all 3 player bullets) needed to face "up" instead — done by
  rotating the already-drawn art 90° counter-clockwise
  (`save_sprite_rotated()` in the script, `Image.ROTATE_90`) rather than
  hand-redrawing anything: since "right" uniformly became "up" for the
  whole game (not just the ship), rotating each sprite's pixels the same
  way automatically keeps eyes/barrels/muzzles pointed the right way
  relative to their new direction of travel. Sprites with no inherent
  direction (Force Pod, Guard Ray, capsules, debris, the enemy bullet)
  were left unrotated.
- **Bubble Bobble** (`make_bubblebobble_sprites.py`): the only game whose
  sprites are **cropped from a real source project's assets** rather than
  hand-authored with PIL shapes — two hand-drawn passes (first from text
  research, then redrawn against reference screenshots/sheets) still
  didn't read as accurate enough, so at the user's request Bub, Zen-Chan,
  the capture-bubble projectile, and the 2 food pickups are cropped
  directly from `tools/bb_ref_assets/` (copied from
  [JulianRijken/BubbleBobble](https://github.com/JulianRijken/BubbleBobble),
  GPL-3.0 — see `SOURCE.md` there for the exact frame coordinates used and
  attribution). Getting the coordinates right took a few tries: an initial
  guess at uniform 32x32 grids for both the enemy sheet and the item sheet
  was wrong (both are actually 16x16 per icon, some of the item icons not
  even on a clean grid — found by cropping with a pixel-grid overlay and
  looking, not by assuming). Bub (24x16 native) and Zen-Chan (16x16
  native) are upscaled 2x *uniformly*, preserving their relative native
  proportions rather than picking a different scale per sprite. Platform
  and wall tiles are still hand-drawn (validated earlier against real NES
  screenshots, never part of the "looks nothing like it" feedback that
  prompted this). Left/right-facing versions are still produced by
  mirroring (`ImageOps.mirror`) rather than needing separate source frames.

A pattern worth reusing for any future game's conversion: once a game's
drawing functions take a **type/color index** (piece type, brick row,
bubble color, etc.) instead of a raw `uint16_t` color value, the old
`uint16_t` palette variables that fed those draw calls usually become
fully dead code — check for and remove them (this happened in all three
of Arkanoid/Tetris/Bust-a-Move) rather than leaving unused color tables
around. Keep any that are still legitimately used elsewhere (e.g.
particle-effect tinting, which stays flat-color "juice" rather than
sprites in every game).

### Debugging over serial

Since this display has no MISO/SDO wire, there's no way to read pixels
back off the real panel — but two debug facilities in `main.cpp` make it
possible to drive and *see* the console entirely over the USB-serial
connection, without touching the physical controls:

- **Input injection** (`readInput()` debug switch): send single ASCII
  bytes over serial to simulate controls — `a`/`d` = encoder left/right
  one tick, `w` = R3 (encoder push), `s` = A (KEY0), `1`/`2` = RB/LB,
  `l` = thumbstick click (L3). Purely additive on top of real hardware
  input; harmless when nothing is sent. Lowercase is a one-frame tap,
  matching a real button's edge — fine for menu navigation or a quick
  fire. Real hardware buttons stay held across many frames on their own,
  but an injected character only asserts for the one frame it's read on,
  which isn't enough to test a hold-*duration* mechanic (R-Type's charge
  shot, any game's R3 pause-hold, or the global L3 2-second exit). For
  that, **uppercase `S`/`W`/`L` toggle a sustained hold latch** for
  A/R3/thumbstick-click — send it once to start "holding," send the same
  letter again to release.
- **Force-to-menu** (`m`): immediately resets to the game-select menu with
  selection index 0, regardless of current state. Added because opening a
  *new* serial connection resets the board via DTR/RTS **inconsistently**
  — whether it fires depends on the line state the previous connection
  left the port in, not something a script controls — so waiting for a
  boot banner as a sync point isn't reliable (it hangs on the common case
  where no reset happens). `m` gives remote test scripts a real,
  deterministic sync point instead. See `tools/rig.py`.
- **Screenshot capture** (`captureScreenshot()`): send `p`. The console
  renders whatever it would currently show into a temporary offscreen
  `Canvas8` (a minimal custom `Adafruit_GFX` subclass, RGB332, 1
  byte/pixel — full resolution but 3-3-2 bit color) instead of the real
  display, then dumps it raw over Serial as `SCR_BEGIN <w> <h> <bytes>
  RGB332\n<raw bytes>\nSCR_END\n`. A real `GFXcanvas16` (2 bytes/pixel,
  153600 bytes) reliably fails to allocate on this board — `freeHeap` is
  ~317KB but `ESP.getMaxAllocHeap()` (largest single contiguous block) is
  only ~110KB, a platform-level fragmentation ceiling, not something this
  sketch causes — hence the smaller custom canvas. Every game exposes a
  `renderSnapshot()` that redraws its current state (including in-flight
  balls/pieces) from its own state variables, so the capture always
  reflects the live game.

Both were essential for finding and fixing several real bugs in Bust-a-Move
(a rendering desync after piece/bubble locks, a redraw region that wiped
the shooter's barrel graphic every shot) without anyone needing to look at
the physical screen.

`tools/screenshot.py <output.png> [port]` sends `p`, reads the response,
and saves a PNG — `python3 tools/screenshot.py shot.png` (needs `pyserial`
and `Pillow`; defaults to `/dev/ttyUSB0`).

`tools/rig.py` is a small importable helper module (`open_console()`,
`nav_to_game()`, `screenshot()`, `tap()`, `hold_latch()`) for writing
one-off remote test scripts without re-deriving the serial protocol and
the `m`-based sync point each time — see its docstring.

## Build / flash

```
pio run                                   # build
pio run -t upload --upload-port /dev/ttyUSB0   # flash
pio device monitor -b 115200                    # serial log
```

Uses `adafruit/Adafruit GFX Library`, `adafruit/Adafruit ST7735 and
ST7789 Library`, and the built-in `Preferences` (NVS-backed per-game high
scores) — see `platformio.ini`. `upload_speed` is pinned to 115200 for
reliability over the CP2102 USB-serial bridge on this board.
