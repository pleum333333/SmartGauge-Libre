# SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Grab a screenshot from the dashboard over USB serial and save it as PNG.

Usage:
  python tools/screenshot.py COM3 out.png              # current screen
  python tools/screenshot.py COM3 out.png --cmd D      # toggle demo first
  python tools/screenshot.py COM3 out.png --cmd DNN    # demo, then two pages forward
  python tools/screenshot.py COM3 out.png --le         # if colours look wrong

Firmware side: handleSerial() in OBD_Dashboard.ino (S = screenshot, D = demo, N/P = page).
Needs: pip install pyserial pillow
"""
import argparse
import sys
import time

import serial
from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("--cmd", default="", help="commands to send before the shot, e.g. DN")
    ap.add_argument("--wait", type=float, default=4.0, help="seconds to wait after opening (board may reset)")
    ap.add_argument("--le", action="store_true", help="pixels are little-endian (default: high byte first)")
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 5
    ser.dtr = False
    ser.rts = False
    ser.open()
    time.sleep(args.wait)
    ser.reset_input_buffer()

    for c in args.cmd:
        ser.write(c.encode())
        time.sleep(1.5)
    ser.reset_input_buffer()

    ser.write(b"S")
    while True:
        line = ser.readline()
        if not line:
            sys.exit("no SHOT header received")
        if line.startswith(b"SHOT "):
            break
    w, h = map(int, line.split()[1:3])
    need = w * h * 2
    data = bytearray()
    deadline = time.time() + 60
    while len(data) < need and time.time() < deadline:
        data += ser.read(need - len(data))
    ser.close()
    if len(data) < need:
        sys.exit(f"short read: {len(data)}/{need} bytes")

    img = Image.new("RGB", (w, h))
    px = img.load()
    for i in range(w * h):
        lo, hi = data[2 * i], data[2 * i + 1]
        v = (hi << 8 | lo) if args.le else (lo << 8 | hi)
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px[i % w, i // w] = (r * 255 // 31, g * 255 // 63, b * 255 // 31)
    img.save(args.out)
    print(f"saved {args.out} ({w}x{h})")


if __name__ == "__main__":
    main()
