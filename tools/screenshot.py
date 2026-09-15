#!/usr/bin/env python3
"""Trigger a screenshot capture on the ESP32 game console over serial and
save it as a PNG. Usage: python3 screenshot.py <output.png> [port]"""
import serial, time, re, sys
from PIL import Image

def capture(out_path, port='/dev/ttyUSB0', baud=115200):
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(b'p')

    header = b""
    start = time.time()
    while time.time() - start < 5:
        b = ser.read(1)
        if not b:
            continue
        header += b
        if header.endswith(b"\n") and b"SCR_BEGIN" in header:
            break
        if len(header) > 2000:
            break

    m = re.search(rb"SCR_BEGIN (\d+) (\d+) (\d+) (\w+)", header)
    if not m:
        ser.close()
        raise RuntimeError(f"no SCR_BEGIN header found; got: {header!r}")

    w, h, nbytes, fmt = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4).decode()

    payload = b""
    start = time.time()
    while len(payload) < nbytes and time.time() - start < 15:
        chunk = ser.read(nbytes - len(payload))
        if chunk:
            payload += chunk
    ser.close()

    if len(payload) != nbytes:
        raise RuntimeError(f"short read: got {len(payload)} of {nbytes} bytes")
    if fmt != "RGB332":
        raise RuntimeError(f"unexpected format: {fmt}")

    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        row = y * w
        for x in range(w):
            v = payload[row + x]
            r3, g3, b2 = (v >> 5) & 0x07, (v >> 2) & 0x07, v & 0x03
            px[x, y] = (r3 * 255 // 7, g3 * 255 // 7, b2 * 255 // 3)
    img.save(out_path)
    return out_path

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'screenshot.png'
    port = sys.argv[2] if len(sys.argv) > 2 else '/dev/ttyUSB0'
    print(capture(out, port))
