# Pairing the ESP32 as a Bluetooth gamepad (Linux / CachyOS)

The firmware advertises a BLE HID gamepad named **MotionCtl Pad** (`BLE_DEVICE_NAME`
in `firmware/include/config.h`). It uses the library's Generic mode: two axes
(left stick X/Y) and 8 buttons.

## Pair

`bluetoothctl scan on` as a one-shot command can exit before it finds anything,
so use an interactive session:

```
bluetoothctl
[bluetoothctl]> power on
[bluetoothctl]> scan on
...  [NEW] Device 78:1C:3C:2D:55:AE MotionCtl Pad   <- note the address
[bluetoothctl]> pair 78:1C:3C:2D:55:AE
[bluetoothctl]> trust 78:1C:3C:2D:55:AE
[bluetoothctl]> connect 78:1C:3C:2D:55:AE
[bluetoothctl]> scan off
```

The address is the board's own and will differ per board. "Just Works" pairing
needs no PIN. `trust` lets it reconnect by itself when the board powers up.
Serial shows `[ble] host connected` and `[stats]` shows `ble=connected`.

To forget it: `bluetoothctl remove <address>`.

## Check that the kernel sees a gamepad

```
grep -A8 "MotionCtl" /proc/bus/input/devices     # expect Handlers=eventN jsN, ABS=3
```

`/dev/input/js0` (or `event*`) streams the axes. Any of these will show them:
`jstest /dev/input/js0`, `evtest`, `jstest-gtk`, or a browser gamepad tester.
(None are installed by default; the project checks used a few lines of Python
reading `js0`.) Your user must be in the `input` group.

## Expected behaviour

Tilt relative to the pose captured at calibration (phone flat, screen up):

| Tilt | Axis |
|---|---|
| right edge down | X positive (full right at `STICK_FULLSCALE_DEG`, 35 deg) |
| left edge down | X negative |
| nose down (tilt forward) | Y negative (HID "up"/forward) |
| nose up | Y positive |

A 5 deg dead zone (`STICK_DEADZONE_DEG`) keeps the stick centred at rest. If the
direction feels wrong for a game, flip `STICK_X_SIGN` / `STICK_Y_SIGN`.

If the phone stream stops for more than `DATA_TIMEOUT_MS` (500 ms), the firmware
logs `[safe] ... inputs released` and sends a centred stick with all buttons
released.

## Troubleshooting

- **Not found while scanning:** make sure the board is powered and flashed with the
  Phase 4 build (serial: `[ble] advertising as "MotionCtl Pad"`). Scan for at
  least 15-20 s.
- **Pairs but no `js0`:** check `/proc/bus/input/devices`; reconnect with
  `bluetoothctl disconnect` then `connect`.
- **Wi-Fi and BLE share the 2.4 GHz radio.** Latency and drops are possible; watch
  `qdrop` and `pps` in `[stats]`.
- Serial log note: `E NimBLEDevice: Failed to set default phy; rc=513` appears at
  start-up and did not stop advertising, pairing or input.
