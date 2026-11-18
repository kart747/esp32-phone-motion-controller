#!/usr/bin/env python3
"""Fake phone: sends CSV sensor packets over UDP so the ESP32 can be tested
without the real phone.

Packet format (matches the firmware default column order in config.h):
    timestamp_ms,ax,ay,az,gx,gy,gz
Units: accel in m/s^2 (INCLUDING gravity), gyro in rad/s - same as Android's
TYPE_ACCELEROMETER / TYPE_GYROSCOPE.

Provisional axis convention (finalised in docs/axes.md in a later phase):
phone held FLAT, screen up, top edge pointing away from you, so a still phone
reads az ~ +9.81 and the gravity vector is (0, 0, 1). This is the neutral pose
the firmware calibrates to. A forehand swing is a gyro pulse about z (the
vertical, +) with a matching sideways x acceleration burst; a backhand is the
same pulse with the opposite sign (-).

Scenarios: still | forehand | backhand | noise | sequence

Examples:
    python3 tools/phone_sim.py --host 192.168.4.1 --scenario still --duration 5
    python3 tools/phone_sim.py --scenario sequence
"""
import argparse
import math
import random
import socket
import time

GRAVITY = 9.81                 # m/s^2
ACCEL_NOISE_STD = 0.05         # m/s^2, sensor noise while "still"
GYRO_NOISE_STD = 0.01          # rad/s
GYRO_BIAS = (0.02, -0.01, 0.015)  # constant gyro offset, to exercise calibration
SWING_DURATION_S = 0.25        # length of the swing pulse
SWING_PEAK_GYRO = 12.0         # rad/s
SWING_PEAK_ACCEL = 25.0        # m/s^2 on top of gravity
NOISE_SCENARIO_ACCEL_STD = 0.8   # shaky hand, below any swing threshold
NOISE_SCENARIO_GYRO_STD = 0.25
SEQUENCE_PLAN = [("still", 2.0), ("forehand", 0.6), ("still", 1.5),
                 ("backhand", 0.6), ("still", 1.5)]


def sample(scenario: str, t: float):
    """Return (ax, ay, az, gx, gy, gz) for `scenario` at time t (seconds
    since the scenario started)."""
    ax, ay, az = 0.0, 0.0, GRAVITY
    gx, gy, gz = GYRO_BIAS
    a_std, g_std = ACCEL_NOISE_STD, GYRO_NOISE_STD

    if scenario in ("forehand", "backhand") and 0.0 <= t < SWING_DURATION_S:
        sign = 1.0 if scenario == "forehand" else -1.0
        # Half-sine envelope: smooth rise and fall like a real swing.
        env = math.sin(math.pi * t / SWING_DURATION_S)
        gz += sign * SWING_PEAK_GYRO * env
        ax += sign * SWING_PEAK_ACCEL * env
    elif scenario == "noise":
        a_std, g_std = NOISE_SCENARIO_ACCEL_STD, NOISE_SCENARIO_GYRO_STD

    r = random.gauss
    return (ax + r(0, a_std), ay + r(0, a_std), az + r(0, a_std),
            gx + r(0, g_std), gy + r(0, g_std), gz + r(0, g_std))


def build_plan(args):
    if args.scenario == "sequence":
        return SEQUENCE_PLAN
    return [(args.scenario, args.duration)]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1", help="ESP32 IP (SoftAP default)")
    ap.add_argument("--port", type=int, default=5555)
    ap.add_argument("--rate", type=float, default=100.0, help="packets per second")
    ap.add_argument("--scenario", default="still",
                    choices=["still", "forehand", "backhand", "noise", "sequence"])
    ap.add_argument("--duration", type=float, default=5.0,
                    help="seconds (ignored for 'sequence')")
    ap.add_argument("--loop", action="store_true", help="repeat forever")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    period = 1.0 / args.rate
    t0_global = time.monotonic()
    sent = 0
    print(f"Sending '{args.scenario}' to {dest} at {args.rate:g} Hz (Ctrl+C to stop)")

    try:
        while True:
            for name, length in build_plan(args):
                seg_start = time.monotonic()
                next_send = seg_start
                print(f"  -> {name} for {length:g}s")
                while (now := time.monotonic()) - seg_start < length:
                    vals = sample(name, now - seg_start)
                    ts_ms = int((now - t0_global) * 1000)
                    line = f"{ts_ms}," + ",".join(f"{v:.4f}" for v in vals)
                    sock.sendto(line.encode("ascii"), dest)
                    sent += 1
                    next_send += period
                    time.sleep(max(0.0, next_send - time.monotonic()))
            if not args.loop:
                break
    except KeyboardInterrupt:
        pass
    print(f"Done. Sent {sent} packets.")


if __name__ == "__main__":
    main()
