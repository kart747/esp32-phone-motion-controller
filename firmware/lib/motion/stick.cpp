#include "stick.h"

#include <math.h>

float tiltToAxis(float deltaDeg, float deadzoneDeg, float fullscaleDeg) {
    if (!isfinite(deltaDeg)) return 0.0f;
    const float mag = fabsf(deltaDeg);
    if (mag <= deadzoneDeg || fullscaleDeg <= deadzoneDeg) return 0.0f;
    float v = (mag - deadzoneDeg) / (fullscaleDeg - deadzoneDeg);
    if (v > 1.0f) v = 1.0f;
    return deltaDeg < 0 ? -v : v;
}

StickOut mapTilt(float rollDeg, float pitchDeg, float deadzoneDeg, float fullscaleDeg,
                 float xSign, float ySign) {
    StickOut o;
    o.x = xSign * tiltToAxis(rollDeg, deadzoneDeg, fullscaleDeg);
    o.y = ySign * tiltToAxis(pitchDeg, deadzoneDeg, fullscaleDeg);
    return o;
}

int16_t axisToHid(float v, int16_t axisMax) {
    if (!isfinite(v)) return 0;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<int16_t>(lroundf(v * axisMax));
}
