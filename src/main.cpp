// Multi-game PixelRoot32 arcade (see ~/.claude/plans/goofy-dancing-locket.md):
// a startup menu Scene (menu_pr32) switches to whichever game Scene is
// selected via engine.setScene(), and each game Scene can request a
// return to the menu (see BubbleBobbleScene::wantsReturnToMenu()).
// Deliberately only ONE display-driver library is active (PixelRoot32's
// own TFT_eSPI-based renderer) — the OLD Adafruit_GFX menu/4 old games
// are set aside for now, not deleted, just excluded from this build (see
// platformio.ini's src_filter). Pac-Man will be the 3rd Scene this hosts
// once it exists.
#include <Arduino.h>
#include <new>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <core/Engine.h>
#include <graphics/Renderer.h>
#include <graphics/DisplayConfig.h>
#include <input/InputConfig.h>

#include "hardware.h"
#include "bubblebobble_pr32.h"
#include "menu_pr32.h"
#include "pacman_pr32.h"
#include "galaga_pr32.h"
#include "arkanoid_pr32.h"
#include "blockstack_pr32.h"
#include "rtype_pr32.h"
#include "digdug_pr32.h"

namespace pr32 = pixelroot32;

namespace {

// Empty — real hardware input (encoder/buttons/thumbstick) is read
// ourselves via hardware.cpp/Button, not through PixelRoot32's InputManager
// (which only polls raw GPIO — no encoder/analog-axis support, and no way
// for this project's serial debug-injection protocol to fake a press
// through it). See Pr32Input in hardware.h (shared by every Scene).
pr32::input::InputConfig inputConfig{};

pr32::graphics::DisplayConfig displayConfig(
    pr32::graphics::DisplayType::ST7789,
    0,   // rotation — unverified yet, adjust once a human looks at the panel
    PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT, // logical == physical: no scaling
    0, 0
);

pr32::core::Engine engine(displayConfig, inputConfig);
// The menu is always resident (cheap, and it's the fallback every game
// returns to), but each GAME scene only occupies memory while it's the
// one being played, constructed the moment it's selected and destroyed
// the moment its player returns to the menu — see the ActiveApp::Menu
// case and returnToMenu() below. Previously these were all static
// globals, meaning every game's worst-case static memory was reserved
// simultaneously for the program's entire lifetime even though only one
// is ever active — the recurring "shrink an unrelated buffer to fit a
// new game" tax (see bubblebobble_pr32.cpp's sceneBuffer history) was
// really just that design's bill coming due.
//
// An earlier version of this fix used plain `new`/`delete` per scene —
// reverted after it crash-looped selecting Galaga specifically
// (`abort()` inside `operator new`, confirmed via
// heap_caps_get_largest_free_block(MALLOC_CAP_8BIT): 18420 bytes
// available in the largest contiguous block vs. GalagaScene's own
// sizeof() of 19288 — genuine heap FRAGMENTATION from other static
// allocations made earlier at boot, not a total-memory shortage. Since
// Galaga is comfortably the largest of the four scenes, it was the only
// one that ever hit this.
//
// Fixed by placement-new into one shared, fixed-size STATIC buffer
// (sized for the largest scene) instead of the general heap — this
// still only ever holds one scene's memory at a time (the original
// goal), but a static buffer is a single contiguous block fixed at LINK
// time, so it can never fragment against anything else. Destruction
// calls the destructor explicitly (NOT `delete`, which would try to
// `free()` memory that was never `malloc`'d).
constexpr size_t kSceneStorageSize = 20480;  // >= the largest scene's
                                              // sizeof() (Galaga, 19288
                                              // bytes) with headroom;
                                              // the static_asserts below
                                              // catch this falling
                                              // behind if a scene grows.
alignas(16) uint8_t sceneStorage[kSceneStorageSize];
static_assert(sizeof(bubblebobble_pr32::BubbleBobbleScene) <= kSceneStorageSize, "grow kSceneStorageSize");
static_assert(sizeof(pacman_pr32::PacManScene) <= kSceneStorageSize, "grow kSceneStorageSize");
static_assert(sizeof(galaga_pr32::GalagaScene) <= kSceneStorageSize, "grow kSceneStorageSize");
static_assert(sizeof(arkanoid_pr32::ArkanoidScene) <= kSceneStorageSize, "grow kSceneStorageSize");
static_assert(sizeof(blockstack_pr32::BlockStackScene) <= kSceneStorageSize, "grow kSceneStorageSize");
static_assert(sizeof(rtype_pr32::RTypeScene) <= kSceneStorageSize, "grow kSceneStorageSize");
static_assert(sizeof(digdug_pr32::DigDugScene) <= kSceneStorageSize, "grow kSceneStorageSize");

menu_pr32::MenuScene menuScene;
bubblebobble_pr32::BubbleBobbleScene* bubbleBobbleScene = nullptr;
pacman_pr32::PacManScene* pacManScene = nullptr;
galaga_pr32::GalagaScene* galagaScene = nullptr;
arkanoid_pr32::ArkanoidScene* arkanoidScene = nullptr;
blockstack_pr32::BlockStackScene* blockStackScene = nullptr;
rtype_pr32::RTypeScene* rtypeScene = nullptr;
digdug_pr32::DigDugScene* digdugScene = nullptr;

// Which Scene is currently hosting input/the return-to-menu check —
// main.cpp's own bookkeeping, since PixelRoot32 has no "ask the engine
// which Scene is active and what game that corresponds to" concept
// (engine.getCurrentScene() returns a bare Scene*, not something typed
// enough to call setInput()/wantsReturnToMenu() on without already
// knowing which one it is).
enum class ActiveApp { Menu, BubbleBobble, PacMan, Galaga, Arkanoid, BlockStack, RType, DigDug };
ActiveApp activeApp = ActiveApp::Menu;

// Switching scenes must force a full redraw: PixelRoot32's dirty-region
// optimization (PIXELROOT32_ENABLE_DIRTY_REGIONS) only redraws cells that
// changed since the LAST presented frame, with no awareness that "the
// active Scene changed" means literally everything is new. Without this,
// returning from Bubble Bobble to the menu left its last frame's stale
// pixels (its blue level background, etc.) wherever the menu's own draw
// calls happened to look "unchanged" against that leftover content —
// reported live as the menu "looking different... only shows Bubble
// Bobble" after a return-to-menu. Renderer::forceFullRedraw() (confirmed
// by reading Renderer.h directly) exists specifically for this.
void switchScene(pr32::core::Scene* scene, ActiveApp app) {
  engine.setScene(scene);
  engine.getRenderer().forceFullRedraw();
  activeApp = app;
  // Belt-and-suspenders: silence the buzzer across every scene switch so
  // a mid-note switch (e.g. returning to the menu right as a chomp/SFX
  // note is playing) can never leave Pac-Man's tone (see
  // pacman_pr32_audio.h) stuck audible in whatever scene comes next.
  // Harmless no-op if silent. Always the 1-arg form — see hardware.h's
  // comment on PIN_BUZZER for why a duration must never be passed here.
  noTone(PIN_BUZZER);
}

// Destroys whichever game scene is currently active (if any) and
// switches back to the menu — the other half of the placement-new
// scheme, paired with the constructions in the ActiveApp::Menu case
// below. Explicit destructor call, NOT `delete` — these were placement-
// new'd into sceneStorage, not allocated by `new` itself, so `delete`
// would wrongly try to `free()` a static buffer. Safe to call
// unconditionally: exactly one of the four pointers is non-null
// whenever activeApp != Menu (Menu itself owns no game pointer to
// free).
void destroyCurrentGameScene() {
  switch (activeApp) {
    case ActiveApp::BubbleBobble: bubbleBobbleScene->~BubbleBobbleScene(); bubbleBobbleScene = nullptr; break;
    case ActiveApp::PacMan:       pacManScene->~PacManScene();             pacManScene = nullptr;       break;
    case ActiveApp::Galaga:       galagaScene->~GalagaScene();             galagaScene = nullptr;       break;
    case ActiveApp::Arkanoid:     arkanoidScene->~ArkanoidScene();         arkanoidScene = nullptr;     break;
    case ActiveApp::BlockStack:   blockStackScene->~BlockStackScene();     blockStackScene = nullptr;   break;
    case ActiveApp::RType:        rtypeScene->~RTypeScene();               rtypeScene = nullptr;        break;
    case ActiveApp::DigDug:       digdugScene->~DigDugScene();             digdugScene = nullptr;       break;
    case ActiveApp::Menu:         break;
  }
}

// Forward-declared: demo mode (below, near its own globals) needs to
// hijack a mid-game "return to menu" request into "advance to the next
// demo game" instead — see demoAdvanceGame().
bool demoModeActive = false;
void demoAdvanceGame();

// L3 (thumbstick click) held 2s at the menu triggers demo/attract mode
// — see enterDemoMode() and its neighboring comment.
unsigned long l3HeldSinceMs = 0;
bool demoTriggerLatched = false;
constexpr unsigned long kDemoTriggerHoldMs = 2000;

void returnToMenu() {
  if (demoModeActive) { demoAdvanceGame(); return; }
  destroyCurrentGameScene();
  switchScene(&menuScene, ActiveApp::Menu);
}

// --- Attract/demo mode: auto-plays each game for a while so the console
// can show itself off without anyone touching it — triggered by holding
// L3 (thumbstick click) 2s at the menu, cancelled by any real input. ---
int demoGameIdx = 0;    // 0..6, same order as the menu / ActiveApp
int demoLap = 0;        // 0, 1, 2 -> lap 1/2/3; lap index 1 ("the second
                        // loop") skips each game ahead a level/round
                        // before playing it, where that game has a debug
                        // hook for it. Wraps back to 0 after lap 2 (index
                        // 2), i.e. repeats every 3 laps, indefinitely,
                        // until cancelled.
unsigned long demoGameStartMs = 0;
constexpr unsigned long kDemoGameDurationMs = 15000;

// Constructs game `idx` exactly like the menu's own selection branches
// (same placement-new-into-sceneStorage pattern), then, on lap index 1
// only, calls that game's existing debug level/round-skip method if it
// has one. Not every game does — Galaga, Block Stack, and Dig Dug don't
// currently expose a matching hook, so those three just play their
// normal opening on every lap rather than skipping ahead.
void demoEnterGame(int idx) {
  switch (idx) {
    case 0:
      bubbleBobbleScene = new (sceneStorage) bubblebobble_pr32::BubbleBobbleScene();
      switchScene(bubbleBobbleScene, ActiveApp::BubbleBobble);
      if (demoLap == 1) bubbleBobbleScene->debugNextLevel();
      break;
    case 1:
      pacManScene = new (sceneStorage) pacman_pr32::PacManScene();
      switchScene(pacManScene, ActiveApp::PacMan);
      if (demoLap == 1) pacManScene->debugAdvanceLevel();
      break;
    case 2:
      galagaScene = new (sceneStorage) galaga_pr32::GalagaScene();
      switchScene(galagaScene, ActiveApp::Galaga);
      break;
    case 3:
      arkanoidScene = new (sceneStorage) arkanoid_pr32::ArkanoidScene();
      switchScene(arkanoidScene, ActiveApp::Arkanoid);
      if (demoLap == 1) arkanoidScene->debugSkipRound();
      break;
    case 4:
      blockStackScene = new (sceneStorage) blockstack_pr32::BlockStackScene();
      switchScene(blockStackScene, ActiveApp::BlockStack);
      break;
    case 5:
      rtypeScene = new (sceneStorage) rtype_pr32::RTypeScene();
      switchScene(rtypeScene, ActiveApp::RType);
      if (demoLap == 1) rtypeScene->debugSkipToBoss();
      break;
    case 6:
      digdugScene = new (sceneStorage) digdug_pr32::DigDugScene();
      switchScene(digdugScene, ActiveApp::DigDug);
      break;
  }
  demoGameStartMs = millis();
}

void enterDemoMode() {
  demoModeActive = true;
  demoGameIdx = 0;
  demoLap = 0;
  // Reset so a stale hold-timestamp from triggering this can't make the
  // menu's L3-hold check look like it's already been held 2s again the
  // instant demo mode ends, if L3 happens to still be down then.
  l3HeldSinceMs = 0;
  demoTriggerLatched = false;
  menuScene.clearSelection();
  demoEnterGame(demoGameIdx);
}

// Called both when a game's own 10-second timer expires AND when a game
// requests an early return-to-menu on its own (game over, etc. —
// returnToMenu() reroutes here instead while demo mode is active) so a
// game that ends itself early doesn't fall through to the real
// interactive menu mid-demo.
void demoAdvanceGame() {
  destroyCurrentGameScene();
  ++demoGameIdx;
  if (demoGameIdx >= 7) {
    demoGameIdx = 0;
    demoLap = (demoLap + 1) % 3;
  }
  demoEnterGame(demoGameIdx);
}

void exitDemoMode() {
  demoModeActive = false;
  l3HeldSinceMs = 0;
  demoTriggerLatched = false;
  destroyCurrentGameScene();
  switchScene(&menuScene, ActiveApp::Menu);
}

// A slowly wandering, self-contained input pattern — not "smart" play,
// just enough continuous movement/firing/rotating to look alive in
// every game (real attract-mode demos in actual arcades are just as
// scripted/aimless as this, not literal AI). Every synthetic press is a
// single-frame edge, never held past one loop() iteration, so it can
// never accidentally cross any game's pause-hold threshold (500ms+) or
// this file's own reset/mute-hold combos (which need several SECONDS
// held) — safe to run through the exact same input pipeline real
// hardware input does.
Pr32Input demoSyntheticInput() {
  Pr32Input in;
  unsigned long t = millis();
  in.thumbX = sinf(static_cast<float>(t) * 0.0011f);
  in.thumbY = cosf(static_cast<float>(t) * 0.0007f);

  // Tetris has its own looping background music (see
  // blockstack_pr32_audio.h) that ducks out for one-shot SFX on every
  // rotate/hard-drop/hold — mashing those as often as every other game
  // gets left the music barely audible under constant SFX. Every
  // synthetic press that can trigger a Tetris SFX (A/R3/RB/LB edges,
  // and encoder ticks — 4 of which is one rotate, see
  // kEncStepThreshold) backs off to a much slower cadence specifically
  // while Tetris is the demo's current game, so the music actually gets
  // heard between the occasional piece move.
  bool quietForMusic = (activeApp == ActiveApp::BlockStack);

  static unsigned long lastFireMs = 0;
  unsigned long fireInterval = quietForMusic ? 4500 : 650;
  if (t - lastFireMs > fireInterval) { lastFireMs = t; in.aEdge = true; in.aHeld = true; }

  static unsigned long lastR3Ms = 0;
  unsigned long r3Interval = quietForMusic ? 5000 : 1400;
  if (t - lastR3Ms > r3Interval) { lastR3Ms = t; in.r3Edge = true; in.r3Held = true; }

  static unsigned long lastRbMs = 0;
  unsigned long rbInterval = quietForMusic ? 3800 : 900;
  if (t - lastRbMs > rbInterval) { lastRbMs = t; in.rbEdge = true; in.rbHeld = true; }

  static unsigned long lastLbMs = 0;
  unsigned long lbInterval = quietForMusic ? 4200 : 1100;
  if (t - lastLbMs > lbInterval) { lastLbMs = t; in.lbEdge = true; in.lbHeld = true; }

  static unsigned long lastEncMs = 0;
  unsigned long encInterval = quietForMusic ? 900 : 130;
  if (t - lastEncMs > encInterval) {
    lastEncMs = t;
    in.encDelta = ((t / 2000) % 2 == 0) ? 1 : -1;
  }
  return in;
}

// True if any of the fields readInput() actually populates from real
// hardware (or the serial debug-injection protocol) looks like a
// deliberate press — used to cancel demo mode the instant a person
// touches anything, same as a real arcade cabinet's attract mode.
// Thumbstick uses a generous deadzone (0.5) so idle ADC jitter can't
// false-trigger a cancel.
bool inputLooksReal(const Pr32Input& in) {
  return in.encDelta != 0 || in.aEdge || in.rbEdge || in.lbEdge || in.r3Edge || in.l3Edge ||
         fabsf(in.thumbX) > 0.5f || fabsf(in.thumbY) > 0.5f;
}

Button a = { PIN_A };
Button rb = { PIN_RB };
Button lb = { PIN_LB };
Button r3 = { PIN_R3 };
Button l3 = { PIN_L3 };
long lastEncoderValue = 0;
bool screenshotRequested = false;

// --- Idle -> light sleep, hard reset, mute (power management pass) ---
unsigned long lastActivityMs = 0; // set for real in setup()
constexpr unsigned long kIdleTimeoutMs = 5UL * 60UL * 1000UL;
unsigned long resetComboHeldSinceMs = 0; // 0 = not currently building toward a reset
unsigned long muteHeldSinceMs = 0;       // 0 = not currently building toward a mute toggle
// The wake button is still physically held when the very next loop()
// iteration reads input, producing a genuine edge (aEdge etc.) that
// would otherwise ALSO act as a normal press — e.g. waking on A at
// the menu would also confirm whatever's selected. Set true right
// after esp_light_sleep_start() returns; the next loop() iteration
// discards that one frame's input instead of acting on it.
bool suppressNextInput = false;

// Light sleep (not deep sleep): preserves RAM, so whichever scene was
// active resumes exactly where it left off on wake instead of
// rebooting to the menu — chosen over deep sleep once the backlight got
// its own GPIO switch (see hardware.h's PIN_BACKLIGHT), since that
// removed deep sleep's one real advantage here (the backlight used to
// stay lit regardless of sleep mode; now either mode can force it off,
// so light sleep's state-preservation wins with only a small, mostly
// theoretical power-draw penalty for a 5-minute idle timeout).
void enterLightSleep() {
  noTone(PIN_BUZZER);
  uint8_t savedBrightness = hardwareGetBrightness();
  hardwareSetBrightness(0);

  // Light sleep keeps the digital domain powered (unlike deep sleep),
  // so gpio_wakeup_enable() works on any regular GPIO at any level —
  // no ext0/ext1 single-pin restriction. Wake on ANY of the physical
  // buttons going low (they're all active-low, INPUT_PULLUP).
  gpio_wakeup_enable((gpio_num_t)PIN_A, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_RB, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_LB, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_R3, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_L3, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  esp_light_sleep_start();
  // Execution resumes HERE on wake — light sleep doesn't reboot.

  hardwareSetBrightness(savedBrightness);
  lastActivityMs = millis();
  suppressNextInput = true;
}

// Manually-triggered DEEP sleep (menu entry, not the automatic idle
// timeout above) — for genuinely long downtime (e.g. set aside while
// working on the physical case), where deep sleep's near-zero draw
// actually matters, unlike the 5-minute auto-timeout where light
// sleep's state-preservation won out (see enterLightSleep()'s
// comment). Deep sleep reboots on wake (no RAM retention), so it lands
// back at the menu — acceptable here since the user is deliberately
// putting it away, not walking away mid-game. Only one dedicated
// RTC-capable pin can wake from deep sleep (ext0) — using A/KEY0,
// the primary/most findable button.
void enterDeepSleep() {
  noTone(PIN_BUZZER);
  // Explicitly drive the buzzer pin low before the digital domain powers
  // down. This does NOT stop the low-volume pittering noise reported on
  // battery power during deep sleep (see hardware.h's PIN_BUZZER comment
  // and this project's memory notes) — GPIO19 isn't RTC-capable, so its
  // driven state can't be held across deep sleep no matter what we do
  // here; it floats the instant the digital domain sleeps regardless.
  // That noise is the boost converter's switching ripple coupling into
  // the now-floating piezo trace, and needs a hardware fix (pull-down
  // resistor across the piezo, or moving PIN_BUZZER to an RTC-capable
  // GPIO). This line only avoids a transient click at the moment of
  // sleeping, nothing more.
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  hardwareSetBrightness(0);
  rtc_gpio_pullup_en((gpio_num_t)PIN_A);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_A);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_A, 0); // wake when A reads LOW
  esp_deep_sleep_start();
  // Never returns — a wake is a full reboot from setup().
}

// Debug-only "held" latch: real hardware keeps R3 pressed across many
// frames on its own, but a single injected 'w' only asserts for the one
// frame it's read on — not enough to cross this game's PAUSE_HOLD_MS
// (500ms). Uppercase 'W' toggles a sustained-hold latch instead (same
// convention as this project's other games), so a real remote pause test
// is `tap 'W', wait >500ms, tap 'W' again`.
bool debugEncSwLatch = false;
bool debugL3Latch = false;

// Debug-only level select ('n'ext/'b'ack), added to reach the 5 new levels
// (added this session, only reachable in normal play by actually clearing
// every enemy on every level before them) without a full playthrough.
// Remove this along with BubbleBobbleScene::debugNextLevel()/
// debugPrevLevel() once the new levels are confirmed on hardware.
bool debugNextLevelRequested = false;
bool debugPrevLevelRequested = false;
bool debugWarpToTunnelRequested = false;
bool debugWarpToPowerRequested = false;
bool debugSpawnFruitRequested = false;
bool debugAdvanceLevelRequested = false;
bool debugForceCaptureRequested = false;
bool debugForceRescueRequested = false;
bool debugForceEatGhostRequested = false;
bool debugSkipRoundRequested = false;
bool debugNearKillBossRequested = false;
bool debugForcePowerupRequested = false;

// Debug-only, same "kept around" convention as every other debug hook
// here — toggles the new backlight power switch (hardware.h's
// PIN_BACKLIGHT) for bring-up testing ahead of folding it into the
// idle-sleep feature. Global, not per-scene: the backlight isn't a
// per-game concern.
bool debugBacklightToggleRequested = false;
bool debugBacklightOn = true;
bool debugBrightnessStepRequested = false;
bool debugBrightnessSweepRequested = false;
bool debugSkipToBossRequested = false;
bool debugDigDugSpawnEnemyRequested = false;
bool debugDigDugForceRockRequested = false;

// Debug-only directional latch for Pac-Man (real control is the
// thumbstick, which the serial debug-injection protocol has no way to
// fake directly — unlike encoder ticks/button presses, there's no analog
// axis in that protocol at all). Sustained (latched, like debugEncSwLatch
// above), not a single-frame tap: a bare one-frame nudge wouldn't move
// Pac-Man far enough to matter against an 8px tile grid. 'i'/'k'/'j'/'l'
// (vi-style up/down/left/right — the usual a/d/s/1/2 chars are already
// taken) each set a persistent direction override that keeps applying to
// thumbX/thumbY every frame until a different direction (or 'o' = none)
// is sent; harmless on real hardware since it only overrides when
// nonzero.
int debugDirX = 0;
int debugDirY = 0;

// Same serial debug-injection convention every other game in this project
// uses (see README.md / feedback_remote_hw_debugging.md): a/d = encoder
// ticks, s = A, 1 = RB, 2 = LB, w = R3 tap, W = R3 sustained
// hold toggle, p = screenshot. Unrecognized characters (other games'
// l/m/S/L conventions) are harmlessly ignored here, same as always —
// purely additive, no effect on real hardware use when nothing is sent.
Pr32Input readInput() {
  Pr32Input in;

  noInterrupts();
  long enc = encoderValue;
  interrupts();
  in.encDelta = enc - lastEncoderValue;
  lastEncoderValue = enc;

  bool key0Changed = updateButton(a);
  in.aHeld = a.pressed;
  in.aEdge = key0Changed && a.pressed;

  bool btn1Changed = updateButton(rb);
  in.rbHeld = rb.pressed;
  in.rbEdge = btn1Changed && rb.pressed;

  bool btn2Changed = updateButton(lb);
  in.lbHeld = lb.pressed;
  in.lbEdge = btn2Changed && lb.pressed;

  bool encSwChanged = updateButton(r3);
  in.r3Held = r3.pressed;
  in.r3Edge = encSwChanged && r3.pressed;

  bool l3Changed = updateButton(l3);
  in.l3Held = l3.pressed;
  in.l3Edge = l3Changed && l3.pressed;

  hardwareReadThumbstick(in.thumbX, in.thumbY);

  while (Serial.available()) {
    char c = Serial.read();
    lastActivityMs = millis(); // any debug/remote-test byte counts as activity
    switch (c) {
      case 'a': in.encDelta -= 1; break;
      case 'd': in.encDelta += 1; break;
      case 's': in.aEdge = true; in.aHeld = true; break;
      case '1': in.rbEdge = true; in.rbHeld = true; break;
      case '2': in.lbEdge = true; in.lbHeld = true; break;
      case 'w': in.r3Edge = true; in.r3Held = true; break;
      case 'W': debugEncSwLatch = !debugEncSwLatch; break;
      case 'q': in.l3Edge = true; in.l3Held = true; break;
      case 'Q': debugL3Latch = !debugL3Latch; break;
      case 'p': screenshotRequested = true; break;
      case 'n': debugNextLevelRequested = true; break;
      case 'b': debugPrevLevelRequested = true; break;
      // debugDirY follows the real thumbstick's own raw-value convention
      // (positive = physically UP — see hardwareReadThumbstick()/
      // menu.cpp's comment), same sign PacManScene::update() now expects
      // after fixing the up/down-inverted bug, so this debug injection
      // stays consistent with a real stick instead of silently masking
      // that class of bug in the future.
      case 'i': debugDirX = 0; debugDirY = 1; break;   // up
      case 'k': debugDirX = 0; debugDirY = -1; break;  // down
      case 'j': debugDirX = -1; debugDirY = 0; break;
      case 'l': debugDirX = 1; debugDirY = 0; break;
      case 'o': debugDirX = 0; debugDirY = 0; break;
      case 't': debugWarpToTunnelRequested = true; break;
      case 'y': debugWarpToPowerRequested = true; break;
      case 'f': debugSpawnFruitRequested = true; break;
      case 'v': debugAdvanceLevelRequested = true; break;
      case 'c': debugForceCaptureRequested = true; break;
      case 'r': debugForceRescueRequested = true; break;
      case 'e': debugForceEatGhostRequested = true; break;
      case 'u': debugSkipRoundRequested = true; break;
      case 'z': debugNearKillBossRequested = true; break;
      case 'x': debugForcePowerupRequested = true; break;
      case 'g': debugBacklightToggleRequested = true; break;
      case 'G': debugBrightnessStepRequested = true; break;
      case 'H': debugBrightnessSweepRequested = true; break;
      case 'B': debugSkipToBossRequested = true; break;
      case 'm': debugDigDugSpawnEnemyRequested = true; break;
      case 'h': debugDigDugForceRockRequested = true; break;
      default: break;
    }
  }
  if (debugEncSwLatch) in.r3Held = true;
  if (debugL3Latch) in.l3Held = true;
  if (debugDirX != 0 || debugDirY != 0) {
    in.thumbX = static_cast<float>(debugDirX);
    in.thumbY = static_cast<float>(debugDirY);
  }

  return in;
}

void captureScreenshot() {
  uint8_t* buf = engine.getRenderer().getDrawSurface().getSpriteBuffer();
  int w = PHYSICAL_DISPLAY_WIDTH;
  int h = PHYSICAL_DISPLAY_HEIGHT;
  if (!buf) {
    Serial.println("SCR_FAIL nobuffer");
    return;
  }
  Serial.printf("SCR_BEGIN %d %d %lu RGB332\n", w, h, (unsigned long)(w * h));
  Serial.write(buf, w * h);
  Serial.println();
  Serial.println("SCR_END");
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("PixelRoot32 Arcade - starting");

  hardwareInit(); // input only (encoder/buttons/thumbstick) — see hardware.h
  a.debounceMs = 40;
  rb.debounceMs = 25;
  lb.debounceMs = 25;
  r3.debounceMs = 25;

  engine.init();
  switchScene(&menuScene, ActiveApp::Menu);

  lastActivityMs = millis();

  Serial.println("Ready.");
}

void loop() {
  Pr32Input in = readInput();

  if (suppressNextInput) {
    suppressNextInput = false;
    in = Pr32Input(); // discard this one frame's input — see its declaration comment
  }

  // L3 held 2s at the menu (and only at the menu — holding it mid-game
  // does nothing here) starts the attract/demo loop.
  if (activeApp == ActiveApp::Menu && !demoModeActive) {
    if (in.l3Held) {
      if (l3HeldSinceMs == 0) l3HeldSinceMs = millis();
      if (!demoTriggerLatched && millis() - l3HeldSinceMs >= kDemoTriggerHoldMs) {
        demoTriggerLatched = true;
        enterDemoMode();
      }
    } else {
      l3HeldSinceMs = 0;
      demoTriggerLatched = false;
    }
  }

  // While demo mode is running, any real press cancels it immediately
  // (checked against the REAL input just read above, before it gets
  // replaced below) — otherwise this frame's input is swapped for the
  // synthetic wandering pattern that actually drives the current game.
  if (demoModeActive) {
    if (inputLooksReal(in)) {
      exitDemoMode();
    } else {
      in = demoSyntheticInput();
    }
  }

  // Any real activity (held states, not just edges, so a continuous
  // hold — e.g. mid-combo — keeps counting) resets the idle timer.
  if (in.encDelta != 0 || in.aHeld || in.rbHeld || in.lbHeld || in.r3Held ||
      fabsf(in.thumbX) > 0.05f || fabsf(in.thumbY) > 0.05f) {
    lastActivityMs = millis();
  }

  if (in.rbHeld && in.lbHeld) {
    if (resetComboHeldSinceMs == 0) resetComboHeldSinceMs = millis();
    else if (millis() - resetComboHeldSinceMs > 3000) ESP.restart();
  } else {
    resetComboHeldSinceMs = 0;
  }

  if (in.lbHeld && !in.rbHeld) {
    if (muteHeldSinceMs == 0) {
      muteHeldSinceMs = millis();
    } else if (millis() - muteHeldSinceMs > 1500) {
      hardwareSetMuted(!audioMuted);
      Serial.printf("Muted: %s\n", audioMuted ? "yes" : "no");
      muteHeldSinceMs = 0; // fire once per hold, not repeatedly
    }
  } else {
    muteHeldSinceMs = 0;
  }

  if (millis() - lastActivityMs > kIdleTimeoutMs) {
    enterLightSleep();
  }

  if (screenshotRequested) {
    screenshotRequested = false;
    captureScreenshot();
  }

  if (debugBacklightToggleRequested) {
    debugBacklightToggleRequested = false;
    debugBacklightOn = !debugBacklightOn;
    hardwareSetBacklight(debugBacklightOn);
    Serial.printf("Backlight %s\n", debugBacklightOn ? "ON" : "OFF");
  }

  if (debugBrightnessStepRequested) {
    debugBrightnessStepRequested = false;
    static const uint8_t kLevels[] = {255, 192, 128, 64, 32, 0};
    static int levelIdx = 0;
    levelIdx = (levelIdx + 1) % (sizeof(kLevels) / sizeof(kLevels[0]));
    hardwareSetBrightness(kLevels[levelIdx]);
    Serial.printf("Brightness %d/255\n", kLevels[levelIdx]);
  }

  if (debugBrightnessSweepRequested) {
    debugBrightnessSweepRequested = false;
    // Deliberately blocking — a one-off manual bring-up test, not
    // something that runs during normal use. 5s down, 5s back up.
    Serial.println("Sweeping brightness: 10s (5 down, 5 up)");
    constexpr int kSteps = 100;
    constexpr int kStepDelayMs = 50; // 100 * 50ms = 5000ms per direction
    for (int i = 0; i <= kSteps; ++i) {
      hardwareSetBrightness(255 - static_cast<uint8_t>(255 * i / kSteps));
      delay(kStepDelayMs);
    }
    for (int i = 0; i <= kSteps; ++i) {
      hardwareSetBrightness(static_cast<uint8_t>(255 * i / kSteps));
      delay(kStepDelayMs);
    }
    Serial.println("Sweep done");
  }

  switch (activeApp) {
    case ActiveApp::Menu: {
      menuScene.setInput(in);
      int selected = menuScene.getSelectedGame();
      if (selected == 0) {
        menuScene.clearSelection();
        bubbleBobbleScene = new (sceneStorage) bubblebobble_pr32::BubbleBobbleScene();
        switchScene(bubbleBobbleScene, ActiveApp::BubbleBobble);
      } else if (selected == 1) {
        menuScene.clearSelection();
        pacManScene = new (sceneStorage) pacman_pr32::PacManScene();
        switchScene(pacManScene, ActiveApp::PacMan);
      } else if (selected == 2) {
        menuScene.clearSelection();
        galagaScene = new (sceneStorage) galaga_pr32::GalagaScene();
        switchScene(galagaScene, ActiveApp::Galaga);
      } else if (selected == 3) {
        menuScene.clearSelection();
        arkanoidScene = new (sceneStorage) arkanoid_pr32::ArkanoidScene();
        switchScene(arkanoidScene, ActiveApp::Arkanoid);
      } else if (selected == 4) {
        menuScene.clearSelection();
        blockStackScene = new (sceneStorage) blockstack_pr32::BlockStackScene();
        switchScene(blockStackScene, ActiveApp::BlockStack);
      } else if (selected == 5) {
        menuScene.clearSelection();
        rtypeScene = new (sceneStorage) rtype_pr32::RTypeScene();
        switchScene(rtypeScene, ActiveApp::RType);
      } else if (selected == 6) {
        menuScene.clearSelection();
        digdugScene = new (sceneStorage) digdug_pr32::DigDugScene();
        switchScene(digdugScene, ActiveApp::DigDug);
      } else if (selected == 7) {
        // Not a game Scene — triggers deep sleep directly from the menu.
        menuScene.clearSelection();
        enterDeepSleep();
      }
      break;
    }
    case ActiveApp::BubbleBobble: {
      bubbleBobbleScene->setInput(in);
      if (debugNextLevelRequested) {
        debugNextLevelRequested = false;
        bubbleBobbleScene->debugNextLevel();
      }
      if (debugPrevLevelRequested) {
        debugPrevLevelRequested = false;
        bubbleBobbleScene->debugPrevLevel();
      }
      if (bubbleBobbleScene->wantsReturnToMenu()) {
        bubbleBobbleScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
    case ActiveApp::PacMan: {
      pacManScene->setInput(in);
      if (debugWarpToTunnelRequested) {
        debugWarpToTunnelRequested = false;
        pacManScene->debugWarpToTunnel();
      }
      if (debugWarpToPowerRequested) {
        debugWarpToPowerRequested = false;
        pacManScene->debugWarpToPowerPellet();
      }
      if (debugSpawnFruitRequested) {
        debugSpawnFruitRequested = false;
        pacManScene->debugSpawnFruit();
      }
      if (debugAdvanceLevelRequested) {
        debugAdvanceLevelRequested = false;
        pacManScene->debugAdvanceLevel();
      }
      if (debugForceEatGhostRequested) {
        debugForceEatGhostRequested = false;
        pacManScene->debugForceEatGhost();
      }
      if (pacManScene->wantsReturnToMenu()) {
        pacManScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
    case ActiveApp::Galaga: {
      galagaScene->setInput(in);
      if (debugForceCaptureRequested) {
        debugForceCaptureRequested = false;
        galagaScene->debugForceCaptureAttempt();
      }
      if (debugForceRescueRequested) {
        debugForceRescueRequested = false;
        galagaScene->debugForceRescue();
      }
      if (galagaScene->wantsReturnToMenu()) {
        galagaScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
    case ActiveApp::Arkanoid: {
      arkanoidScene->setInput(in);
      if (debugSkipRoundRequested) {
        debugSkipRoundRequested = false;
        arkanoidScene->debugSkipRound();
      }
      if (debugNearKillBossRequested) {
        debugNearKillBossRequested = false;
        arkanoidScene->debugNearKillBoss();
      }
      if (debugForcePowerupRequested) {
        debugForcePowerupRequested = false;
        arkanoidScene->debugForcePowerup();
      }
      if (arkanoidScene->wantsReturnToMenu()) {
        arkanoidScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
    case ActiveApp::BlockStack: {
      blockStackScene->setInput(in);
      if (blockStackScene->wantsReturnToMenu()) {
        blockStackScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
    case ActiveApp::RType: {
      rtypeScene->setInput(in);
      if (debugSkipToBossRequested) {
        debugSkipToBossRequested = false;
        rtypeScene->debugSkipToBoss();
      }
      if (rtypeScene->wantsReturnToMenu()) {
        rtypeScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
    case ActiveApp::DigDug: {
      digdugScene->setInput(in);
      if (debugDigDugSpawnEnemyRequested) {
        debugDigDugSpawnEnemyRequested = false;
        digdugScene->debugSpawnEnemyNearPlayer();
      }
      if (debugDigDugForceRockRequested) {
        debugDigDugForceRockRequested = false;
        digdugScene->debugForceNearestRockDrop();
      }
      if (digdugScene->wantsReturnToMenu()) {
        digdugScene->clearReturnToMenuRequest();
        returnToMenu();
      }
      break;
    }
  }

  if (demoModeActive && activeApp != ActiveApp::Menu &&
      millis() - demoGameStartMs >= kDemoGameDurationMs) {
    demoAdvanceGame();
  }

  engine.run();
}
