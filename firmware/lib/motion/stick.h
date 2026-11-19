// stick - map phone tilt (roll/pitch relative to the calibrated neutral pose)
// to a normalised analog stick position in -1..1. Hardware-free.
#pragma once

#include <stdint.h>

struct StickOut {
    float x = 0.0f;  // -1 left .. +1 right
    float y = 0.0f;  // -1 .. +1 in HID terms (negative = up/forward)
};

// One axis: dead-zone, then linear to full scale, clamped. Sign preserved.
// The output starts at 0 at the dead-zone edge (no jump when leaving it).
float tiltToAxis(float deltaDeg, float deadzoneDeg, float fullscaleDeg);

// rollDeg/pitchDeg are relative to the neutral pose.
//   x = xSign * axis(roll)    (tilt right -> +x)
//   y = ySign * axis(pitch)   (nose up    -> +y, i.e. HID "down")
// With the default ySign = +1, tilting the nose down gives a negative y,
// which is "stick forward" for HID gamepads.
StickOut mapTilt(float rollDeg, float pitchDeg, float deadzoneDeg, float fullscaleDeg,
                 float xSign, float ySign);

// -1..1 (clamped) -> HID axis value -axisMax..axisMax.
int16_t axisToHid(float v, int16_t axisMax);
