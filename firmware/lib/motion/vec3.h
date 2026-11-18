// Tiny 3-vector helpers (header-only, no dependencies).
#pragma once

#include <math.h>

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float norm(Vec3 a) { return sqrtf(dot(a, a)); }

// Unit vector; returns `fallback` for (near) zero input instead of NaN.
inline Vec3 normalized(Vec3 a, Vec3 fallback = {0.0f, 0.0f, 1.0f}) {
    const float n = norm(a);
    return n > 1e-6f ? a * (1.0f / n) : fallback;
}