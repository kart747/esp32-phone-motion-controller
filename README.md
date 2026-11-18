# ESP32 Phone Motion Controller

A Wii-Remote-style motion controller for tennis/cricket PC games.

- **Phone** = motion sensor (streams accel + gyro over Wi-Fi UDP, e.g. HyperIMU).
- **ESP32** = edge device: runs a Wi-Fi SoftAP, parses packets, recognises swings on-device.
- **Laptop** (CachyOS) sees the ESP32 as a standard Bluetooth (BLE HID) gamepad.

## Architecture

```
 Phone (HyperIMU)            ESP32 (SoftAP "MotionCtl")                  Laptop
 +-------------+  UDP:5555  +----------------------------------+  BLE   +-----------+
 | accel+gyro  | ---------> | udp_rx task -> queue ->          | -----> | HID       |
 | CSV packets |   Wi-Fi    |   process task (parse, filter,   |  HID   | gamepad   |
 +-------------+            |   gestures) -> gamepad output    |        | (Steam,   |
                            |  buttons: Start/Recal/Mode, LED  |        |  games)   |
                            +----------------------------------+        +-----------+
```

## Status

| Phase | What | State |
|---|---|---|
| 1 | Scaffold, `config.h`, SoftAP, raw UDP dump to serial, `phone_sim.py`, `udp_sniff.py` | Written, pushed. **Firmware not yet compiled or tested on hardware.** |
| 2 | CSV parser, packet stats, 500 ms timeout safety | Not started |
| 3 | Calibration + complementary filter, roll/pitch at 10 Hz | Not started |
| 4 | BLE gamepad (buttons + analog stick) and CachyOS pairing steps | Not started |
| 5 | Swing detection, tennis classification, tuning mode, remaining tools | Not started |
| 6 | Cricket mode, Mode button, LED states | Not started |
| 7 | Optional ESP32 web dashboard | Not started |

Work proceeds one phase at a time; each phase is tested before the next begins.

## Repo layout

```
firmware/   PlatformIO project (Arduino framework, ESP32 / ESP32-S3)
  include/config.h   ALL pins, ports, timings, thresholds - no magic numbers elsewhere
  src/               net_udp, main (more modules arrive in later phases)
tools/      Python 3 helpers: phone_sim.py, udp_sniff.py (+ more in Phase 5)
docs/       testing.md (Phase 1 steps now; axes.md, wiring, pairing later)
data/       recorded swing CSVs (gitignored)
```

## Quick start (Phase 1)

Requirements: [PlatformIO](https://platformio.org/) (`pip install platformio`), Python 3, an ESP32 DevKit.

```bash
cd firmware
pio run                      # compile  (esp32dev; use -e esp32s3 for S3)
pio run -t upload            # flash
pio device monitor           # serial, 115200 baud
```

Then:

1. Join Wi-Fi **MotionCtl** (password in `firmware/include/config.h`; change it if you like).
2. `python3 tools/phone_sim.py --scenario sequence` - serial should print `[rx ...]` lines.
3. Real phone: join `MotionCtl`, set the sensor app to **UDP -> 192.168.4.1:5555**, enable accel + gyro, and copy a few serial lines.

Full steps and troubleshooting: [docs/testing.md](docs/testing.md).

## Wiring (defaults for generic ESP32 DevKit; ESP32-S3 pins differ, see `config.h`)

| Part | GPIO | Notes |
|---|---|---|
| Status LED | 2 | on-board LED on most DevKits |
| Start button | 25 | other leg to GND (internal pull-up) |
| Recalibrate button | 26 | other leg to GND |
| Mode button | 27 | other leg to GND |

Buttons are not used by the firmware until later phases.

## For collaborators

- See [docs/next-steps.md](docs/next-steps.md) for the checklist. First task: **Phase 1 hardware verification** and capturing the phone packet format; the Phase 2 parser depends on it.
- Keep every threshold, pin and port in `config.h`.
- Small, focused commits; push to `main` (or a branch if you prefer).
