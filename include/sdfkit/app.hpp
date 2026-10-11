#pragma once

// The shell every sdfkit game runs in: a window, a full-screen sphere-tracing
// pass, and shader hot-reload.
//
// The game supplies a GLSL fragment that defines the scene; sdfkit assembles
// it with the shared primitives and the marcher at runtime. Shaders are read
// as plain text every time, which is what makes hot-reload possible: iterating
// on a distance field means editing the field, and waiting on a C++ rebuild to
// do it would be the slowest part of the day.

#include <array>
#include <functional>
#include <string>

#include <glm/glm.hpp>

namespace sdfkit {

/// Uniforms every scene receives. Mirrored by the `params` block that
/// app.cpp prepends to the fragment shader - keep the two in step.
struct Params {
    glm::vec4 resolution_time{0.0f}; // xy = pixels, z = seconds, w = frame dt
    glm::vec4 eye{0.0f};
    glm::vec4 look_at{0.0f};
    /// Four scalars the game may use for whatever it likes.
    glm::vec4 tune{0.0f};
};

struct Config {
    std::string title = "sdfkit";
    int width = 1024;
    int height = 640;
    /// Scene GLSL, relative to the working directory. Reloaded when its
    /// mtime changes.
    std::string scene_shader = "shaders/scene.glsl";
    /// Render at 1/scale and upscale. Sphere tracing costs per fragment, so
    /// this is the cheapest real saving available on weak hardware.
    int render_scale = 2;
};

/// Called once per frame before drawing, to advance the game and fill in the
/// uniforms. `dt` is a fixed step.
using StepFn = std::function<void(Params &params, float dt)>;

/// Called with each key as it goes down or up.
using KeyFn = std::function<void(int key_code, bool down)>;

struct Hooks {
    StepFn step;
    KeyFn key;
};

/// Runs until the window closes. Never returns normally before then.
int run(const Config &config, Hooks hooks);

} // namespace sdfkit
