"""
Reusable remote test-rig helpers for driving the console over serial
without physical access.

Key discovery (2026-09-12 session): opening a new serial.Serial() to this
board's CP2102 adapter resets it via the DTR/RTS auto-reset circuit
**inconsistently** -- whether it fires depends on the DTR/RTS line state
the *previous* connection left the port in, which a script doesn't
control. So neither "assume a reset happened" nor "assume it didn't" is
safe, and waiting for a boot banner hangs forever on the (common) case
where no reset occurs. The reliable fix isn't in the test script at all:
main.cpp has a debug command, uppercase `W`, that toggles a sustained-
hold latch simulating a long encoder-switch press -- holding it past the
global ~2s "return to menu" threshold forces an immediate, deterministic
return to the menu regardless of current state or whether a reset just
happened. Every helper here goes through that instead of relying on
connection-open timing. (Earlier versions of this file referenced a
lowercase `m` command; no such command exists in main.cpp's readInput
switch -- that was a documentation error, fixed 2026-09-13.)

Usage:
    from rig import open_console, nav_to_game, screenshot, tap, hold_latch

    s = open_console()             # settles + forces a known menu state
    nav_to_game(s, 3)              # 0=BubbleBobble 1=PacMan 2=Galaga 3=Arkanoid 4=BlockStack
    tap(s, 's')                    # shoot / KEY0
    screenshot(s, "out.png")
    s.close()
"""
import serial
import time
import re
from PIL import Image

PORT = '/dev/ttyUSB0'
BAUD = 115200


def open_console(port=PORT, baud=BAUD, settle=1.0):
    """Opens the serial connection, waits a moment for the port (and any
    reset it happened to trigger) to settle, then holds the 'W' latch
    past the global return-to-menu threshold to deterministically land
    at the menu -- regardless of whether this connection reset the board
    or not, and regardless of whatever state a previous connection left
    it in. If a reset just happened, this is a harmless no-op sent to
    the already-showing menu (the latch check only fires while a game
    scene is active)."""
    s = serial.Serial(port, baud, timeout=0.2)
    time.sleep(settle)
    s.reset_input_buffer()
    hold_latch(s, 'W', hold_s=2.6)
    s.reset_input_buffer()
    return s


def tap(s, ch: str, settle=0.05):
    """One-frame press of a debug char (see main.cpp's readInput debug switch)."""
    s.write(ch.encode())
    time.sleep(settle)


def hold_latch(s, ch: str, hold_s=3.5):
    """Toggles a sustained-hold latch on, waits, toggles it off. Uppercase
    S/W/L only. hold_s should stay comfortably above the mechanic's real
    threshold (e.g. >2.5s for the 2s global L3 exit) since some of that
    window may be spent on other work before this call reaches the board."""
    s.write(ch.encode())
    time.sleep(hold_s)
    s.write(ch.encode())
    time.sleep(0.3)


def nav_to_game(s, target_index: int, confirm=True):
    """Assumes the menu is currently showing with index 0 (BUBBLE BOBBLE)
    selected (true right after open_console(), which lands here via the
    'W' hold-latch return-to-menu) and moves the selection to
    target_index, optionally confirming with KEY0. 4 encoder ticks are
    needed per menu step (menu.cpp's ENC_STEP_THRESHOLD), with a pause
    after each group to clear MOVE_COOLDOWN_MS=200. Current menu order
    (menu_pr32.cpp, checked 2026-09-13): 0=BubbleBobble 1=PacMan
    2=Galaga 3=Arkanoid 4=BlockStack -- re-check this list before relying
    on it if entries are ever added/reordered."""
    for _ in range(target_index):
        for _ in range(4):
            s.write(b'd')
            time.sleep(0.05)
        time.sleep(0.3)
    if confirm:
        time.sleep(0.2)
        s.write(b's')
        time.sleep(0.8)


def screenshot(s, path, timeout=60.0, swap_rb=False):
    """Sends 'p', decodes the RGB332 dump, saves a PNG. Returns True/False.

    swap_rb: this rig reads the internal software framebuffer, which is
    provably correct for Bubble Bobble's own sprites (its
    make_bubblebobble_pr32_sprites.py palette is un-swapped and confirmed
    right on the real panel) but NOT for anything drawn through a
    custom sprite palette that was deliberately R/B-swapped this session
    to compensate a real-panel-only color mismatch (menu, Pac-Man's own
    sprites, the Pac-Man maze bitmap — see arcade_menu_pacman.md) — for
    those, this buffer read shows the intentionally "pre-compensated"
    colors, which the user confirmed live on the real device look
    correct, but which look wrong here unless swapped back. Pass
    swap_rb=True when screenshotting the menu or Pac-Man scenes so this
    tool's own output matches what the user actually sees; leave it
    False for Bubble Bobble (or anything else using an un-swapped
    palette) where the raw buffer read is already correct. There's no
    way to auto-detect which scene is active from here, so this has to
    be set by the caller.
    """
    s.reset_input_buffer()
    s.write(b'p')
    header = b""
    start = time.time()
    while time.time() - start < timeout:
        b = s.read(1)
        if not b:
            continue
        header += b
        if header.endswith(b"\n") and b"SCR_BEGIN" in header:
            break
        # The buzzer's "LEDC/DC is not initialized" spam (hardware.h,
        # buzzer_audio.h -- known, left-alone transport artifact) can
        # print continuously while a game scene is active, so a small
        # cap here gives up before SCR_BEGIN ever arrives. Keep a large
        # cap just as a sanity backstop against a truly wedged connection,
        # not as the normal exit path.
        if len(header) > 2_000_000:
            break
    m = re.search(rb"SCR_BEGIN (\d+) (\d+) (\d+) (\w+)", header)
    if not m:
        print("screenshot() HEADER FAIL:", header[-500:])
        return False
    w, h, nbytes, fmt = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4).decode()
    payload = b""
    start = time.time()
    while len(payload) < nbytes and time.time() - start < 15:
        chunk = s.read(nbytes - len(payload))
        if chunk:
            payload += chunk
    if len(payload) != nbytes:
        print(f"screenshot() short read: {len(payload)}/{nbytes}")
        return False
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        row = y * w
        for x in range(w):
            v = payload[row + x]
            r3, g3, b2 = (v >> 5) & 0x07, (v >> 2) & 0x07, v & 0x03
            r, g, b = r3 * 255 // 7, g3 * 255 // 7, b2 * 255 // 3
            if swap_rb:
                r, b = b, r
            px[x, y] = (r, g, b)
    img.save(path)
    return True
