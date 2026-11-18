#include "stick.h"

#include <math.h>

#include "config.h"

float wrapDeg(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

float tiltToAxis(float deltaDeg, float deadzoneDeg, float fullscaleDeg) {
    const float mag = fabsf(deltaDeg);
    if (mag <= deadzoneDeg || fullscaleDeg <= deadzoneDeg) return 0.0f;
    float v = (mag - deadzoneDeg) / (fullscaleDeg - deadzoneDeg);  // starts at 0 at the dead-zone edge
    if (v > 1.0f) v = 1.0f;
    return deltaDeg < 0 ? -v : v;
}

StickOut mapTilt(float rollDeg, float pitchDeg, float rollRef, float pitchRef, float deadzoneDeg,
                 float fullscaleDeg, float xSign, float ySign) {
    StickOut o;
    o.x = xSign * tiltToAxis(wrapDeg(rollDeg - rollRef), deadzoneDeg, fullscaleDeg);
    o.y = ySign * tiltToAxis(wrapDeg(pitchDeg - pitchRef), deadzoneDeg, fullscaleDeg);
    return o;
}

int16_t axisToHid(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<int16_t>(lroundf(v * GAMEPAD_AXIS_MAX));
}
