#!/usr/bin/env python3
"""Listen on a UDP port and show what a phone sensor app sends.

Use this for format discovery: point the phone app at THIS laptop's IP
(same Wi-Fi network as the laptop), run the script, and read the output.

It prints each payload (escaped), a guess at the CSV column count, and a
packet-rate summary once per second.

Example:
    python3 tools/udp_sniff.py --port 5555
"""
import argparse
import socket
import time


def escape(data: bytes) -> str:
    """Printable form of a payload; non-printable bytes become \\xHH."""
    out = []
    for b in data:
        if b == 0x0A:
            out.append("\\n")
        elif b == 0x0D:
            out.append("\\r")
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02X}")
    return "".join(out)


def describe(data: bytes) -> str:
    """One-line guess about the format (CSV field count / text vs binary)."""
    try:
        text = data.decode("ascii").strip()
    except UnicodeDecodeError:
        return "binary (not ASCII)"
    fields = text.split(",")
    numeric = 0
    for f in fields:
        try:
            float(f)
            numeric += 1
        except ValueError:
            pass
    return f"ascii, {len(fields)} comma-separated fields ({numeric} numeric)"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=5555, help="UDP port (default 5555)")
    ap.add_argument("--bind", default="0.0.0.0", help="address to bind (default all)")
    ap.add_argument("--hex", action="store_true", help="also print a hex dump")
    ap.add_argument("--max", type=int, default=0,
                    help="stop after N packets (default: run until Ctrl+C)")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    sock.settimeout(1.0)
    print(f"Listening on {args.bind}:{args.port} - Ctrl+C to stop")

    count = 0
    window_count = 0
    window_start = time.monotonic()
    try:
        while args.max == 0 or count < args.max:
            try:
                data, (ip, port) = sock.recvfrom(65535)
            except socket.timeout:
                data = None
            now = time.monotonic()
            if data is not None:
                count += 1
                window_count += 1
                print(f"[{count:6d}] {ip}:{port} len={len(data)} | {escape(data)}")
                if count == 1:
                    print(f"         format guess: {describe(data)}")
                if args.hex:
                    print("         hex:", data.hex(" "))
            if now - window_start >= 1.0:
                rate = window_count / (now - window_start)
                print(f"--- rate: {rate:.1f} packets/s (total {count}) ---")
                window_count = 0
                window_start = now
    except KeyboardInterrupt:
        pass
    print(f"\nDone. {count} packets received.")


if __name__ == "__main__":
    main()
