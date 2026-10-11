// Proves the shell: a window, a sphere-traced scene, an orbiting camera, and
// shader hot-reload. Edit shaders/scene.glsl while this runs.
//
// There is no main(). sdfkit owns the entry point so the same game source
// builds for desktop and for Android, which have incompatible ones.

#include <cmath>

#include "sdfkit/app.hpp"

namespace sdfkit {

Setup configure() {
    Setup setup;
    setup.config.title = "sdfkit demo";
    setup.config.scene_shader = "shaders/scene.glsl";

    setup.hooks.step = [](Params &params, float) {
        const float t = params.resolution_time.z;
        const float angle = t * 0.25f;
        params.eye = glm::vec4(std::sin(angle) * 9.0f, 3.4f, std::cos(angle) * 9.0f, 0.0f);
        params.look_at = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
    };
    return setup;
}

} // namespace sdfkit
