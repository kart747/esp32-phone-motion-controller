# Next steps

Current state: Phases 1-3 are done and verified on hardware with a real phone.
Phase 4 (BLE gamepad) works on branch `phase4-ble`: the laptop pairs with
"MotionCtl Pad", tilt drives the left stick (all four directions checked) and a
lost stream centres the stick within ~0.5 s. Merge it to `main` when ready.

## 1. Remaining verification for phases 2-3

- [x] Join `MotionCtl` from the laptop, run `python3 tools/phone_sim.py --scenario sequence`, and confirm `samples` rises with `malformed=0`. (Done: 1620/1620 samples, 0 malformed.)
- [x] Confirm `[cal] done` appears and `[angles]` tracks tilts. (Done with the simulator and the real phone; the physical tilt directions are not yet independently confirmed.)
- [x] Confirm the 500 ms timeout: stop the stream, expect `[safe] ... inputs released`; restart, expect `[safe] valid data again`. (Done: ~502 ms, recovers.)
- [x] Real phone: point the sensor app (HyperIMU) at UDP `192.168.4.1:5555`, accel + gyro enabled, then use `raw` to check the column order and units against `config.h`. The real HyperIMU stream is `ax,ay,az,gx,gy,gz` (no timestamp) in m/s² and rad/s, now the `config.h` default. Set the app interval to 20 ms: at 100 ms calibration cannot finish.
- [x] Save 10-20 raw lines from the real phone into `docs/phone-packet-sample.txt`.

Details: [testing.md](testing.md), [axes.md](axes.md).

## 2. Phase 4 follow-ups

- [ ] Merge `phase4-ble` into `main`.
- [ ] Flash headroom: the app is ~91% of the default 1.31 MB partition. Add a custom partition table (e.g. `huge_app.csv`) before Phase 5.
- [ ] Measure added latency from running Wi-Fi and BLE together before trusting swing timing.
- [ ] Beyond ~75 deg of nose-up the angles are "unusable" and the stick snaps from full to centre; consider holding full deflection instead.
- [ ] The resting pose can read a few degrees off the calibrated neutral (observed roll +8, pitch -6); this exceeds the 5 deg dead zone. Recalibrate, or widen `STICK_DEADZONE_DEG`.
- [ ] Buttons (Start / Recal / Mode) are defined but not wired to the gamepad yet.
- [ ] Serial once printed `ertising` in a tight loop after a flash; cleared by a hard reset. Watch for a repeat.

## 3. Later phases

5. Swing detection, tennis classification, tuning mode, `serial_logger.py`, `plot_swings.py`
6. Cricket mode, Mode button, LED states
7. Optional: web dashboard with thresholds in NVS

Work one phase at a time; test each before starting the next.