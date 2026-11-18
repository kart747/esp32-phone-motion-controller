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

`[angles]` prints at 10 Hz (`DEBUG_ANGLES_PERIOD_MS`). Tilt the phone: right edge
down gives positive roll, nose up gives positive pitch (see `docs/axes.md`).

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