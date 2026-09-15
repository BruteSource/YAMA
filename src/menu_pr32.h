// Startup game-select menu, PixelRoot32-native (see
// ~/.claude/plans/goofy-dancing-locket.md). Replaces the old
// src/menu.cpp (raw Adafruit_GFX + function-pointer GameEntry registry —
// see games_registry.cpp), which is architecturally incompatible with
// PixelRoot32 Scenes and stays excluded from this build. This one is a
// plain Scene: main.cpp holds it (and every game's Scene) as static
// objects and calls engine.setScene() to switch between them — no
// registry needed, PixelRoot32's own SceneManager already supports
// calling setScene() again at any time.
#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>
#include "hardware.h"
#include "menu_pr32_audio.h"

namespace menu_pr32 {

class MenuScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;
    void processTouchEvents(pixelroot32::input::TouchEvent*, uint8_t) override {}
    void onUnconsumedTouchEvent(const pixelroot32::input::TouchEvent&) override {}

    void setInput(const Pr32Input& in) { pendingInput = in; }

    // -1 until the user confirms a selection on an available entry;
    // main.cpp checks this once per loop() after feeding input, switches
    // to the corresponding game Scene if so, and calls clearSelection()
    // afterward so the menu starts fresh next time it's shown again.
    int getSelectedGame() const { return confirmedIndex; }
    void clearSelection() { confirmedIndex = -1; }

private:
    Pr32Input pendingInput;
    int selection = 0;
    int encAccum = 0;
    int confirmedIndex = -1;
    // Thumbstick up/down as a secondary way to move the selection,
    // matching the old (pre-PixelRoot32) menu.cpp's own thumbstick
    // support — the new PixelRoot32 menu had never actually ported that
    // over (encoder-only until now), reported live as "moving the
    // thumbstick in the menu doesn't move the selection." Latched (one
    // step per push past the trigger threshold, released below a lower
    // threshold) rather than continuous, same idiom as the old menu.
    bool thumbMoveLatched = false;
    unsigned long lastThumbMoveMs = 0;
    // Continuous diagonal scroll position for the tiled background (see
    // menu_pr32.cpp's draw()) — kept wrapped to the tile size every frame
    // so this never grows unbounded across a long time sitting at the menu.
    // Background scroll animation is currently disabled (static black
    // background instead) — these fields are unused dead weight left in
    // place for now rather than ripped out; see update()'s comment.
    float scrollX = 0.0f;
    float scrollY = 0.0f;

    // Animated logo: cycles through menu_pr32_sprites::LOGO_FRAMES (each
    // a phase-shifted rainbow bitmap — see make_menu_pr32_sprites.py) at
    // a fixed rate for a continuously sweeping effect.
    int logoFrame = 0;
    float logoFrameTimer = 0.0f;

    // Animated background: an old-arcade-diagnostic-screen pastiche
    // (crosshatch, color bars, checkerboard, RAM/ROM check text) cycling
    // behind the menu — see drawBootScreenBackground() in menu_pr32.cpp.
    // Generic self-test-screen vocabulary shared across countless real
    // arcade boards (crosshatch/color-bars/checksums), not any single
    // game's proprietary screen — researched for authenticity, not
    // copied from one source.
    int bgPhase = 0;
    float bgPhaseTimer = 0.0f;
    void drawBootScreenBackground(pixelroot32::graphics::Renderer& renderer);

    // Scrolling list "viewport": only kVisibleRows entries are drawn at
    // once (the panel used to draw all of them fixed-height, which
    // overflowed the screen once a 5th game — Block Stack — was added,
    // reported live as "getting two long and cut off"). viewportTop is
    // the index of the first visible entry, scrolled by the minimum
    // amount needed to keep `selection` in view, same idiom as any
    // standard scrolling list.
    int viewportTop = 0;

    // Brightness control: thumbstick LEFT/RIGHT (X axis — Y is already
    // taken by game selection), continuous rather than latched/stepped
    // since brightness benefits from fine control. Persisted via
    // hardwareSaveBrightness() once the stick releases back toward
    // center, not on every frame while adjusting (NVS writes are wear-
    // limited). brightnessIndicatorUntilMs keeps the on-screen
    // brightness readout visible for a short moment after the last
    // adjustment instead of popping in/out every single frame.
    bool brightnessAdjusting = false;
    unsigned long brightnessIndicatorUntilMs = 0;

    // Sound: `sfx` (move/confirm blips) takes priority — `tune`'s own
    // update() only runs (and only re-triggers itself when it loops)
    // while `sfx` isn't playing, so a blip briefly interrupts the
    // background tune rather than fighting it for the single buzzer
    // channel. See menu_pr32_audio.h for the note tables.
    menu_pr32_audio::NoteSequencer sfx;
    menu_pr32_audio::NoteSequencer tune;
};

} // namespace menu_pr32
