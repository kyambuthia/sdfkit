#pragma once

// Distance primitives, CPU side.
//
// Every function here has a twin in shaders/sdf.glsl. The twins must stay
// numerically identical: the field is what the renderer sphere-traces AND
// what collision queries, and that single-source property is the whole reason
// for building on distance fields. tests/parity is what stops them drifting.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace sdf {

// Polynomial smooth minimum. Fuses shapes instead of creasing them, at the
// cost of the field no longer being a true distance - which is why every
// march steps at a fraction of the reported distance.
inline float smin(float a, float b, float k) {
    const float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return (b * (1.0f - h) + a * h) - k * h * (1.0f - h);
}

inline float smax(float a, float b, float k) {
    return -smin(-a, -b, k);
}

inline float sphere(const glm::vec3 &p, const glm::vec3 &centre, float radius) {
    return glm::length(p - centre) - radius;
}

inline float round_box(const glm::vec3 &p, const glm::vec3 &centre,
                       const glm::vec3 &half, float radius) {
    const glm::vec3 q = glm::abs(p - centre) - half;
    return glm::length(glm::max(q, glm::vec3(0.0f))) +
           std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - radius;
}

// Exact distance to a round cone: two spherical caps and the conical band
// between them, nearest wins. The limb primitive - it tapers, which a capsule
// does not.
inline float round_cone(const glm::vec3 &p, const glm::vec3 &a,
                        const glm::vec3 &b, float r1, float r2) {
    const glm::vec3 ba = b - a;
    const float l2 = glm::dot(ba, ba);
    const float rr = r1 - r2;
    const float a2 = l2 - rr * rr;
    const float il2 = 1.0f / l2;

    const glm::vec3 pa = p - a;
    const float y = glm::dot(pa, ba);
    const float z = y - l2;
    const glm::vec3 cross_term = pa * l2 - ba * y;
    const float x2 = glm::dot(cross_term, cross_term);
    const float y2 = y * y * l2;
    const float z2 = z * z * l2;

    const float k = std::copysign(1.0f, rr) * rr * rr * x2;
    if (std::copysign(1.0f, z) * a2 * z2 > k) {
        return std::sqrt(x2 + z2) * il2 - r2;
    }
    if (std::copysign(1.0f, y) * a2 * y2 < k) {
        return std::sqrt(x2 + y2) * il2 - r1;
    }
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}

/// Gradient of a field by central differences. The surface normal, and the
/// direction collision pushes out along.
template <typename Field>
glm::vec3 gradient(const Field &field, const glm::vec3 &p, float e = 1.0e-3f) {
    return glm::normalize(glm::vec3(
        field(p + glm::vec3(e, 0, 0)) - field(p - glm::vec3(e, 0, 0)),
        field(p + glm::vec3(0, e, 0)) - field(p - glm::vec3(0, e, 0)),
        field(p + glm::vec3(0, 0, e)) - field(p - glm::vec3(0, 0, e))));
}

} // namespace sdf
