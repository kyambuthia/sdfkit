#include "sdfkit/app.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#define SOKOL_IMPL
// GLES3 by default: GLSL ES 300 reaches Android 4.3+, iOS, WebGL2 and
// desktop Mesa, where a desktop core profile reaches none of the first
// three. The shader body is identical either way; only the version header
// and precision qualifiers differ, and those are injected below.
#if !defined(SOKOL_GLCORE)
#define SOKOL_GLES3
#endif
// Desktop: sokol_app would supply its own main(); we drive it through
// sapp_run() instead. Android has no main() at all and sokol explicitly does
// not support sapp_run() there, so it keeps sokol_main().

#if !defined(__ANDROID__)
#define SOKOL_NO_ENTRY
#endif
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"

namespace sdfkit {
namespace {

namespace fs = std::filesystem;

// Prepended to every scene. Declares the uniforms and pulls in the shared
// primitives; the scene then only has to define `scene()` and `shade()`.
constexpr const char *kVersion =
#if defined(SOKOL_GLES3)
    // ES guarantees highp in fragment shaders, and a distance field needs
    // it - at mediump the surface tears apart at any distance.
    "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
    "#version 330\n";
#endif

constexpr const char *kPrelude = R"(
out vec4 frag_colour;
uniform vec4 params[4];
#define RESOLUTION params[0].xy
#define TIME       params[0].z
#define DT         params[0].w
#define EYE        params[1].xyz
#define LOOK_AT    params[2].xyz
#define TUNE       params[3]

// The scene supplies these three; declared here so the shared helpers below
// can call scene() and the scene can call the helpers. GLSL has no two-pass
// resolution, so without this the two halves cannot see each other.
float scene(vec3 p);
vec3 sky(vec3 dir);
vec3 shade(vec3 p, vec3 n, float t);
)";

// Appended after the scene. The scene supplies `float scene(vec3 p)` and
// `vec3 shade(vec3 p, vec3 n, float t)`; everything here is shared.
constexpr const char *kHelpers = R"(
vec3 sdf_normal(vec3 p) {
    // Tetrahedron trick: four taps instead of six.
    vec2 e = vec2(1.0, -1.0) * 0.5773 * 0.0015;
    return normalize(e.xyy * scene(p + e.xyy) + e.yyx * scene(p + e.yyx) +
                     e.yxy * scene(p + e.yxy) + e.xxx * scene(p + e.xxx));
}

float sdf_shadow(vec3 origin, vec3 dir, float far) {
    float result = 1.0;
    float t = 0.05;
    for (int i = 0; i < 32; i++) {
        float d = scene(origin + dir * t);
        if (d < 0.001) return 0.0;
        result = min(result, 9.0 * d / t);
        t += clamp(d * 0.9, 0.03, 0.6);
        if (t > far) break;
    }
    return clamp(result, 0.0, 1.0);
}

float sdf_occlusion(vec3 p, vec3 n) {
    float occlusion = 0.0;
    float scale = 1.0;
    for (int i = 1; i <= 4; i++) {
        float h = 0.025 * float(i) * float(i);
        occlusion += (h - scene(p + n * h)) * scale;
        scale *= 0.68;
    }
    return clamp(1.0 - 1.5 * occlusion, 0.0, 1.0);
}

)";

// Appended last, after the scene has defined its three entry points.
constexpr const char *kMain = R"(
void main() {
    vec2 uv = (2.0 * gl_FragCoord.xy - RESOLUTION) / RESOLUTION.y;
    vec3 forward = normalize(LOOK_AT - EYE);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);
    vec3 dir = normalize(forward * 1.7 + right * uv.x + up * uv.y);

    float t = 0.0;
    bool hit = false;
    for (int i = 0; i < 160; i++) {
        float d = scene(EYE + dir * t);
        if (d < 0.0008 * t + 0.0006) { hit = true; break; }
        // 0.8, not 1.0: smooth-min lets the field overestimate, and a full
        // step overshoots the surface and speckles the silhouette.
        t += d * 0.8;
        if (t > 120.0) break;
    }

    vec3 colour;
    if (hit) {
        vec3 p = EYE + dir * t;
        colour = shade(p, sdf_normal(p), t);
    } else {
        colour = sky(dir);
    }
    frag_colour = vec4(pow(max(colour, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
}
)";

constexpr const char *kVertex = R"(
void main() {
    // Full-screen triangle: three vertices, no vertex buffer.
    vec2 uv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/native_activity.h>

// Shaders live inside the APK, not on the filesystem, so they come through
// the asset manager. Hot-reload is desktop-only for the same reason: an
// asset has no mtime to watch.
std::string read_file(const fs::path &path) {
    auto *activity = static_cast<ANativeActivity *>(
        const_cast<void *>(sapp_android_get_native_activity()));
    if (!activity) return {};
    AAsset *asset = AAssetManager_open(activity->assetManager,
                                       path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return {};
    const auto length = static_cast<size_t>(AAsset_getLength(asset));
    std::string out(static_cast<const char *>(AAsset_getBuffer(asset)), length);
    AAsset_close(asset);
    return out;
}
#else
std::string read_file(const fs::path &path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}
#endif

struct State {
    Config config;
    Hooks hooks;
    Params params{};
    sg_pipeline pipeline{};
    sg_shader shader{};
    bool pipeline_valid = false;
    fs::file_time_type scene_stamp{};
    fs::file_time_type shared_stamp{};
    double elapsed = 0.0;
    int reloads = 0;
};

State g;

fs::file_time_type stamp_of(const fs::path &path) {
    std::error_code ec;
    auto stamp = fs::last_write_time(path, ec);
    return ec ? fs::file_time_type{} : stamp;
}

/// Assemble prelude + shared primitives + scene + marcher, and build a
/// pipeline from it. Returns false and leaves the old pipeline in place when
/// the new source does not compile, so a typo mid-edit does not kill the app.
bool build_pipeline() {
    const std::string shared = read_file("shaders/sdf.glsl");
    const std::string scene = read_file(g.config.scene_shader);
    if (shared.empty()) {
        std::fprintf(stderr, "[sdfkit] missing shaders/sdf.glsl\n");
        return false;
    }
    if (scene.empty()) {
        std::fprintf(stderr, "[sdfkit] missing %s\n", g.config.scene_shader.c_str());
        return false;
    }
    const std::string fragment =
        kVersion + std::string(kPrelude) + shared + kHelpers + scene + kMain;

    sg_shader_desc shader_desc{};
    const std::string vertex = kVersion + std::string(kVertex);
    shader_desc.vertex_func.source = vertex.c_str();
    shader_desc.fragment_func.source = fragment.c_str();
    shader_desc.label = "sdf";
    shader_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shader_desc.uniform_blocks[0].size = sizeof(Params);
    shader_desc.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    shader_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shader_desc.uniform_blocks[0].glsl_uniforms[0].array_count = 4;
    shader_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "params";

    const sg_shader shader = sg_make_shader(&shader_desc);
    if (sg_query_shader_state(shader) != SG_RESOURCESTATE_VALID) {
        sg_destroy_shader(shader);
        std::fprintf(stderr, "[sdfkit] shader failed to compile, keeping previous\n");
        return false;
    }

    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shader;
    pipeline_desc.label = "sdf";
    const sg_pipeline pipeline = sg_make_pipeline(&pipeline_desc);
    if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID) {
        sg_destroy_pipeline(pipeline);
        sg_destroy_shader(shader);
        return false;
    }

    if (g.pipeline_valid) {
        sg_destroy_pipeline(g.pipeline);
        sg_destroy_shader(g.shader);
    }
    g.pipeline = pipeline;
    g.shader = shader;
    g.pipeline_valid = true;
    return true;
}

void init() {
    sg_desc desc{};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);

    g.scene_stamp = stamp_of(g.config.scene_shader);
    g.shared_stamp = stamp_of("shaders/sdf.glsl");
    if (!build_pipeline()) {
        std::fprintf(stderr, "[sdfkit] no usable shader at startup\n");
    }
}

void frame() {
    const float dt = static_cast<float>(sapp_frame_duration());
    g.elapsed += dt;

    // Hot-reload: cheap stat, once a frame.
    const auto scene_now = stamp_of(g.config.scene_shader);
    const auto shared_now = stamp_of("shaders/sdf.glsl");
    if (scene_now != g.scene_stamp || shared_now != g.shared_stamp) {
        g.scene_stamp = scene_now;
        g.shared_stamp = shared_now;
        if (build_pipeline()) {
            std::printf("[sdfkit] shader reloaded (%d)\n", ++g.reloads);
            std::fflush(stdout);
        }
    }

    g.params.resolution_time = glm::vec4(static_cast<float>(sapp_width()),
                                         static_cast<float>(sapp_height()),
                                         static_cast<float>(g.elapsed), dt);
    if (g.hooks.step) {
        g.hooks.step(g.params, dt);
    }

    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    if (g.pipeline_valid) {
        sg_apply_pipeline(g.pipeline);
        const sg_range range{&g.params, sizeof(Params)};
        sg_apply_uniforms(0, &range);
        sg_draw(0, 3, 1);
    }
    sg_end_pass();
    sg_commit();
}

void event(const sapp_event *e) {
    if (e->type == SAPP_EVENTTYPE_KEY_DOWN && e->key_code == SAPP_KEYCODE_ESCAPE) {
        sapp_request_quit();
        return;
    }
    if (g.hooks.key && (e->type == SAPP_EVENTTYPE_KEY_DOWN ||
                        e->type == SAPP_EVENTTYPE_KEY_UP)) {
        g.hooks.key(static_cast<int>(e->key_code),
                    e->type == SAPP_EVENTTYPE_KEY_DOWN);
    }
}

void cleanup() {
    sg_shutdown();
}

} // namespace


/// Builds the descriptor and installs the game's hooks. Shared by both entry
/// points below.
sapp_desc describe() {
    sdfkit::Setup setup = sdfkit::configure();
    g.config = setup.config;
    g.hooks = std::move(setup.hooks);

    sapp_desc desc{};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.event_cb = event;
    desc.cleanup_cb = cleanup;
    desc.width = g.config.width;
    desc.height = g.config.height;
    desc.window_title = g.config.title.c_str();
    // sokol defaults to GL 4.1, which neither backend here can give: Mesa on
    // the development machine tops out at a 3.3 desktop core profile, and ES
    // has no version above 3.2. A refused request is a hard panic rather
    // than a downgrade, so it has to be asked for correctly per backend.
    desc.gl.major_version = 3;
#if defined(SOKOL_GLES3)
    // ES versions are 3.0, 3.1, 3.2 - there is no 3.3. Carrying the desktop
    // minor version over fails eglCreateContext outright.
    desc.gl.minor_version = 0;
#else
    desc.gl.minor_version = 3;
#endif
    desc.logger.func = slog_func;
    return desc;
}

} // namespace sdfkit

#if defined(__ANDROID__)
// Android has no main(); the native activity calls sokol_main() for the
// descriptor and sokol drives the loop itself.
sapp_desc sokol_main(int, char **) {
    return sdfkit::describe();
}
#else
int main() {
    sapp_desc desc = sdfkit::describe();
    sapp_run(&desc);
    return 0;
}
#endif
