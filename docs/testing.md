# Testing

Phases 1-3 are implemented. Each section lists what to run and what to expect.

```bash
cd firmware
pio run -e esp32dev -t upload     # or -e esp32s3
pio device monitor                # 115200 baud
python3 ../tools/phone_sim.py --host 192.168.4.1 --scenario sequence
```

Serial commands (one per line): `recal`, `raw`, `hex`, `angles`, `stats`.

## Phase 1 - SoftAP and UDP receive

Goal: the ESP32 creates the Wi-Fi network and receives UDP datagrams.

Expected boot output:

```
=== ESP32 phone motion controller - phases 2-3 ===
[wifi] AP up: SSID="MotionCtl" channel=1 IP=192.168.4.1
[udp] listening on port 5555
[cal] hold the phone flat and still for 1500 ms
[cmd] recal | raw | hex | angles | stats
```

1. Join Wi-Fi **MotionCtl** (password in `firmware/include/config.h`).
2. `python3 tools/phone_sim.py --scenario sequence`
3. `[stats]` every 5 s should show `datagrams` and `samples` rising.

If nothing arrives: check the phone is really on `MotionCtl` (Android may switch
away from a network without internet - accept the "stay connected" prompt).

## Phase 2 - parsing, counters and the safety timeout

`[stats]` reports, per 5 s window:

| Field | Meaning |
|---|---|
| `datagrams` | UDP datagrams received |
| `samples` | valid samples parsed out of them |
| `malformed` | non-empty lines that failed to parse |
| `overflow` | valid lines dropped because a datagram had more than `CSV_MAX_SAMPLES_PER_PACKET` |
| `truncated` | datagrams larger than `UDP_MAX_PAYLOAD` (not parsed at all) |
| `qdrop` | packets dropped because the receive queue was full |
| `pps` / `sps` | datagram and sample rate over the last second |
| `since_valid` | ms since the last **valid sample**, or `never` |

Checks:

- **Format discovery:** `raw` on, then `raw` off. Use this to confirm column
  order and units against `config.h`. `hex` adds a hex view.
- **Malformed traffic must not look alive.** Send garbage at the port (for
  example `printf 'nonsense' | nc -u 192.168.4.1 5555`). `malformed` rises,
  `samples` stays flat, and after `DATA_TIMEOUT_MS` (500 ms) the log shows
  `[safe] no valid data for ... ms - inputs released`.
- **Timeout and recovery.** Stop the simulator: within ~500 ms plus one
  `PROCESS_WAKE_MS` (10 ms) you get the `[safe]` line. Restart it: `[safe] valid
  data again - inputs live`.
- **Truncation.** Send a datagram longer than `UDP_MAX_PAYLOAD` (512 bytes):
  `truncated` rises and the packet is not parsed.

Note: Phase 2 has no gamepad yet, so "inputs released" currently only logs the
transition; the BLE output adapter that acts on it arrives in Phase 4.

## Phase 3 - calibration and angles

Hold the phone flat and still after boot (or after sending `recal`):

```
[cal] recalibrating: hold the phone flat and still
[cal] collecting 45% (failed=0 last=)
[cal] done: gyro bias=(+0.020 -0.010 +0.015) rad/s, |g|=9.81 m/s^2
[angles] roll=+0.2 pitch=-0.1 deg
```

## Phase 5 - swing detection, classification and mouse clicks

Set HyperIMU to **20 ms (50 Hz)**; at 10 Hz the sample interval exceeds
`DT_MAX_S` and calibration never completes.

`[angles]` prints at 10 Hz (`DEBUG_ANGLES_PERIOD_MS`). Tilt the phone: right edge
down gives positive roll, nose up gives positive pitch (see `docs/axes.md`).

### Serial commands added in Phase 5

| Command | Effect |
|---|---|
| `tune on` / `tune off` | stream CSV sample (`S,`) and swing (`W,`) records for the tools |
| `get` | dump the tunables currently in force (`T,key=value`) |
| `set <key> <value>` | change one threshold at runtime; rejected if unknown, out of range, or if the resulting combination is inconsistent |
| `defaults` | restore every threshold from `config.h` |
| `sport tennis` / `sport cricket` / `mode` | select or toggle the sport |
| `click on` / `click off` / `click` | enable, disable or toggle the click on recognised swings |

Runtime changes are not saved automatically. Persist them with **Save to
device** on the dashboard ([dashboard.md](dashboard.md)); the mode and click
switch set over serial are saved the same way.

### Reading the swing log

Each detected movement produces one line:

```
[swing] forehand peak=8.1 rad/s dur=260 ms yaw=1.28 pitch=0.00
[swing] rejected (roll_dominant) peak=7.9 rad/s
```

Rejection reasons:

| Reason | Meaning |
|---|---|
| `weak` | peak rate below `TENNIS_MIN_PEAK_RAD_S`: fidgeting, not a stroke |
| `small_integral` | energetic but almost no net rotation |
| `ambiguous` | the dominant axis does not clearly beat the next one |
| `roll_dominant` | wrist roll (rotation about the phone's long axis) |
| `no_up_accel` | an upward pitch swing without the upward acceleration a lob needs |
| `downswing_not_overhead` | a downward swing that did not start from a raised pose |
| `unsupported_pose` | the swing began too far from the calibrated neutral pose |

A recognised stroke in mouse mode also fires **one** left click (press, then
release after `SWING_CLICK_HOLD_MS`). Cursor movement is unchanged. A second
swing during a click is ignored rather than queued.

### Recording real swings (do this before trusting any threshold)

Every default threshold is a **guess**. Record your own strokes, then tune.

```bash
cd firmware && pio device monitor          # terminal 1: watch the logs
python3 tools/serial_logger.py --port /dev/ttyUSB0 \
    --label forehand --count 20 --note "right hand, phone flat"
```

Hold the phone still for ~1.5 s first so calibration finishes, then swing. The
script writes `data/<stamp>_<label>_samples.csv`, `_swings.csv` and `_meta.txt`
(the tunables in force). `data/` is gitignored: recordings are personal.

Record for every stroke (`--label forehand|backhand|lob|serve`), plus idle and
deliberate fast non-swings. Keep at least one session you do **not** tune on, so
accuracy can be measured on data the thresholds never saw.

Then examine and iterate:

```bash
python3 tools/plot_swings.py data/<stamp>_forehand_samples.csv --out swings.png
python3 tools/plot_swings.py data/<stamp>_forehand_samples.csv --summary
```

### Replaying a recording through the firmware logic

`tools/replay_swings.cpp` links the **same** `lib/motion` sources the ESP32
builds, so thresholds are evaluated against recorded data without reflashing:

```bash
g++ -std=c++17 -O2 -I firmware/include -I firmware/lib/motion \
    tools/replay_swings.cpp firmware/lib/motion/{parser,pipeline,imu_filter,sample_clock,gestures,tunables}.cpp \
    -o /tmp/replay_swings

/tmp/replay_swings data/<stamp>_forehand_samples.csv data/<stamp>_forehand_swings.csv
```

It prints the gesture the firmware would produce for each swing next to the
recorded one, with `MATCH`/`MISS`, plus a summary of recognised vs rejected.
Adjust a value with `set` on the device, re-record, and confirm the change before
making it the `config.h` default.

Not covered by replay: the UDP transport, the BLE adapters and task scheduling.

## Phase 6 - cricket, buttons and LED

**Sport mode** (`sport cricket` or the Mode button) switches the classifier to
cricket: drive, pull, loft and defensive. A recognised cricket stroke still
makes one left click in mouse mode; gamepad mode keeps driving the stick only.

**Buttons** (active-low to GND, internal pull-up):

| Button | Pin (ESP32 / S3) | Action |
|---|---|---|
| Start | 25 / 5 | toggle the click on recognised swings |
| Recalibrate | 26 / 6 | same as `recal` |
| Mode | 27 / 7 | toggle tennis / cricket |

A button held while the board boots is adopted silently rather than firing, and
presses closer than `BTN_MIN_GAP_MS` are ignored.

**LED patterns** (`LED_PAT_*` in `config.h`): solid = running (streaming +
BLE), long blink = streaming only, short blink = calibrating, rapid blink =
error, mostly dark = idle. A mode change flashes once for tennis and twice for
cricket, but an error always wins over that flash.

## Phase 7 - dashboard

See [dashboard.md](dashboard.md). Quick check that the page is served:

```bash
curl -s http://192.168.4.1/api/status     # live JSON, no token needed
```

### Automated tests

```bash
cd firmware && pio test -e native    # 159 cases across 10 suites
python3 tools/test_tools.py          # host tool record parsing
```

The suites cover the CSV parser, counters and watchdog, orientation, stick and
mouse, gesture detection and classification (tennis and cricket), the
diagnostic ring, the command/button/LED logic, the saved-settings record with
its corruption paths, and the dashboard API including authentication and
validation.

Calibration restarts itself, counting `failed`, if:

| `last` reason | Cause |
|---|---|
| `moving` | a gyro sample exceeded `CAL_ABORT_GYRO_RAD_S` |
| `not_still` | gyro or accel spread too large over the window |
| `too_few_samples` | fewer than `CAL_MIN_SAMPLES` arrived |
| `gravity_magnitude_wrong_units?` | mean \|accel\| far from 1 g - check the unit scales in `config.h` |
| `neutral_not_flat` | phone held more than `CAL_MAX_REF_TILT_DEG` from flat |
| `gap` / `data_lost` | the stream stalled, so the window was restarted |

Above roughly 75° of pitch the angles are reported as unusable instead of noisy.

A completed calibration is kept across a short data outage; it is only redone on
request (`recal`) or after a reboot.

## Automated tests

The hardware-free logic in `firmware/lib/motion` runs on the laptop:

```bash
cd firmware
pio test -e native
```

Covers the CSV parser (boundaries, layouts, unit scales, multiline, overflow,
truncation), the counters and watchdog (malformed-only traffic, initial silence,
`millis()` wrap, rate window), the output gate's idempotence, sample timing
(timestamps vs arrival time vs nominal, gaps, out-of-order) and calibration and
filter behaviour (stillness, bias, rejection reasons, tilt signs, tracked
rotation, singularity, data loss).