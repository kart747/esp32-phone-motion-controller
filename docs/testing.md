# Testing

## Phase 1 - SoftAP and raw UDP dump

Goal: confirm the ESP32 creates the Wi-Fi network, receives UDP datagrams and
shows their raw contents on serial, so we can learn the phone app's format.

### Build and flash

```bash
pip install platformio            # once (or use the VS Code extension)
cd firmware
pio run -t upload                 # esp32dev; use `-e esp32s3` for ESP32-S3
pio device monitor                # 115200 baud
```

Expected serial output after boot:

```
=== ESP32 phone motion controller - Phase 1 ===
[wifi] AP up: SSID="MotionCtl" channel=1 IP=192.168.4.1
[udp] listening on port 5555
[stats] received=0 dropped(queue full)=0      <- every 5 s
```

### Test A - laptop simulator

1. On the laptop, join the Wi-Fi network `MotionCtl` (password `swing1234`,
   both set in `firmware/include/config.h`).
2. `python3 tools/phone_sim.py --scenario sequence`
3. Serial should show one `[rx ...]` line per packet, e.g.
   `[rx 4821393 us] 192.168.4.2:51234 len=46 | 120,0.04,9.81,...`
   and the `[stats]` line should show `received` rising with `dropped = 0`.

### Test B - real phone (format discovery)

1. Join `MotionCtl` on the phone.
2. In the sensor-streaming app (e.g. HyperIMU), set the target IP to
   `192.168.4.1`, port `5555`, protocol **UDP**, and enable accelerometer and
   gyroscope.
3. Start streaming and watch the serial monitor. **Copy a handful of lines
   into your notes** - the Phase 2 parser column order depends on them.
4. Optional: to inspect the format on the laptop instead, put phone and
   laptop on the same router Wi-Fi, point the app at the laptop IP and run
   `python3 tools/udp_sniff.py --port 5555`.

If nothing arrives: check the phone is actually connected to `MotionCtl`
(Android may auto-switch away from a network without internet - accept the
"stay connected" prompt), and that the app's target port is 5555.
