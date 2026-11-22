# Axes and orientation

## Device frame

Android sensor axes, unchanged by the firmware:

| Axis | Direction |
|---|---|
| x | to the right of the screen |
| y | toward the top edge of the phone |
| z | out of the screen, toward you |

Accelerometer readings **include gravity** (Android `TYPE_ACCELEROMETER`), in
m/s². Gyroscope readings are angular velocity in rad/s.

## Neutral pose

The phone is held **flat, screen up, top edge pointing away from you**, like a
motion controller resting in your hands.

At rest in that pose the accelerometer reads about `(0, 0, +9.81)`, so the
"gravity vector" the filter tracks is the **unit vector pointing up, expressed
in the device frame**: `(0, 0, 1)` at neutral.

Calibration captures this pose (plus the gyro bias) and then reports orientation
*relative to it*, so a flat phone always reads roll = pitch = 0 no matter how it
was lying when you calibrated.

## Roll and pitch

| Angle | Definition | Positive means |
|---|---|---|
| roll | `atan2(-g.x, g.z)` | right edge down |
| pitch | `atan2(g.y, hypot(g.x, g.z))` | nose (top edge) up |

Both are reported in degrees, wrapped to ±180°, and relative to the calibrated
neutral pose.

## Supported range

Roll is mathematically undefined when pitch reaches ±90° (gravity then points
along the device y axis, so `g.x` and `g.z` are both zero). The firmware
therefore:

- **refuses to calibrate** a neutral pose tilted more than
  `CAL_MAX_REF_TILT_DEG` (30°), reporting `neutral_not_flat`;
- **marks the angles unusable** once pitch passes `NEAR_SINGULAR_PITCH_DEG`
  (75°), printing `[angles] unusable: pitch near 90 deg` instead of a value.

This is fine for holding the phone like a controller. Holding it like a racket
handle (gravity along +y at rest, i.e. pitch = 90°) is exactly the degenerate
case, which is why it is rejected rather than silently producing noise.

## Simulator convention

`tools/phone_sim.py` uses the same flat pose: gravity on +z, a forehand is a
gyro pulse about **z** (the vertical) with a matching sideways x acceleration
burst, and a backhand is the same pulse with the opposite sign.

## Swing axes (Phase 5)

The swing detector does **not** use raw device axes, because they overlap once
the phone is tilted. It projects the gyro using the current gravity estimate so
the three rotation integrals are mutually orthogonal:

| Integral | From | Meaning | Used for |
|---|---|---|---|
| yaw | `gyro · up` | rotation about the vertical | forehand vs backhand |
| pitch | horizontal part along device X | nose up / nose down | lob, serve |
| roll | horizontal part along device Y | wrist roll | rejected |

Held flat, `up = (0,0,1)` and these reduce to `(gz, gx, gy)`, so the signs match
the table above. The axes are captured once at the start of a swing.

`FOREHAND_YAW_SIGN` (+1 in `config.h`) decides which yaw sign is a forehand, so
a left-handed player can flip it with `set yaw_sign -1`. A swing that begins
more than `SWING_MAX_ONSET_TILT_DEG` from neutral is reported as
`unsupported_pose` instead of guessing.

## Checking it on hardware

With the phone (or the simulator) streaming, send `angles` on serial if angle
output was turned off, then tilt the phone:

- right edge down → roll becomes positive;
- nose up → pitch becomes positive.

If the signs come out reversed for your grip, that is a per-user preference and
belongs in the tuning layer (Phase 5), not in the filter.