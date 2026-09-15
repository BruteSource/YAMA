#include "menu_pr32.h"
#include "menu_pr32_sprites.h"
#include "menu_pr32_font.h"
#include <graphics/FontManager.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace pr32 = pixelroot32;

namespace menu_pr32 {

namespace {

struct MenuEntry {
    const char* name;
    bool available;
};

constexpr MenuEntry kEntries[] = {
    { "BUBBLE BOBBLE", true },
    { "PAC-MAN", true },
    { "GALAGA", true },
    { "ARKANOID", true },
    { "BLOCK STACK", true },
    { "R-TYPE", true },
    { "DIG DUG", true },
    { "DEEP SLEEP", true },
};
constexpr int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

// Same 4-tick-per-detent threshold as the old menu.cpp's
// ENC_STEP_THRESHOLD and BubbleBobbleScene's debug level-select — one
// physical encoder click reliably produces several raw interrupt-level
// ticks, not one (learned the hard way this session: triggering on any
// nonzero delta made a single indent jump multiple steps).
constexpr int kEncStepThreshold = 4;

// Only this many rows draw at once — see viewportTop's comment in
// menu_pr32.h for why.
constexpr int kVisibleRows = 1;

// kTile (background tile size) removed along with the tile sprite
// itself (see make_menu_pr32_sprites.py — background is a plain black
// fill now). kScrollSpeedX/Y kept for the same "for now" reason as
// scrollX/scrollY in menu_pr32.h, currently unused.
constexpr float kScrollSpeedX = 14.0f; // px/sec, diagonal drift
constexpr float kScrollSpeedY = 7.0f;

// Time between logo animation frames — 16 frames * 70ms = a 1.12s full
// sweep cycle, brisk enough to read as continuous motion without being
// distracting at a glance.
constexpr float kLogoFrameIntervalS = 0.07f;

// --- Animated "old arcade diagnostic screen" background ---
// Generic self-test-screen vocabulary (crosshatch/convergence pattern,
// color bars, checkerboard, RAM/ROM checksum readouts) shared across
// countless real arcade boards from many different manufacturers —
// researched for authenticity (crosshatch+color-bars+RAM/ROM-checksum
// is the common documented format, not any one game's proprietary
// screen), then redrawn from scratch here with this project's own
// fabricated addresses/checksums, same "generic vocabulary, not a
// specific copy" reasoning as the earlier Memphis-style background.
enum BgPhase { kPhaseCrosshatch, kPhaseColorBars, kPhaseCheckerboard, kPhaseRamCheck, kPhaseRomCheck, kBgPhaseCount };
constexpr float kBgPhaseDurationS = 4.0f;
constexpr float kBgLineStaggerS = 0.4f;   // one more line "starts" every this-many seconds
constexpr float kBgScrambleS = 0.18f;     // how long a line shows jumbled chars before resolving

const char* const kRamLines[] = {
    "RAM TEST",
    "0000-0FFF ........ OK",
    "1000-1FFF ........ OK",
    "2000-2FFF ........ OK",
    "3000-3FFF ........ OK",
    "4000-4FFF ........ OK",
    "ALL RAM OK",
};
constexpr int kRamLineCount = sizeof(kRamLines) / sizeof(kRamLines[0]);

const char* const kRomLines[] = {
    "ROM TEST",
    "IC1  CHECKSUM A3F2 OK",
    "IC2  CHECKSUM 91BC OK",
    "IC3  CHECKSUM 5D07 OK",
    "IC4  CHECKSUM E64A OK",
    "ALL ROM OK",
};
constexpr int kRomLineCount = sizeof(kRomLines) / sizeof(kRomLines[0]);

// Fills `out` with random uppercase/digit/symbol junk the same length
// as `real` — the "jumbled text" a line shows for kBgScrambleS before
// resolving into its real text, like an old terminal-decrypt effect.
void scrambleText(const char* real, char* out, size_t outCap) {
    static const char kCharset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#$%-.";
    size_t len = strlen(real);
    if (len >= outCap) len = outCap - 1;
    for (size_t i = 0; i < len; ++i) {
        out[i] = real[i] == ' ' ? ' ' : kCharset[rand() % (sizeof(kCharset) - 1)];
    }
    out[len] = '\0';
}

} // namespace

void MenuScene::init() {
    selection = 0;
    encAccum = 0;
    confirmedIndex = -1;
    scrollX = 0.0f;
    scrollY = 0.0f;
    thumbMoveLatched = false;
    lastThumbMoveMs = 0;
    viewportTop = 0;
    brightnessAdjusting = false;
    brightnessIndicatorUntilMs = 0;
    logoFrame = 0;
    logoFrameTimer = 0.0f;
    bgPhase = 0;
    bgPhaseTimer = 0.0f;
    sfx.stop();
    tune.stop();

    // This menu's own custom sprite palette (background tile + logo) —
    // set every time the menu becomes active, same idiom
    // BubbleBobbleScene::init() already uses for its own sprites. Needed
    // because the sprite palette is a single global table: without
    // re-asserting this on (re-)entry, returning from Bubble Bobble would
    // still have ITS palette active and the menu's sprites would resolve
    // through the wrong color table entirely.
    pr32::graphics::enableDualPaletteMode(true);
    pr32::graphics::setBackgroundPalette(pr32::graphics::PaletteType::PR32);
    pr32::graphics::setSpriteCustomPalette(menu_pr32_sprites::PALETTE_RGB565);
}

void MenuScene::update(unsigned long deltaTime) {
    float dtSeconds = static_cast<float>(deltaTime) * 0.001f;
    // Background scroll animation disabled for now (static background,
    // per explicit request) — scrollX/scrollY just stay at their init()
    // value of 0 instead of advancing every frame. Left the fields/
    // kScrollSpeedX/Y in place rather than ripping them out, since this
    // is a "for now" call or, not a permanent design decision.

    logoFrameTimer += dtSeconds;
    if (logoFrameTimer >= kLogoFrameIntervalS) {
        logoFrameTimer -= kLogoFrameIntervalS;
        logoFrame = (logoFrame + 1) % menu_pr32_sprites::LOGO_FRAME_COUNT;
    }

    bgPhaseTimer += dtSeconds;
    if (bgPhaseTimer >= kBgPhaseDurationS) {
        bgPhaseTimer -= kBgPhaseDurationS;
        bgPhase = (bgPhase + 1) % kBgPhaseCount;
    }

    encAccum += static_cast<int>(pendingInput.encDelta);
    if (encAccum >= kEncStepThreshold) {
        selection = (selection + 1) % kEntryCount;
        encAccum = 0;
        sfx.play(menu_pr32_audio::kMenuMoveSfx, menu_pr32_audio::kMenuMoveSfxLen);
    } else if (encAccum <= -kEncStepThreshold) {
        selection = (selection - 1 + kEntryCount) % kEntryCount;
        encAccum = 0;
        sfx.play(menu_pr32_audio::kMenuMoveSfx, menu_pr32_audio::kMenuMoveSfxLen);
    }

    // Thumbstick as a secondary selector, same latch idiom as the old
    // menu.cpp: one step per push past kThumbTrigger, released once it
    // falls back below kThumbRelease so holding the stick doesn't spam
    // multiple steps. Raw thumbY reads positive when pushed UP (same
    // convention documented/fixed for Pac-Man's own movement — see
    // PacManScene::update()) — up moves the selection up (previous
    // entry), down moves it to the next.
    //
    // Trigger lowered from the old menu.cpp's borrowed 0.6/0.3 (tuned for
    // a different stick on the original console) after being reported
    // live as "only up works, and it's laggy" — this stick's real
    // mechanical travel is apparently NOT symmetric (a common cheap-
    // thumbstick trait), so "down" likely never reached 0.6 at all,
    // while pushing it further just felt like unresponsive lag. Matched
    // to Pac-Man's own already-confirmed-working movement threshold
    // (0.3f, see PacManScene::update()) instead of a separately-tuned
    // value, on the theory that whatever magnitude reliably registers
    // there should reliably register here too. Cooldown shortened to
    // match — 200ms was partly what read as "laggy" on top of a
    // threshold that took a hard push to reach.
    constexpr float kThumbTrigger = 0.35f;
    constexpr float kThumbRelease = 0.15f;
    constexpr unsigned long kThumbMoveCooldownMs = 150;
    float ty = pendingInput.thumbY;
    if (fabsf(ty) < kThumbRelease) thumbMoveLatched = false;
    unsigned long nowMs = millis();
    if (!thumbMoveLatched && fabsf(ty) >= kThumbTrigger && nowMs - lastThumbMoveMs >= kThumbMoveCooldownMs) {
        thumbMoveLatched = true;
        lastThumbMoveMs = nowMs;
        if (ty > 0) {
            selection = (selection - 1 + kEntryCount) % kEntryCount;
        } else {
            selection = (selection + 1) % kEntryCount;
        }
        sfx.play(menu_pr32_audio::kMenuMoveSfx, menu_pr32_audio::kMenuMoveSfxLen);
    }

    if ((pendingInput.aEdge || pendingInput.r3Edge) && kEntries[selection].available) {
        confirmedIndex = selection;
        sfx.play(menu_pr32_audio::kMenuConfirmSfx, menu_pr32_audio::kMenuConfirmSfxLen);
    }

    // Scroll the viewport by the minimum amount needed to keep the
    // selection visible — standard scrolling-list behavior.
    if (selection < viewportTop) {
        viewportTop = selection;
    } else if (selection > viewportTop + kVisibleRows - 1) {
        viewportTop = selection - kVisibleRows + 1;
    }
    if (viewportTop > kEntryCount - kVisibleRows) viewportTop = kEntryCount - kVisibleRows;
    if (viewportTop < 0) viewportTop = 0;

    // Brightness: thumbstick X, continuous while deflected past the
    // deadzone. Rate chosen so a full-deflection hold sweeps the whole
    // 0-255 range in about 1.5s — fast enough to feel responsive,
    // slow enough to land on a precise level.
    constexpr float kBrightnessDeadzone = 0.15f;
    constexpr float kBrightnessRatePerSec = 180.0f;
    float tx = pendingInput.thumbX;
    if (fabsf(tx) >= kBrightnessDeadzone) {
        int level = static_cast<int>(hardwareGetBrightness()) +
                    static_cast<int>(tx * kBrightnessRatePerSec * dtSeconds);
        if (level < 0) level = 0;
        if (level > 255) level = 255;
        hardwareSetBrightness(static_cast<uint8_t>(level));
        brightnessAdjusting = true;
        brightnessIndicatorUntilMs = millis() + 1500;
    } else if (brightnessAdjusting) {
        // Just released past the deadzone — commit to NVS once, not
        // every frame while adjusting.
        brightnessAdjusting = false;
        hardwareSaveBrightness();
    }

    // Background tune only advances (and only re-triggers itself once it
    // loops) while a move/confirm blip isn't using the single buzzer
    // channel — see menu_pr32.h's comment on `sfx`/`tune`.
    sfx.update();
    if (!sfx.isPlaying()) {
        tune.update();
        if (!tune.isPlaying()) {
            tune.play(menu_pr32_audio::kMenuTune, menu_pr32_audio::kMenuTuneLen);
        }
    }

    Scene::update(deltaTime);
}

namespace {
// Tiles an 8x8 solid-color swatch sprite across an arbitrary rectangle —
// stands in for drawFilledRectangle(..., Color::X) for colors that need
// to go through the menu's OWN (compensated) sprite palette rather than
// the engine's shared default palette (see swatch_cyan/orange/navy's
// comment in make_menu_pr32_sprites.py for why: those shared Color enums
// showed real red/blue-swapped colors on the actual physical panel —
// confirmed by the user, not a photo artifact — but Bubble Bobble's use
// of the SAME shared palette is confirmed correct, so the fix has to be
// scoped to colors this menu controls itself, not the shared table).
void fillSwatch(pixelroot32::graphics::Renderer& renderer, const pixelroot32::graphics::Sprite4bpp& swatch,
                 int x0, int y0, int w, int h) {
    for (int y = 0; y < h; y += swatch.height) {
        for (int x = 0; x < w; x += swatch.width) {
            renderer.drawSprite(swatch, x0 + x, y0 + y, false);
        }
    }
}

// Same 6-step ROYGBV rainbow the logo animates through (see
// make_menu_pr32_sprites.py — these swatches reuse its exact RGB
// tuples, zero extra palette cost). Order must match RAINBOW_HUES/
// RAINBOW_COLORS there.
constexpr float kRainbowHues[6] = { 0, 36, 72, 140, 210, 270 };

const pixelroot32::graphics::Sprite4bpp& rainbowSwatchForHue(float hueDeg) {
    namespace spr = menu_pr32_sprites;
    static const pixelroot32::graphics::Sprite4bpp* const kSwatches[6] = {
        &spr::MENU_SWATCH_RAINBOW0, &spr::MENU_SWATCH_RAINBOW1, &spr::MENU_SWATCH_RAINBOW2,
        &spr::MENU_SWATCH_RAINBOW3, &spr::MENU_SWATCH_RAINBOW4, &spr::MENU_SWATCH_RAINBOW5,
    };
    int best = 0;
    float bestDist = 1e9f;
    for (int i = 0; i < 6; ++i) {
        float d = fabsf(fmodf(hueDeg - kRainbowHues[i] + 540.0f, 360.0f) - 180.0f);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return *kSwatches[best];
}

// Draws a `kBorderTile`-thick rainbow border around [x0,y0]-[x1,y1],
// one swatch tile at a time walking the perimeter, each tile's hue
// picked from its position along that walk plus `phaseDeg` — the same
// "quantize a position-plus-phase hue to the nearest of 6 swatches"
// principle as the logo's own animated frames (see
// make_menu_pr32_sprites.py's LOGO section), just computed live here
// per-tile instead of pre-baked, since a border is cheap to walk at
// runtime and doesn't need dozens of whole-panel pre-baked frames the
// way the logo bitmap does. `phaseDeg` should come from the same
// logoFrame-driven timer as the logo for a visually synced sweep.
void drawRainbowBorder(pixelroot32::graphics::Renderer& renderer, int x0, int y0, int x1, int y1, float phaseDeg) {
    constexpr int kBorderTile = 8;
    int w = x1 - x0, h = y1 - y0;
    int topCount = w / kBorderTile;
    int sideCount = (h - 2 * kBorderTile) / kBorderTile;
    int total = topCount * 2 + sideCount * 2;
    int idx = 0;
    auto drawAt = [&](int tx, int ty) {
        float frac = static_cast<float>(idx) / static_cast<float>(total);
        float hue = fmodf(frac * 360.0f + phaseDeg, 360.0f);
        renderer.drawSprite(rainbowSwatchForHue(hue), tx, ty, false);
        ++idx;
    };
    for (int x = x0; x < x1; x += kBorderTile) drawAt(x, y0);                         // top
    for (int y = y0 + kBorderTile; y < y1 - kBorderTile; y += kBorderTile) drawAt(x1 - kBorderTile, y); // right
    for (int x = x1 - kBorderTile; x >= x0; x -= kBorderTile) drawAt(x, y1 - kBorderTile);              // bottom
    for (int y = y1 - 2 * kBorderTile; y >= y0 + kBorderTile; y -= kBorderTile) drawAt(x0, y);           // left
}
} // namespace

void MenuScene::drawBootScreenBackground(pixelroot32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    namespace spr = menu_pr32_sprites;
    namespace mfont = menu_pr32_font;

    // Solid black base every phase draws over — neutral color, safe via
    // Background context regardless of the real-panel R/B-swap issue.
    pr32::graphics::PaletteContext bgCtx = pr32::graphics::PaletteContext::Background;
    pr32::graphics::PaletteContext* prev = renderer.getRenderContext();
    renderer.setRenderContext(&bgCtx);
    renderer.drawFilledRectangle(0, 0, SCREEN_W, SCREEN_H, Color::Black);

    // Marquee split: the animated diagnostic-screen patterns only fill
    // below this line now, leaving a plain black band behind the logo —
    // like a real arcade cabinet's lit marquee sign sitting above the
    // actual game screen, instead of the pattern running right through
    // the logo text. kMarqueeSplitY sits just below the logo (drawn at
    // y=30, LOGO_H=32 tall, so it ends at y=62).
    constexpr int kMarqueeSplitY = 70;

    switch (bgPhase) {
        case kPhaseCrosshatch: {
            constexpr int kStep = 24;
            for (int x = 0; x <= SCREEN_W; x += kStep) {
                renderer.drawLine(x, kMarqueeSplitY, x, SCREEN_H, Color::Gray);
            }
            for (int y = kMarqueeSplitY; y <= SCREEN_H; y += kStep) {
                renderer.drawLine(0, y, SCREEN_W, y, Color::Gray);
            }
            int cy = kMarqueeSplitY + (SCREEN_H - kMarqueeSplitY) / 2;
            renderer.drawCircle(SCREEN_W / 2, cy, 60, Color::White);
            renderer.drawCircle(SCREEN_W / 2, cy, 30, Color::White);
            break;
        }
        case kPhaseColorBars: {
            // Primary red/green/blue repeating bars (see swatch_red/
            // green/blue in make_menu_pr32_sprites.py) — more pleasing
            // than the earlier cyan/orange/navy mix, still reads
            // instantly as a "test pattern."
            renderer.setRenderContext(prev); // back to sprite context for fillSwatch
            constexpr int kBars = 9;
            constexpr int kBarW = (SCREEN_W + kBars - 1) / kBars;
            const pr32::graphics::Sprite4bpp* cycle[3] = {
                &spr::MENU_SWATCH_RED, &spr::MENU_SWATCH_GREEN, &spr::MENU_SWATCH_BLUE
            };
            for (int i = 0; i < kBars; ++i) {
                fillSwatch(renderer, *cycle[i % 3], i * kBarW, kMarqueeSplitY, kBarW, SCREEN_H - kMarqueeSplitY);
            }
            renderer.setRenderContext(&bgCtx);
            break;
        }
        case kPhaseCheckerboard: {
            constexpr int kSq = 20;
            for (int y = kMarqueeSplitY; y < SCREEN_H; y += kSq) {
                for (int x = 0; x < SCREEN_W; x += kSq) {
                    bool on = ((x / kSq) + (y / kSq)) % 2 == 0;
                    if (on) renderer.drawFilledRectangle(x, y, kSq, kSq, Color::White);
                }
            }
            break;
        }
        case kPhaseRamCheck:
        case kPhaseRomCheck: {
            const char* const* lines = (bgPhase == kPhaseRamCheck) ? kRamLines : kRomLines;
            int count = (bgPhase == kPhaseRamCheck) ? kRamLineCount : kRomLineCount;
            constexpr int kLeftX = 14;
            const int kTopY = kMarqueeSplitY + 6;
            constexpr int kLineH = 15;
            for (int i = 0; i < count; ++i) {
                float lineStartS = i * kBgLineStaggerS;
                if (bgPhaseTimer < lineStartS) continue; // not started yet
                int y = kTopY + i * kLineH;
                float sinceStart = bgPhaseTimer - lineStartS;
                if (sinceStart < kBgScrambleS) {
                    char scrambled[32];
                    scrambleText(lines[i], scrambled, sizeof(scrambled));
                    renderer.drawText(scrambled, kLeftX, y, Color::Gray, 1, &mfont::FONT_SMALL);
                } else {
                    renderer.drawText(lines[i], kLeftX, y, Color::White, 1, &mfont::FONT_SMALL);
                }
            }
            break;
        }
    }

    renderer.setRenderContext(prev);
}

void MenuScene::draw(pixelroot32::graphics::Renderer& renderer) {
    using pr32::graphics::Color;
    namespace spr = menu_pr32_sprites;

    bool showBrightness = millis() < brightnessIndicatorUntilMs;
    int brightnessPct = (hardwareGetBrightness() * 100) / 255;

    drawBootScreenBackground(renderer);

    const auto& logoSprite = *spr::LOGO_FRAMES[logoFrame];
    renderer.drawSprite(logoSprite, SCREEN_W / 2 - logoSprite.width / 2, 30, false);

    // One bordered "card" panel behind the whole list, instead of a bare
    // highlight box popping in and out per-row (reported live as looking
    // bad on its own) — a 2px border faked with an outer/inner rect pair
    // (Renderer has no rounded-rect primitive), a dark navy interior so
    // text stays legible over the busy scrolling tile regardless of what
    // colors happen to be behind it, and enough internal padding that
    // rows don't feel cramped against the border. Drawn via the swatch
    // sprites (see fillSwatch()'s comment), not Color::Cyan/Navy.
    // Border thickness matches the swatch tile size (8px) exactly, and
    // every rect fillSwatch() draws (outer AND inner) is sized to a
    // clean multiple of 8 in both axes — fillSwatch() tiles whole 8x8
    // sprites and doesn't clip the last one to fit, so any rect that
    // ISN'T an exact multiple of 8 lets that overdraw silently paint
    // past the intended edge. That's exactly what was eating the bottom
    // border before (old 34px row + 14px*2 padding = 62px inner height,
    // not a multiple of 8 → the inner navy fill's last tile row
    // overdrew straight through the bottom cyan border). Also shrunk
    // substantially now that only one row shows at a time.
    constexpr int kPanelBorder = 8;         // rainbow border tile size (drawRainbowBorder draws full 8x8 tiles)
    constexpr int kPanelBorderVisible = 4;  // how much of each tile stays visible once navy cuts into it — half of kPanelBorder
    constexpr int kPanelInnerH = 24;  // just enough for one FONT_LARGE line
    constexpr int kPanelMarginX = 24;
    constexpr int kPanelX0 = kPanelMarginX;
    constexpr int kPanelX1 = SCREEN_W - kPanelMarginX;  // 192px wide, multiple of 8
    const int panelY0 = 130;
    const int panelY1 = panelY0 + kPanelBorder * 2 + kPanelInnerH;  // 40px tall

    // Animated rainbow border instead of solid cyan — synced to the same
    // logoFrame timer driving the logo's own sweep (see
    // drawRainbowBorder()'s comment). Thinned to kPanelBorderVisible by
    // letting the navy interior fill (drawn after, so it paints on top)
    // cut further into each 8x8 border tile than before, leaving only a
    // narrow ring of it showing — simpler and more robust than trying to
    // draw partial/clipped tiles, and since kPanelBorderVisible*2 (8) is
    // still a multiple of the tile size, the navy fill itself still
    // tiles perfectly with no overdraw of its own.
    float borderPhaseDeg = logoFrame * (360.0f / menu_pr32_sprites::LOGO_FRAME_COUNT);
    drawRainbowBorder(renderer, kPanelX0, panelY0, kPanelX1, panelY1, borderPhaseDeg);
    fillSwatch(renderer, spr::MENU_SWATCH_NAVY, kPanelX0 + kPanelBorderVisible, panelY0 + kPanelBorderVisible,
               kPanelX1 - kPanelX0 - kPanelBorderVisible * 2, panelY1 - panelY0 - kPanelBorderVisible * 2);

    // Selection used to be a full-row orange highlight bar, then a
    // left-side yellow chevron — dropped both now that only one entry
    // shows at a time (it's inherently "the selection"). These small
    // up/down chevron sprites instead hint "scrollable list, more
    // entries exist" — real baked-color sprites, not Color::Yellow via
    // drawText/drawFilledRectangle (that showed up BLUE on real
    // hardware once this menu's custom sprite palette is active — see
    // make_menu_pr32_sprites.py's CHEVRONS section). Drawn here in the
    // sprite-context part of draw(), not the later Background-context
    // text pass, since Sprite4bpp sprites need the sprite context.
    if (viewportTop > 0) {
        renderer.drawSprite(spr::MENU_CHEVRON_UP, SCREEN_W / 2 - spr::MENU_CHEVRON_UP.width / 2, panelY0 - 12, false);
    }
    if (viewportTop + kVisibleRows < kEntryCount) {
        renderer.drawSprite(spr::MENU_CHEVRON_DOWN, SCREEN_W / 2 - spr::MENU_CHEVRON_DOWN.width / 2, panelY1 + 4, false);
    }

    // Bottom hint bar — grown from a single line to three so it can
    // cover every control that isn't otherwise visible anywhere on
    // screen (brightness, mute, reset), not just select/start. A
    // separate floating card for the brightness hint was tried first
    // and reverted — its fixed width didn't fit "STICK L/R: BRIGHTNESS"
    // (text drew past the card into the busy background and became
    // unreadable), and it read as a bolted-on extra element rather than
    // belonging with the rest of the controls reference.
    constexpr int kHintH = 54;
    fillSwatch(renderer, spr::MENU_SWATCH_CYAN, 0, SCREEN_H - kHintH, SCREEN_W, kHintH);
    fillSwatch(renderer, spr::MENU_SWATCH_NAVY, 0, SCREEN_H - kHintH + 2, SCREEN_W, kHintH - 2);

    // Everything below is TEXT, drawn as a plain color primitive, not a
    // sprite — force it through the Background context so it resolves via
    // the engine's untouched default palette. Text colors here are all
    // neutral (White/Black/Gray, R==G==B) so they're unaffected by the
    // real-panel R/B swap that affected Cyan/Orange/Magenta above — no
    // swatch-sprite workaround needed for text.
    pr32::graphics::PaletteContext bgContext = pr32::graphics::PaletteContext::Background;
    pr32::graphics::PaletteContext* saved = renderer.getRenderContext();
    renderer.setRenderContext(&bgContext);

    namespace mfont = menu_pr32_font;

    int rowY = panelY0 + kPanelBorderVisible;
    for (int i = viewportTop; i < viewportTop + kVisibleRows; ++i) {
        // No left-side selection chevron anymore — with only one row
        // visible at a time (kVisibleRows == 1) the sole displayed entry
        // IS the selection by definition, so pointing at it would be
        // redundant. The up/down sprite chevrons drawn earlier (sprite
        // context, real baked color — see their comment) do the actual
        // job now: hinting "scrollable list," not "this one's picked."
        bool available = kEntries[i].available;
        Color textColor = available ? Color::White : Color::Gray;
        int textW = pr32::graphics::FontManager::textWidth(&mfont::FONT_LARGE, kEntries[i].name, 1);
        int nameX = SCREEN_W / 2 - textW / 2;
        renderer.drawText(kEntries[i].name, nameX, rowY + 6, textColor, 1, &mfont::FONT_LARGE);

        if (!available) {
            const char* soon = "(COMING SOON)";
            int soonW = pr32::graphics::FontManager::textWidth(&mfont::FONT_SMALL, soon, 1);
            renderer.drawText(soon, SCREEN_W / 2 - soonW / 2, rowY + 20, Color::Gray, 1, &mfont::FONT_SMALL);
        }

        rowY += kPanelInnerH + kPanelBorder * 2;
    }

    // Three-line hint bar: every control that has no other on-screen
    // indicator, in one place. Line 2 shows the live percentage while
    // actually adjusting brightness (still useful feedback), falling
    // back to a static reminder the rest of the time.
    auto drawHintLine = [&](const char* text, int y, pr32::graphics::Color color) {
        int w = pr32::graphics::FontManager::textWidth(&mfont::FONT_SMALL, text, 1);
        renderer.drawText(text, SCREEN_W / 2 - w / 2, y, color, 1, &mfont::FONT_SMALL);
    };

    // This bitmap font has no bold weight, so "bold" here is the classic
    // pixel-art fake: draw the same text twice, offset 1px horizontally
    // — thickens the strokes without needing a whole second font asset.
    // Used for "HOLD" so it's visually obvious those two actions need a
    // sustained press, not a tap, unlike everything else in this bar.
    auto drawHintLineBoldPrefix = [&](const char* bold, const char* rest, int y, pr32::graphics::Color color) {
        char combined[40];
        snprintf(combined, sizeof(combined), "%s %s", bold, rest);
        int totalW = pr32::graphics::FontManager::textWidth(&mfont::FONT_SMALL, combined, 1);
        int x = SCREEN_W / 2 - totalW / 2;
        renderer.drawText(bold, x, y, color, 1, &mfont::FONT_SMALL);
        renderer.drawText(bold, x + 1, y, color, 1, &mfont::FONT_SMALL);
        int boldW = pr32::graphics::FontManager::textWidth(&mfont::FONT_SMALL, bold, 1);
        int spaceW = pr32::graphics::FontManager::textWidth(&mfont::FONT_SMALL, " ", 1);
        renderer.drawText(rest, x + boldW + spaceW + 1, y, color, 1, &mfont::FONT_SMALL);
    };

    const int hintTop = SCREEN_H - kHintH;
    drawHintLine("ROTATE:SELECT  A:START", hintTop + 6, Color::White);

    if (showBrightness) {
        char buf[20];
        snprintf(buf, sizeof(buf), "BRIGHTNESS %d%%", brightnessPct);
        drawHintLine(buf, hintTop + 20, Color::White);
    } else {
        drawHintLine("L/R:BRIGHTNESS", hintTop + 20, Color::White);
    }

    drawHintLineBoldPrefix("HOLD", "LB:MUTE  RB+LB:RESET", hintTop + 34, Color::White);

    renderer.setRenderContext(saved);
}

} // namespace menu_pr32
