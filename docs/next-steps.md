# Next steps

Current state: Phases 1-3 are implemented, build clean for `esp32dev` and
`esp32s3`, pass the native test suite, and are flashed to the ESP32
(ESP32-D0WD-V3). The live simulator/phone streaming check over the SoftAP is
still outstanding - see below.

## 1. Remaining verification for phases 2-3

- [ ] Join `MotionCtl` from the laptop, run `python3 tools/phone_sim.py --scenario sequence`, and confirm `samples` rises with `malformed=0`.
- [ ] Confirm `[cal] done` appears and `[angles]` tracks tilts with the documented signs.
- [ ] Confirm the 500 ms timeout: stop the stream, expect `[safe] ... inputs released`; restart, expect `[safe] valid data again`.
- [ ] Real phone: point the sensor app (HyperIMU) at UDP `192.168.4.1:5555`, accel + gyro enabled, then use `raw` to check the column order and units against `config.h`. The defaults assume `timestamp_ms,ax,ay,az,gx,gy,gz` in m/s² and rad/s.
- [ ] Save 10-20 raw lines from the real phone into `docs/phone-packet-sample.txt`.

Details: [testing.md](testing.md), [axes.md](axes.md).

## 2. Phase 4 - BLE gamepad

- BLE HID gamepad (lemmingDev/ESP32-BLE-Gamepad on NimBLE-Arduino; check current docs first) + CachyOS pairing steps.
- Wire the `OutputGate` actions to real button/stick release: `GateAction::Release` must clear inputs, `Resume` re-enables them.
- Map calibrated roll/pitch to the analog stick (dead zone and full scale constants already exist as Phase 3 leftovers in the branch work, not in `main`).
- Note: Wi-Fi AP and BLE share the 2.4 GHz radio; measure added latency before trusting swing timing.
- Flash headroom on the classic DevKit is tight (~70% of a 1.31 MB app partition used); this may need a custom partition table.

## 3. Later phases

5. Swing detection, tennis classification, tuning mode, `serial_logger.py`, `plot_swings.py`
6. Cricket mode, Mode button, LED states
7. Optional: web dashboard with thresholds in NVS

Work one phase at a time; test each before starting the next.