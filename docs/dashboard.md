# Dashboard and saved settings (Phase 7)

The ESP32 serves a small dashboard from its own access point. No internet, no
filesystem and no external assets: the page is compiled into the firmware.

```
Phone ──UDP──▶ ESP32 (SoftAP 192.168.4.1) ──BLE──▶ laptop
                    │
                    └── HTTP :80 ──▶ browser (the dashboard)
```

Open **http://192.168.4.1/** while connected to `MotionCtl`. The access point
allows two clients, so the phone can stream while the laptop or the phone's own
browser uses the page.

## What it shows

| Card | Meaning |
|---|---|
| Phone | `streaming` = a valid sample arrived within `DATA_TIMEOUT_MS` |
| Bluetooth | `connected` = a host has paired |
| Calibration | `calibrating`, `ready`, or `error` |
| Samples/s | measured sample rate |
| Dropped | queue drops plus stale packets discarded |
| Last swing | the last classification and, when rejected, why |

Below that are the live settings: sport, the swing-click switch, and every
threshold the classifier uses.

## What it can change

| Action | Effect |
|---|---|
| **Apply** | validates the whole form and applies it to the running device |
| **Save to device** | writes the current settings to non-volatile storage |
| **Restore defaults** | loads the `config.h` defaults into the form (still needs Apply, then Save) |

"Apply" changes the running behaviour but not the stored copy, so the page shows
an **unsaved** marker until you save. Saving while there are unapplied edits is
refused - apply first - so the stored settings always match what you were
looking at.

Changes from the dashboard go through the same validated path as the serial
commands, and are applied by the processing task, which is the only writer of
live configuration.

## Persistence

Settings survive a reboot. The record is text, versioned and checksummed:

```
version=1
sport=cricket
click=1
onset=2.5
...
crc=1A2B3C4D
```

On boot the firmware validates, in order: the record is complete, the version is
known, the CRC matches, every field parses, and the whole set passes the
cross-field checks. **Any failure means the record is ignored**, the defaults are
used, and serial reports why:

```
[save] saved settings ignored: checksum mismatch
```

A half-written record from a power loss therefore cannot be applied. Deleting the
record (or never saving) simply means defaults on every boot.

## Security - read this

The access point password is shared and set in `config.h`, and the dashboard is
served over **plain HTTP**. On a network you control that is usually fine; the
points worth knowing:

- **Reads are unauthenticated.** Anyone on the AP can see the current settings
  and status.
- **Writes need a bearer token**, set by `DASHBOARD_TOKEN` in `config.h`. The
  default is **empty, which disables write authentication** - acceptable only
  while you are the only person on the AP.
- **Cross-origin writes are refused.** A page on another site cannot reconfigure
  the device through the browser, because requests carrying a foreign `Origin`
  are rejected.
- **Plain HTTP is readable by anyone able to observe the AP traffic.** Do not
  reuse a password you care about.

To enable write protection:

1. Set `DASHBOARD_TOKEN` to a long random string in `config.h` and reflash.
2. Also change `AP_PASSWORD` while you are there.
3. Type the token into the dashboard's **Device token** field before pressing
   Apply/Save. It is kept in the page's memory only, never stored.

## Testing

The API logic (`GET /api/status`, `GET/POST /api/settings`, `POST /api/save`,
`POST /api/defaults`, authentication, origin checks, body limits, validation) is
covered by `firmware/test/test_api` - 20 native cases, no hardware needed.

The record format, checksum and every corruption path are covered by
`firmware/test/test_settings` - 12 native cases.

To exercise the **page** without the device (for example when joining the AP
would drop your internet), use the mock, which serves the firmware's own HTML:

```bash
python3 tools/dashboard_mock.py --port 8099 --token testtoken
# then open http://127.0.0.1:8099/
```

The mock is a Python mirror of the API, so it verifies the page and its flows -
not the firmware. Anything checked that way is UI verification only.

## Unverified on hardware

The page was verified in a browser against the mock (desktop and mobile
viewports, apply/save/defaults, rejection paths, offline state). It has **not**
been exercised against the real device, because that needs the laptop on the
`MotionCtl` AP. In particular, unverified:

- HTTP performance while the phone streams (packet drops under load)
- NVS persistence across a real power cycle
- Behaviour on a phone browser connected to the same AP it streams over