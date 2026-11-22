# Next steps

Current state: phases 1-7 are **implemented**. Automated tests pass (159 native
cases, 11 host-tool cases) and both ESP32 targets build clean. What is missing is
almost entirely **hardware validation and real-swing tuning** - the plan for
that is below.

An earlier build of phases 2-4 was verified on hardware with a real phone. The
phase 5-7 work has **not** been run on the board yet.

## 1. Before trusting anything: record real swings (phase 5 tuning)

Every threshold in `config.h` is a guess. Nothing below is meaningful until these
numbers come from your own strokes.

- [ ] Flash the current firmware (`pio run -e esp32dev -t upload`); close `pio device monitor` first or the port is busy.
- [ ] HyperIMU at 20 ms (50 Hz), phone on `MotionCtl`, streaming to `192.168.4.1:5555`.
- [ ] Hold the phone flat and still ~1.5 s; confirm `[cal] done`.
- [ ] Record a session per label: `python3 tools/serial_logger.py --label forehand --count 20` (repeat for backhand, lob, serve, then cricket drive/pull/loft/defensive, plus a "still" session for false positives).
- [ ] Replay each recording: `/tmp/replay_swings data/..._samples.csv data/..._swings.csv` (build line is in the file header).
- [ ] Adjust with `set <key> <value>`, re-record, and confirm on a **held-out** session before writing the winner into `config.h`.
- [ ] Confirm on the host that one recognised stroke produces exactly one press and release (e.g. `libinput debug-events` or `xev`), and that a 500 ms data loss releases it.
- [ ] Serve is the least certain class: it starts near the 60 deg onset-tilt limit. Verify or report it unverified.

## 2. Phase 6 hardware checks

- [ ] Buttons: wiring polarity, no false triggers at boot, one action per press, and that a Mode press during a swing cancels it.
- [ ] LED: confirm the real polarity and that each state is distinguishable (error, calibrating, streaming, running, idle).
- [ ] Mode flash: one blink for tennis, two for cricket, and never masking an error.
- [ ] Cricket classification against labelled real strokes.

Note: on a classic ESP32 the LED is GPIO2, which is also a boot-strapping pin -
watch for it affecting flashing.

## 3. Phase 7 dashboard checks

- [ ] Open `http://192.168.4.1/` from the laptop while the phone streams; confirm status updates and that loading the page does not increase `qdrop`.
- [ ] Apply / Save / Restore defaults over HTTP, then **reboot the board** and confirm the saved settings came back (`[save] restored saved settings`).
- [ ] Power-cycle mid-save if you can, and confirm the board falls back to defaults with a `[save] ... ignored: <reason>` message rather than loading junk.
- [ ] Set `DASHBOARD_TOKEN` and confirm writes are refused without it.
- [ ] Check the page on a phone browser at mobile width.

## 4. Known limitations to keep in mind

- Movement slower than `SWING_ONSET_RAD_S` cannot be detected at all, so a soft
  cricket block may simply be invisible; tune the onset if that matters.
- Roll is ill-defined near 90 deg of pitch; such poses are reported
  `unsupported_pose` rather than guessed.
- Wi-Fi AP and BLE share the 2.4 GHz radio. Latency under load is still
  unmeasured - worth doing before trusting swing timing in a game.
- The AP password is shared and hard-coded, and the dashboard is plain HTTP.
  Fine on your own bench; see the security section of `docs/dashboard.md`.

## 5. Where the reference material lives

- `origin/wip/motion-lib` - original phase 5-6 sketch. Its architecture has been
  superseded; only its cricket rule ideas were reused.
- `origin/phase4-ble`, `origin/phase5-swing` - already merged / empty labels.