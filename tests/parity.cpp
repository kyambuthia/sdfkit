// The invariant the whole approach rests on: the CPU and GPU evaluators of
// the same field must agree.
//
// This first pass checks the CPU primitives against their own analytic
// properties - that a distance function really does return distance. The GPU
// half lands with the first game, once there is a scene worth sampling; the
// harness is here from commit one so it is never bolted on later.

#include <cmath>
#include <cstdio>
#include <random>

#include "sdfkit/sdf.hpp"

namespace {

int failures = 0;

void check(bool ok, const char *what, float got, float want) {
    if (!ok) {
        std::printf("  FAIL %-34s got %+.6f want %+.6f\n", what, got, want);
        ++failures;
    }
}

// A distance function must return the true distance to the surface. Walking
// exactly `d` along the gradient from any point must land on the surface.
template <typename Field>
void surface_lands_at_zero(const Field &field, const char *name, int samples) {
    std::mt19937 rng(0x5eed);
    std::uniform_real_distribution<float> space(-4.0f, 4.0f);
    float worst = 0.0f;
    for (int i = 0; i < samples; ++i) {
        const glm::vec3 p(space(rng), space(rng), space(rng));
        const float d = field(p);
        if (std::abs(d) < 1.0e-3f) continue;
        const glm::vec3 n = sdf::gradient(field, p);
        const float residual = std::abs(field(p - n * d));
        worst = std::max(worst, residual);
    }
    check(worst < 2.0e-3f, name, worst, 0.0f);
}

} // namespace

int main() {
    std::printf("sdfkit parity\n");

    surface_lands_at_zero(
        [](const glm::vec3 &p) { return sdf::sphere(p, glm::vec3(0.3f, 0.0f, -0.2f), 1.1f); },
        "sphere is a true distance", 400);

    surface_lands_at_zero(
        [](const glm::vec3 &p) {
            return sdf::round_box(p, glm::vec3(0.0f), glm::vec3(1.0f, 0.6f, 1.4f), 0.15f);
        },
        "round box is a true distance", 400);

    surface_lands_at_zero(
        [](const glm::vec3 &p) {
            return sdf::round_cone(p, glm::vec3(-0.5f, -0.8f, 0.0f),
                                   glm::vec3(0.4f, 1.1f, 0.2f), 0.5f, 0.18f);
        },
        "round cone is a true distance", 400);

    // smin must stay below both operands and reach each one away from the seam.
    const float fused = sdf::smin(0.4f, 0.9f, 0.3f);
    check(fused <= 0.4f + 1.0e-6f, "smin never exceeds its operands", fused, 0.4f);
    const float apart = sdf::smin(0.4f, 9.0f, 0.3f);
    check(std::abs(apart - 0.4f) < 1.0e-4f, "smin is exact far from the seam", apart, 0.4f);

    if (failures == 0) {
        std::printf("  all checks passed\n");
        return 0;
    }
    std::printf("  %d failed\n", failures);
    return 1;
}
