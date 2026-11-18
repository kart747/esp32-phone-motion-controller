# ESP32 Phone Motion Controller

A Wii-Remote-style motion controller for tennis/cricket PC games.

- **Phone** = motion sensor (streams accel + gyro over Wi-Fi UDP).
- **ESP32** = edge device: SoftAP, parses packets, recognises swings on-device.
- **Laptop** sees the ESP32 as a standard Bluetooth (BLE HID) gamepad.

Status: **Phase 1** (scaffold, SoftAP, raw UDP dump). See `docs/` for details.
