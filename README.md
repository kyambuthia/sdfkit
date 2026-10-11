# sdfkit

The shared engine behind a set of games built entirely on signed distance
fields. One `f(p)` is sphere-traced to draw the world and queried to collide
with it, so the surface you see and the shape you hit cannot drift apart.

C++23, sokol, GL 3.3 core.

## Build

```bash
git clone --recurse-submodules https://github.com/kyambuthia/sdfkit.git
cd sdfkit
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sdfkit_demo
```

## Shader hot-reload

Shaders are plain text read at runtime, not baked into the binary. Edit
`shaders/scene.glsl` while the demo is running and the pipeline rebuilds on
save; a shader that fails to compile logs its error and leaves the previous
one in place, so a typo mid-edit does not kill the app.

Iterating on a distance field means editing the field. Waiting on a C++
rebuild to do that would be the slowest part of the day.

## Writing a scene

A scene defines three functions. Everything else - the marcher, normals,
shadows, occlusion, the camera basis - comes from the kit.

```glsl
float scene(vec3 p);              // the distance field
vec3  sky(vec3 dir);              // what a ray that hits nothing sees
vec3  shade(vec3 p, vec3 n, float t);
```

Available to them: `RESOLUTION`, `TIME`, `DT`, `EYE`, `LOOK_AT`, `TUNE`, the
primitives in `shaders/sdf.glsl`, and `sdf_normal`, `sdf_shadow`,
`sdf_occlusion`.

## Parity

`include/sdfkit/sdf.hpp` and `shaders/sdf.glsl` are twins: the same
primitives, once for collision on the CPU and once for rendering on the GPU.
Two implementations of one definition drift, and the symptom is a physics bug
nobody connects to a rendering change weeks later.

`ctest` checks that each primitive really is a distance function - walking the
reported distance along the gradient must land on the surface. The GPU half of
the comparison lands with the first game, once there is a scene worth
sampling.

## Portability

The default backend is **OpenGL ES 3.0 / GLSL ES 300**, chosen for reach:

| target | how |
| --- | --- |
| Android 4.3+ (2013 onward) | native, GLES3 |
| iOS | native, GLES3 |
| Desktop Linux / Windows / macOS | Mesa, or ANGLE over D3D/Metal |
| Browsers | WebGL2, which is GLES3 |

ES 2.0 would reach further still, but its shading language cannot do dynamic
loop bounds, and sphere tracing is a loop that exits when it hits something.
So 3.0 is the floor.

Build with `-DSDFKIT_GLCORE=ON` for a desktop 3.3 core profile instead. The
shader body is identical either way; `src/app.cpp` injects the version line
and the ES precision qualifiers per backend, so a scene never names a GL
version. Fragment shaders declare `highp` because a distance field at
`mediump` tears apart at any distance from the origin.

## Hardware notes

Developed against a Mesa Intel HD Graphics 3000 (Sandy Bridge, 2011), which
shapes several choices:

- **X11 via sokol_app**, which Xwayland composites correctly under GNOME. A
  native Wayland surface on the same machine rendered one frame and then
  stalled waiting for frame callbacks that never arrived. X11 windows also
  appear in the X tree, so a screenshot by window id can confirm what was
  drawn.
- **Context versions must be asked for exactly.** sokol defaults to GL 4.1;
  this chip refuses it, ES has no version above 3.2, and a refused request is
  a hard panic rather than a downgrade.
- **No compute shaders.** That rules out the sparse brick structures a mutable
  world would need, so worlds here are analytic functions rather than editable
  volumes.
