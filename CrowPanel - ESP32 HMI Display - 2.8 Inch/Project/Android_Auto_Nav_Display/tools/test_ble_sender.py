#!/usr/bin/env python3
"""
test_ble_sender.py - bench test "fake phone" for the AA Nav Display.

Sends scripted Google Maps-style turn-by-turn JSON over BLE (Nordic UART
Service) so the display can be tested on a desk, without a phone.

Requires:  pip install bleak
Usage:
    python test_ble_sender.py                       # loop scenario
    python test_ble_sender.py --scenario approach   # one approach + turn
    python test_ble_sender.py --scenario reroute    # nav -> reroute -> nav
    python test_ble_sender.py --scenario arrive     # approach -> arrived
    python test_ble_sender.py --address XX:XX:XX:XX:XX:XX --speed 60

The display must be powered and advertising as "AA Nav Display" (pairing in
Windows/phone Bluetooth settings is optional for NUS - this script connects
without bonding).
"""

import argparse
import asyncio
import json
import sys

from bleak import BleakClient, BleakScanner

SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
CHAR_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # write: us -> display
CHAR_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # notify: display -> us

LEGS = [
    {"m": "TR", "st": "Main Street", "th": "NH 48", "step": 12},
    {"m": "ST", "st": "NH 48", "th": "Airport Road", "step": 20},
    {"m": "SL", "st": "Airport Road", "th": "5th Cross", "step": 9},
]


def nav_msg(code, meters, street, then_street, speed):
    return json.dumps({
        "t": "nav",
        "m": code,
        "d": int(meters),
        "ds": fmt_distance(meters),
        "st": street,
        "th": then_street,
        "eta": "14:32",
        "rt": "12.4 km",
        "v": int(speed),
        "s": "nav",
    })


def fmt_distance(m):
    if m >= 1000:
        return f"{m / 1000:.1f} km"
    return f"{int(m)} m"


async def notify_handler(_char, data: bytearray):
    print(f"  <- display: {data.decode(errors='replace')}")


async def find_device(name, address):
    print("Scanning for the display ...")
    if address:
        dev = await BleakScanner.find_device_by_address(address, timeout=15)
    else:
        dev = await BleakScanner.find_device_by_name(name, timeout=15)
    if dev is None:
        target = address or name
        print(f"ERROR: device '{target}' not found.", file=sys.stderr)
        print("Check that the display is powered, advertising (serial log shows", file=sys.stderr)
        print("'advertising as'), and that this PC's Bluetooth is on.", file=sys.stderr)
    return dev


async def send_leg(client, leg, speed, interval, end_at_zero=True):
    meters = 1200
    while meters > 0 or not end_at_zero:
        msg = nav_msg(leg["m"], meters, leg["st"], leg["th"], speed)
        print(f"  -> {msg}")
        await client.write_gatt_char(CHAR_RX_UUID, (msg + "\n").encode())
        await asyncio.sleep(interval)
        meters -= leg["step"]
        if end_at_zero and meters <= 0:
            msg = nav_msg(leg["m"], 0, leg["st"], leg["th"], speed)
            print(f"  -> {msg}")
            await client.write_gatt_char(CHAR_RX_UUID, (msg + "\n").encode())
            break


async def run(name, address, scenario, interval, speed):
    dev = await find_device(name, address)
    if dev is None:
        return 1

    print(f"Connecting to {dev.name} [{dev.address}] ...")
    async with BleakClient(dev, disconnected_callback=lambda c: print("!! disconnected")) as client:
        print("Connected.")
        await client.start_notify(CHAR_TX_UUID, notify_handler)

        if scenario == "approach":
            await send_leg(client, LEGS[0], speed, interval)
            await client.write_gatt_char(CHAR_RX_UUID, b'{"t":"state","s":"idle"}\n')
        elif scenario == "reroute":
            await send_leg(client, LEGS[0], speed, interval)
            print("  -> state: reroute")
            await client.write_gatt_char(CHAR_RX_UUID, b'{"t":"state","s":"reroute"}\n')
            await asyncio.sleep(4)
            await send_leg(client, LEGS[2], speed, interval)
        elif scenario == "arrive":
            await send_leg(client, LEGS[1], speed, interval)
            msg = nav_msg("ARR", 0, "Destination", "", speed)
            print(f"  -> {msg}")
            await client.write_gatt_char(CHAR_RX_UUID, (msg + "\n").encode())
            await asyncio.sleep(1)
            await client.write_gatt_char(CHAR_RX_UUID, b'{"t":"state","s":"arrived"}\n')
        else:  # loop
            i = 0
            while True:
                await send_leg(client, LEGS[i % len(LEGS)], speed, interval)
                i += 1

        await client.stop_notify(CHAR_TX_UUID)
    print("Done.")
    return 0


def main():
    ap = argparse.ArgumentParser(description="BLE bench test sender for AA Nav Display")
    ap.add_argument("--name", default="AA Nav Display", help="BLE device name")
    ap.add_argument("--address", default=None, help="BLE MAC/UUID address (skips scan)")
    ap.add_argument("--scenario", default="loop",
                    choices=["loop", "approach", "reroute", "arrive"])
    ap.add_argument("--interval", type=float, default=1.0, help="seconds between updates")
    ap.add_argument("--speed", type=int, default=42, help="simulated speed km/h")
    args = ap.parse_args()

    try:
        sys.exit(asyncio.run(run(args.name, args.address, args.scenario,
                                 args.interval, args.speed)))
    except KeyboardInterrupt:
        print("\nStopped.")
        sys.exit(0)


if __name__ == "__main__":
    main()
