import serial, time, re, sys
from PIL import Image

PORT = '/dev/ttyUSB0'
BAUD = 115200


def connect():
    # No 'm' debug command exists in main.cpp; use the real 'W' hold-latch
    # mechanism (same fix applied to rig.py 2026-09-13) to deterministically
    # land at the menu regardless of prior board state.
    s = serial.Serial(PORT, BAUD, timeout=0.2)
    time.sleep(0.5)
    s.reset_input_buffer()
    s.write(b'W')
    time.sleep(2.6)
    s.write(b'W')
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def tap(s, ch, settle=0.05):
    s.write(ch.encode())
    time.sleep(settle)


def shot(s, path):
    s.reset_input_buffer()
    s.write(b'p')
    header = b''
    start = time.time()
    while time.time() - start < 15:
        b = s.read(4096)
        if not b:
            continue
        header += b
        if b'SCR_BEGIN' in header:
            break
        if len(header) > 2_000_000:
            break
    m = re.search(rb'SCR_BEGIN (\d+) (\d+) (\d+) (\w+)\n', header)
    if not m:
        print('HEADER NOT FOUND, scanned', len(header))
        return False
    w, h, nbytes = int(m.group(1)), int(m.group(2)), int(m.group(3))
    payload = header[m.end():]
    start = time.time()
    while len(payload) < nbytes and time.time() - start < 15:
        chunk = s.read(nbytes - len(payload))
        if chunk:
            payload += chunk
    if len(payload) != nbytes:
        print('short payload', len(payload), nbytes)
        return False
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        row = y * w
        for x in range(w):
            v = payload[row + x]
            r3, g3, b2 = (v >> 5) & 7, (v >> 2) & 7, v & 3
            r, g, b = r3 * 255 // 7, g3 * 255 // 7, b2 * 255 // 3
            px[x, y] = (r, g, b)
    img.save(path)
    return True


if __name__ == '__main__':
    s = connect()
    # navigate to Arkanoid = index 3 (Bubble Bobble, Pac-Man, Galaga, Arkanoid)
    for _ in range(3):
        for _ in range(4):
            tap(s, 'd')
        time.sleep(0.3)
    tap(s, 's', 0.5)
    shot(s, '/tmp/ark_title.png')
    time.sleep(4.5)
    shot(s, '/tmp/ark_story.png')
    time.sleep(4.5)
    shot(s, '/tmp/ark_round.png')
    time.sleep(1.8)
    shot(s, '/tmp/ark_gameplay.png')
    s.close()
