# Next steps

Current state: Phase 1 code is pushed but has **not been compiled or run on hardware**.

## 1. Verify Phase 1 (do this first)

- [ ] `cd firmware && pio run` compiles for `esp32dev` (and `-e esp32s3` if you have one). Fix any build errors.
- [ ] `pio run -t upload`, then `pio device monitor`: expect `AP up`, `listening on port 5555`, and a `[stats]` line every 5 s.
- [ ] Join Wi-Fi `MotionCtl` from the laptop, run `python3 tools/phone_sim.py --scenario sequence`: serial shows `[rx ...]` lines, `dropped = 0`.
- [ ] Real phone: join `MotionCtl`, point the sensor app (HyperIMU) at UDP `192.168.4.1:5555`, accel + gyro enabled.
- [ ] Save 10-20 raw serial lines from the real phone into `docs/phone-packet-sample.txt`. Note the units (m/s^2 vs g, rad/s vs deg/s) and whether a timestamp column exists.

Details: [testing.md](testing.md).

## 2. Phase 2 - parser and safety (blocked on the packet sample above)

- CSV parser with configurable column order in `config.h`; ignore extra fields; reject malformed packets without crashing.
- Packet rate, dropped/malformed counters, time since last packet.
- No data for more than 500 ms: release all gamepad buttons.

## 3. Later phases

3. Calibration + complementary filter, roll/pitch printed at 10 Hz
4. BLE gamepad (lemmingDev/ESP32-BLE-Gamepad on NimBLE-Arduino; check current docs first) + CachyOS pairing steps
5. Swing detection, tennis classification, tuning mode, `serial_logger.py`, `plot_swings.py`
6. Cricket mode, Mode button, LED states
7. Optional: web dashboard with thresholds in NVS

Work one phase at a time; test each before starting the next.
