#!/usr/bin/env python3
"""Record labelled sensor/swing data from the ESP32 over USB serial.

Turn streaming on first (`tune on` in `pio device monitor`, or this script will
send it for you), then swing the phone and label each stroke. The recording is
written to data/ as CSV, which tools/plot_swings.py and tools/replay_swings.py
can read back.

Firmware records (one per line):
    S,<rx_ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<up_x>,<up_y>,<up_z>,<roll>,<pitch>,<dt>
    W,<t_ms>,<duration_ms>,<peak_gyro>,<peak_lin_acc>,<peak_up_acc>,
      <int_yaw>,<int_pitch>,<int_roll>,<onset_pitch>,<gesture>,<reason>
    T,<key>=<value>            (tunables in force for this session)

Everything else on the serial line is human-readable logging and is ignored, so
raw/angles/stats output can stay on without breaking the recording.

Examples:
    python3 tools/serial_logger.py --port /dev/ttyUSB0 --label forehand
    python3 tools/serial_logger.py --label forehand --count 20
    python3 tools/serial_logger.py --no-tune          # leave the firmware alone
"""
import argparse
import csv
import datetime as dt
import pathlib
import re
import sys
import time

SAMPLE_FIELDS = ["rx_ms", "ax", "ay", "az", "gx", "gy", "gz",
                 "up_x", "up_y", "up_z", "roll_deg", "pitch_deg", "dt_s", "calibrating"]
SWING_FIELDS = ["t_ms", "duration_ms", "peak_gyro", "peak_lin_acc", "peak_up_acc",
                "int_yaw", "int_pitch", "int_roll", "onset_pitch_deg", "gesture", "reason"]

GESTURES = ("forehand", "backhand", "lob", "serve")


def parse_record(line: str):
    """Return (kind, payload) for a firmware record, or (None, None)."""
    line = line.strip()
    if len(line) < 2 or line[1] != ",":
        return None, None
    kind, rest = line[0], line[2:]
    if kind == "S":
        parts = rest.split(",")
        if len(parts) != len(SAMPLE_FIELDS):
            return None, None
        try:
            return "S", [float(p) for p in parts]
        except ValueError:
            return None, None
    if kind == "W":
        parts = rest.split(",")
        if len(parts) != len(SWING_FIELDS):
            return None, None
        row = parts[:-2]
        try:
            values = [float(p) for p in row]
        except ValueError:
            return None, None
        return "W", values + [parts[-2], parts[-1]]
    if kind == "T":
        return "T", rest
    return None, None


def open_serial(port: str, baud: int):
    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("pyserial is required: pip install pyserial "
                 "(see tools/requirements.txt)")
    try:
        return serial.Serial(port, baud, timeout=0.2)
    except Exception as exc:  # permission, busy, wrong port
        sys.exit(f"could not open {port}: {exc}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--label", default="unlabelled",
                    help="stroke label for this session, e.g. forehand")
    ap.add_argument("--count", type=int, default=0,
                    help="stop after this many recognised swings (0 = until Ctrl+C)")
    ap.add_argument("--duration", type=float, default=0.0,
                    help="stop after this many seconds (0 = until Ctrl+C)")
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--note", default="", help="free-text note stored in the filename")
    ap.add_argument("--no-tune", action="store_true",
                    help="do not send 'tune on' / 'tune off'")
    args = ap.parse_args()

    ser = open_serial(args.port, args.baud)
    if not args.no_tune:
        time.sleep(0.3)
        ser.write(b"tune on\n")
        time.sleep(0.2)
        ser.write(b"get\n")  # record which thresholds produced this session

    outdir = pathlib.Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    safe_note = re.sub(r"[^A-Za-z0-9._-]+", "-", args.note).strip("-")
    name = f"{stamp}_{args.label}" + (f"_{safe_note}" if safe_note else "")
    samples_path = outdir / f"{name}_samples.csv"
    swings_path = outdir / f"{name}_swings.csv"

    sample_file = open(samples_path, "w", newline="")
    swing_file = open(swings_path, "w", newline="")
    sample_writer = csv.writer(sample_file)
    swing_writer = csv.writer(swing_file)
    sample_writer.writerow(["label", "session"] + SAMPLE_FIELDS)
    swing_writer.writerow(["label", "session"] + SWING_FIELDS)

    unparsed = 0
    swings = 0
    samples = 0
    tunables = []
    started = time.monotonic()
    interrupted = False

    print(f"recording '{args.label}' to {samples_path}  (Ctrl+C to stop)")
    try:
        while True:
            raw = ser.readline()
            if not raw:
                if args.duration and time.monotonic() - started >= args.duration:
                    break
                continue
            line = raw.decode("utf-8", "replace")
            kind, payload = parse_record(line)
            if kind == "S":
                sample_writer.writerow([args.label, name] + payload)
                samples += 1
            elif kind == "W":
                swing_writer.writerow([args.label, name] + payload)
                swings += 1
                gesture = payload[-2]
                print(f"  swing {swings}: {gesture}"
                      f"{' (reason: ' + payload[-1] + ')' if payload[-1] else ''}")
                if args.count and swings >= args.count:
                    break
            elif kind == "T":
                tunables.append(payload)
            elif line.strip():
                unparsed += 1
            if args.duration and time.monotonic() - started >= args.duration:
                break
    except KeyboardInterrupt:
        interrupted = True
    finally:
        if not args.no_tune:
            ser.write(b"tune off\n")
        ser.close()
        sample_file.close()
        swing_file.close()

    meta_path = outdir / f"{name}_meta.txt"
    with open(meta_path, "w") as f:
        f.write(f"label: {args.label}\n")
        f.write(f"recorded: {dt.datetime.now().isoformat(timespec='seconds')}\n")
        f.write(f"port: {args.port} baud: {args.baud}\n")
        f.write(f"samples: {samples}\nswings: {swings}\n")
        f.write(f"non-record serial lines ignored: {unparsed}\n")
        f.write(f"interrupted: {interrupted}\n")
        f.write("tunables in force:\n")
        for t in tunables:
            f.write(f"  {t}\n")

    print(f"\ndone: {samples} samples, {swings} swings -> {samples_path.name}")
    print(f"      tunables recorded in {meta_path.name}")
    if samples == 0:
        print("\nWARNING: no sample records arrived. Is the firmware running the "
              "Phase 5 build, and is 'tune on' accepted? Check the port and baud.")
    if unparsed:
        print(f"note: ignored {unparsed} non-record serial lines (expected).")


if __name__ == "__main__":
    main()