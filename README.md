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
| 1 | Scaffold, `config.h`, SoftAP, raw UDP dump to serial, `phone_sim.py`, `udp_sniff.py` | Done; flashed and running on the ESP32. |
| 2 | CSV parser, packet stats, 500 ms safety timeout | Done; verified with the simulator and a real phone (HyperIMU at 20 ms, 0 malformed). |
| 3 | Calibration + complementary filter, roll/pitch at 10 Hz | Done; calibration and angles verified on a real phone (gyro units confirmed rad/s). |
| 4 | BLE gamepad (analog stick from tilt) and CachyOS pairing steps | Done; pairs on Linux, X/Y axes and safety release verified. Physical buttons are wired in Phase 6. See [docs/pairing.md](docs/pairing.md). |
| 5 | Swing detection, tennis classification (forehand/backhand/lob/serve), tuning mode, `serial_logger.py`, `plot_swings.py` | Implemented and unit-tested; threshold values are **unvalidated guesses** until real swings are recorded. See [docs/testing.md](docs/testing.md). |
| 6 | Cricket mode (drive/pull/loft/defensive), Mode/Recal/Start buttons, LED status patterns | Implemented and unit-tested. Buttons and LED need hardware verification. |
| 7 | ESP32 web dashboard and non-volatile settings | Implemented; API and record format unit-tested, page verified in a browser against a mock. Not yet exercised on the device. See [docs/dashboard.md](docs/dashboard.md). |

Work proceeds one phase at a time; each phase is tested before the next begins.
Phases 5-7 are **implemented with automated tests but not fully validated on
hardware**: the swing and cricket thresholds are still unmeasured guesses, and
the buttons, LED and dashboard have not been exercised on the board. See
[docs/next-steps.md](docs/next-steps.md) for exactly what remains.

## Repo layout

```
firmware/   PlatformIO project (Arduino framework, ESP32 / ESP32-S3)
  include/config.h   ALL pins, ports, timings, thresholds - no magic numbers elsewhere
  src/               net_udp (SoftAP + UDP rx), main (tasks, serial commands)
  lib/motion/        hardware-free logic: parser, packet stats, sample timing,
                     calibration, gravity filter, pipeline. Tested with `pio test -e native`
  test/              native unit tests for lib/motion
tools/      Python 3 helpers: phone_sim.py, udp_sniff.py,
            serial_logger.py (record swings), plot_swings.py (inspect them),
            replay_swings.cpp (re-run a recording through the firmware logic),
            dashboard_mock.py (serve the dashboard page locally), test_tools.py
docs/       testing.md, axes.md, pairing.md, dashboard.md, phone-packet-sample.txt
data/       recorded swing CSVs (gitignored)
```

## Serial commands

With the monitor open, type one command per line:

| Command | Effect |
|---|---|
| `recal` | redo still-pose calibration |
| `raw` | toggle printing every raw UDP payload |
| `hex` | toggle the hex view of payloads |
| `angles` | toggle the 10 Hz roll/pitch output |
| `stats` | print the counters now |
| `tune on` / `tune off` | stream CSV records for `tools/serial_logger.py` |
| `get` | print the swing thresholds in force |
| `set <key> <value>` | change one threshold (runtime only, reboot restores `config.h`) |
| `defaults` | restore every threshold from `config.h` |
| `sport tennis` / `sport cricket` / `mode` | select or toggle the sport |
| `click on` / `click off` / `click` | enable, disable or toggle the click on recognised swings |

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
