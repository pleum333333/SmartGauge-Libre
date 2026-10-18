# SmartGauge Libre — Copyright (c) 2026 khanatuch — code written by khanatuch & Claude (AI)
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stand-in for the phone: connects to the gauge over BLE and sends turn hints.

Needs a PC with Bluetooth LE and `pip install bleak`.

  python tools/nav_send.py                         # play a short route, 1 update/s
  python tools/nav_send.py --text "N|left|300|14:32|Rama IV Rd"
  python tools/nav_send.py --text "N||250||Turn right onto Sukhumvit Rd"   # turn guessed from text
  python tools/nav_send.py --icon                  # send a 48x48 test bitmap, then turn "icon"
  python tools/nav_send.py --end                   # "navigation ended"
  python tools/nav_send.py --rate 10 --seconds 30  # stress: 10 writes/s, prints write round-trip times

Protocol (see OBD_Dashboard/nav.h): NAV characteristic takes UTF-8 "N|turn|metres|eta|street" or "X";
ICON characteristic takes [offset lo, offset hi, bytes...] chunks of a 288-byte 1-bit 48x48 bitmap.
"""
import argparse
import asyncio
import statistics
import time

from bleak import BleakClient, BleakScanner

SERVICE = "5a4e0001-8f6b-4c1e-9a43-6d0c5e7a1b20"
NAV = "5a4e0002-8f6b-4c1e-9a43-6d0c5e7a1b20"
ICON = "5a4e0003-8f6b-4c1e-9a43-6d0c5e7a1b20"

ROUTE = [("straight", "Rama IV Rd", 700), ("left", "Sukhumvit Rd", 500), ("slight-right", "Expressway ramp", 300),
         ("roundabout", "2nd exit", 200), ("right", "Soi 24", 250), ("arrive", "Destination on right", 60)]


def test_icon():
    """A right-pointing arrow as 48x48 1-bit, rows MSB first."""
    bits = bytearray(288)
    for y in range(48):
        for x in range(48):
            if (6 <= x < 28 and 19 <= y <= 28) or (28 <= x <= 44 and abs(y - 24) <= 44 - x):
                bits[y * 6 + x // 8] |= 0x80 >> (x % 8)
    return bytes(bits)


async def write(client, uuid, data, response=True):
    t0 = time.perf_counter()
    await client.write_gatt_char(uuid, data, response=response)
    return (time.perf_counter() - t0) * 1000


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--text", help="one NAV message to send")
    ap.add_argument("--end", action="store_true", help='send "X" (navigation ended)')
    ap.add_argument("--icon", action="store_true", help="send the test bitmap and show it")
    ap.add_argument("--rate", type=float, default=1.0, help="route updates per second")
    ap.add_argument("--seconds", type=float, default=40, help="how long to play the route")
    ap.add_argument("--hold", type=float, default=0, help="stay connected this long after sending")
    args = ap.parse_args()

    print("scanning for the gauge ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: SERVICE in [u.lower() for u in ad.service_uuids], timeout=15)
    if not dev:
        raise SystemExit("gauge not found (is it powered and advertising?)")
    print(f"found {dev.address} {dev.name!r}, connecting ...")
    t0 = time.perf_counter()
    client = BleakClient(dev, timeout=20)
    for attempt in range(3):  # Windows service discovery sometimes times out while the gauge scans for the ELM327
        try:
            await client.connect()
            break
        except (TimeoutError, OSError) as e:
            print(f"connect attempt {attempt + 1} failed: {type(e).__name__}")
    else:
        raise SystemExit("could not connect")
    async with client:
        print(f"connected in {(time.perf_counter() - t0):.1f} s, MTU {client.mtu_size}")
        if args.end:
            await write(client, NAV, b"X")
        elif args.text:
            print(f"write {await write(client, NAV, args.text.encode()):.0f} ms")
        elif args.icon:
            icon = test_icon()
            step = max(20, client.mtu_size - 3 - 2)
            for off in range(0, len(icon), step):
                chunk = icon[off:off + step]
                await write(client, ICON, bytes([off & 255, off >> 8]) + chunk, response=False)
            await asyncio.sleep(0.2)
            await write(client, NAV, b"N|icon|180|14:32|Test bitmap")
            print("icon sent")
        else:
            times, step, left = [], 0, ROUTE[0][2]
            end = time.time() + args.seconds
            while time.time() < end:
                turn, street, _ = ROUTE[step]
                eta = time.strftime("%H:%M", time.localtime(time.time() + 600))
                times.append(await write(client, NAV, f"N|{turn}|{int(left)}|{eta}|{street}".encode()))
                await asyncio.sleep(max(0, 1 / args.rate - times[-1] / 1000))
                left -= 15 / args.rate  # ~54 km/h
                if left <= 0:
                    step = (step + 1) % len(ROUTE)
                    left = ROUTE[step][2]
            await write(client, NAV, b"X")
            times.sort()
            print(f"{len(times)} writes, round trip ms: median {statistics.median(times):.0f}, "
                  f"p95 {times[int(len(times) * 0.95) - 1]:.0f}, max {times[-1]:.0f}")
        if args.hold:
            await asyncio.sleep(args.hold)


if __name__ == "__main__":
    asyncio.run(main())
