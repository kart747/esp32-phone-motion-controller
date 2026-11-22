#!/usr/bin/env python3
"""Plot a recording from tools/serial_logger.py, with detected swings marked.

Needs matplotlib (pip install -r tools/requirements.txt). Without it, the
script still prints a numeric summary so a recording can be inspected on a bare
system.

Examples:
    python3 tools/plot_swings.py data/20260101-120000_forehand_samples.csv
    python3 tools/plot_swings.py data/..._samples.csv --out swings.png
    python3 tools/plot_swings.py data/..._samples.csv --summary   # numbers only
"""
import argparse
import csv
import pathlib
import sys

GESTURE_COLOURS = {
    "forehand": "tab:green",
    "backhand": "tab:blue",
    "lob": "tab:orange",
    "serve": "tab:red",
    "none": "tab:gray",
}


def load_samples(path: pathlib.Path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        sys.exit(f"{path} has no sample rows")
    required = {"rx_ms", "ax", "ay", "az", "gx", "gy", "gz"}
    missing = required - set(rows[0].keys())
    if missing:
        sys.exit(f"{path} is missing columns: {sorted(missing)}")
    return rows


def load_swings(path: pathlib.Path):
    """Optional companion file, same stem with _swings.csv."""
    if not path.exists():
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def summarise(samples, swings) -> None:
    t0 = float(samples[0]["rx_ms"])
    t1 = float(samples[-1]["rx_ms"])
    dts = []
    for a, b in zip(samples, samples[1:]):
        dts.append(float(b["rx_ms"]) - float(a["rx_ms"]))
    dts.sort()
    print(f"duration      : {(t1 - t0) / 1000:.2f} s over {len(samples)} samples")
    if dts:
        mid = dts[len(dts) // 2]
        print(f"interval      : median {mid:.0f} ms  min {dts[0]:.0f}  max {dts[-1]:.0f}"
              f"  (~{1000 / mid:.0f} Hz)" if mid > 0 else "interval      : n/a")
        gaps = [d for d in dts if d > 100]
        if gaps:
            print(f"gaps > 100 ms : {len(gaps)} (largest {max(gaps):.0f} ms)")
    peaks = {}
    for s in swings:
        g = s.get("gesture", "none")
        peaks[g] = peaks.get(g, 0) + 1
    if peaks:
        print("swings        : " + ", ".join(f"{k}={v}" for k, v in sorted(peaks.items())))
        reasons = [s.get("reason", "") for s in swings if s.get("reason")]
        if reasons:
            print("reject reasons: " + ", ".join(sorted(set(reasons))))
    else:
        print("swings        : none recorded")


def plot(samples, swings, out: str | None, title: str) -> None:
    try:
        import matplotlib
        if out:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\nmatplotlib is not installed - showing the summary only.")
        print("Install it with: pip install -r tools/requirements.txt")
        return

    t = [(float(s["rx_ms"]) - float(samples[0]["rx_ms"])) / 1000.0 for s in samples]
    ax = [float(s["ax"]) for s in samples]
    ay = [float(s["ay"]) for s in samples]
    az = [float(s["az"]) for s in samples]
    gyro_mag = [
        (float(s["gx"]) ** 2 + float(s["gy"]) ** 2 + float(s["gz"]) ** 2) ** 0.5 for s in samples
    ]

    fig, (a1, a2) = plt.subplots(2, 1, figsize=(12, 7), sharex=True)
    a1.plot(t, ax, label="ax", alpha=0.8)
    a1.plot(t, ay, label="ay", alpha=0.8)
    a1.plot(t, az, label="az", alpha=0.8)
    a1.set_ylabel("accel (m/s²)")
    a1.legend(loc="upper right")
    a1.grid(alpha=0.3)

    a2.plot(t, gyro_mag, color="black", label="|gyro|")
    a2.set_ylabel("|gyro| (rad/s)")
    a2.set_xlabel("time (s)")
    a2.grid(alpha=0.3)
    a2.legend(loc="upper right")

    t_base = float(samples[0]["rx_ms"])
    for s in swings:
        try:
            ts = (float(s["t_ms"]) - t_base) / 1000.0
        except (KeyError, ValueError):
            continue
        g = s.get("gesture", "none")
        colour = GESTURE_COLOURS.get(g, "tab:purple")
        label = g if g != "none" else f"none ({s.get('reason', '')})"
        for axis in (a1, a2):
            axis.axvline(ts, color=colour, linestyle="--", linewidth=1.0, alpha=0.8)
        a2.annotate(label, (ts, a2.get_ylim()[1] * 0.9), rotation=90, fontsize=8, color=colour,
                    ha="right", va="top")

    fig.suptitle(title)
    fig.tight_layout()
    if out:
        fig.savefig(out, dpi=120)
        print(f"wrote {out}")
    else:
        plt.show()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("samples", type=pathlib.Path, help="..._samples.csv recording")
    ap.add_argument("--swings", type=pathlib.Path, default=None,
                    help="swing file (defaults to the _swings.csv next to the samples)")
    ap.add_argument("--out", default=None, help="save to this image instead of showing")
    ap.add_argument("--summary", action="store_true", help="print numbers only, no plot")
    args = ap.parse_args()

    if not args.samples.exists():
        sys.exit(f"no such file: {args.samples}")
    samples = load_samples(args.samples)
    swing_path = args.swings
    if swing_path is None:
        derived = args.samples.name.replace("_samples.csv", "_swings.csv")
        # Only use the companion file if the name really was a samples file;
        # otherwise we would reload the samples as if they were swings.
        if derived != args.samples.name:
            swing_path = args.samples.with_name(derived)
    swings = load_swings(swing_path) if swing_path else []

    title = args.samples.stem
    summarise(samples, swings)
    if not args.summary:
        plot(samples, swings, args.out, title)


if __name__ == "__main__":
    main()