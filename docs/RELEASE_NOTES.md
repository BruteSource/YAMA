# YAMA v0.1.0-beta — first public beta

**Y**et **A**nother **M**icrocontroller **A**rcade: a 7-game handheld arcade console on a custom ESP32 + 2.0" ST7789 TFT + rotary encoder board, running on the [PixelRoot32](https://github.com/PixelRoot32-Game-Engine/PixelRoot32-Game-Engine) game engine.

This is a **beta**: all 7 games are playable end-to-end, but numeric tuning (speed, difficulty, timing) is a first pass and not yet fully balanced. See "Known issues" below.

**This is a work-in-progress hobby project, not a final release — just for fun.** It was **vibe-coded**: built collaboratively with an AI coding assistant, using several other open-source projects as references (see each game's credit below) rather than written from scratch by hand.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/menu.gif" width="240">

## Flashing this release

This release includes `YAMA-v0.1.0-beta-full.bin`, a single pre-built image containing the bootloader, partition table, and firmware — flash it directly to an ESP32 with [esptool](https://github.com/espressif/esptool):

```
pip install esptool
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 115200 write_flash 0x0 YAMA-v0.1.0-beta-full.bin
```

Replace `/dev/ttyUSB0` with your board's serial port (e.g. `COM3` on Windows). This assumes the same board this project targets — a bare ESP32 dev module (`esp32dev` in PlatformIO), 4MB flash, DIO mode. If you're building for different hardware, build from source instead (see the main [README](README.md#build--flash)).

## Platform features

- Shared 7-game arcade menu with rainbow-animated selection and scrolling viewport
- Backlight brightness control (hardware PWM via a dedicated power switch)
- Mute toggle, idle auto light-sleep (button-wake, resumes exactly where you left off), and a manually-triggered deep-sleep menu entry for long downtime
- Hard reset combo
- High scores persisted per-game across power cycles

## Known issues / not yet final

- Numeric tuning (speed, difficulty curves, enemy timing) across all 7 games is a first pass, not yet fully playtested for balance
- Dig Dug's Drake enemy doesn't have its fire-breath attack yet (contact-only for now)
- A low-volume noise from the piezo buzzer has been traced to the battery boost converter, audible only during deep sleep on battery power — cosmetic, doesn't affect gameplay, hardware fix pending
- The board's physical case is still in progress

## Games

### Bubble Bobble
Platformer bubble-trap-and-pop action across a multi-screen level.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/bubblebobble_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/bubblebobble_2.png" width="240">

### Pac-Man
Maze chase with full ghost AI (scatter/chase/frightened modes), power pellets, and fruit bonuses.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/pacman_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/pacman_2.png" width="240">

### Galaga
Fixed-shooter with diving enemy formations and the tractor-beam capture/rescue mechanic.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/galaga_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/galaga_2.png" width="240">

### Arkanoid
Brick-breaking paddle-and-ball action with powerups and a boss fight.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/arkanoid_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/arkanoid_2.png" width="240">

### Block Stack
An original falling-block puzzle game (hold piece, next-piece preview, line clears).

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/blockstack_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/blockstack_2.png" width="240">

### R-Type
Vertical scrolling shmup: charge-shot weapon (cycle between normal/spread/laser), an auto-firing support drone, drifting enemies and turrets, and a 3-phase boss that splits at low HP.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/rtype_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/rtype_2.png" width="240">

### Dig Dug
Newest addition: tunnel through dirt, inflate enemies with a pump attack until they burst, or lure them under a rock and dig out its support to crush them.

<img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/digdug_1.png" width="240"> <img src="https://raw.githubusercontent.com/BruteSource/YAMA/main/docs/screenshots/digdug_2.png" width="240">

## Hardware

Custom ESP32 board: 2.0" 240x320 ST7789 SPI TFT + EC11 rotary encoder (with onboard button) + 2 hand-wired tactile buttons + analog thumbstick + piezo buzzer, LiPo battery powered through a boost converter. Full wiring reference in the main [README](README.md).
