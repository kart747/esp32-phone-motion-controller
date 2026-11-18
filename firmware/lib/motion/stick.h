// stick - map phone tilt (roll/pitch relative to the neutral pose) to a
// normalised analog stick position in -1..1.
#pragma once

#include <stdint.h>

struct StickOut {
    float x = 0.0f;  // -1 left .. +1 right
    float y = 0.0f;  // -1 .. +1, sign follows STICK_Y_SIGN (default: nose down = -1 = stick up/forward in HID terms)
};

// Wrap an angle difference into [-180, 180].
float wrapDeg(float deg);

// One axis: dead-zone, then linear to full scale, clamped. Sign preserved.
float tiltToAxis(float deltaDeg, float deadzoneDeg, float fullscaleDeg);

// rollDeg/pitchDeg are absolute (from the filter); *Ref are the neutral values.
//   x =  xSign * axis(roll  - rollRef)    (tilt right  -> +x)
//   y =  ySign * axis(pitch - pitchRef)   (nose up     -> +y, HID "down")
// With the default ySign = +1, tilting the nose down gives negative y, which
// is "stick forward" for HID gamepads (Y axis up = negative).
StickOut mapTilt(float rollDeg, float pitchDeg, float rollRef, float pitchRef, float deadzoneDeg,
                 float fullscaleDeg, float xSign, float ySign);

// -1..1 -> HID axis value (-GAMEPAD_AXIS_MAX..GAMEPAD_AXIS_MAX).
int16_t axisToHid(float v);
