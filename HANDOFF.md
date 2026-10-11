# Handoff: finish the Android target for sdfkit

You are continuing work on `~/Projects/sdfkit` on the machine `ukandi`
(Debian 13). Read this whole file before changing anything. It records
constraints that were each discovered by hitting them; re-deriving them costs
hours, and several of them look like arbitrary choices until you know why.

## What the project is

A C++23 engine for games built entirely from signed distance fields. One
function `f(p)` is sphere-traced to draw the world and queried to collide with
it, so the rendered surface and the collision shape cannot drift apart. That
single-source property is the point of the project; do not break it.

Repo: https://github.com/kyambuthia/sdfkit (public, branch `trunk`, 3 commits)

```
include/sdfkit/sdf.hpp    distance primitives, CPU side
shaders/sdf.glsl          the same primitives, GPU side - a deliberate twin
include/sdfkit/app.hpp    Config, Params, Hooks, Setup, configure()
src/app.cpp               window, shader assembly, marcher, hot-reload, entry points
shaders/scene.glsl        the demo scene
examples/demo.cpp         the demo, which defines sdfkit::configure()
tests/parity.cpp          checks each primitive is a true distance function
android/                  gradle project (DOES NOT BUILD YET - your first task)
```

## Verified working - do not regress

- Desktop build: `cmake -S . -B build -G Ninja && cmake --build build`
- `./build/sdfkit_parity` passes.
- `./build/sdfkit_demo` opens a window on ukandi and renders the scene,
  running on **OpenGL ES 3.0 via EGL** (confirm with `ldd`: it links
  `libEGL`/`libGLESv2`, not desktop GL).
- Shader hot-reload: edit `shaders/scene.glsl` while the demo runs and the
  pipeline rebuilds on save. A shader that fails to compile logs the GLSL
  error and keeps the previous pipeline.

## Constraints discovered the hard way

Each of these caused a hard failure. Do not "simplify" them away.

1. **GLES 3.0, not a desktop core profile.** GLSL ES 300 reaches Android, iOS,
   WebGL2 and desktop Mesa from one shader body. ES 2.0 cannot do dynamic loop
   bounds and sphere tracing is exactly such a loop, so 3.0 is the floor.

2. **Context versions must be exact.** sokol asks for GL 4.1 by default. The
   dev machine's Mesa (Intel HD 3000, Sandy Bridge) tops out at a 3.3 desktop
   core profile, and **ES has no 3.3** - the versions are 3.0/3.1/3.2. A
   refused request is a hard panic, not a downgrade. `src/app.cpp` sets
   major=3 and minor=0 for GLES, minor=3 for GLCORE.

3. **Fragment shaders need `highp`.** A distance field at `mediump` tears
   apart at any distance from the origin. The precision qualifiers are
   injected per backend in `app.cpp`; a scene file never names a GL version.

4. **GLSL resolves in one pass.** The shared helpers (`sdf_normal`,
   `sdf_shadow`, `sdf_occlusion`) call `scene()`, and the scene's `shade()`
   calls the helpers. The prelude forward-declares `scene`, `sky` and `shade`
   so the two halves can see each other. Assembly order in `build_pipeline()`
   is: version header, prelude, `shaders/sdf.glsl`, helpers, scene, `main()`.

5. **X11, not native Wayland.** sokol_app uses X11, which Xwayland composites
   correctly under GNOME. An earlier Rust prototype using a native Wayland
   surface rendered exactly one frame and then stalled forever waiting on
   frame callbacks. X11 windows also appear in the X tree, which is how you
   screenshot them (see "How to verify" below).

6. **No compute shaders on this GPU.** That rules out sparse brick structures,
   so worlds here are analytic functions rather than editable volumes.

7. **sokol does not support `SOKOL_NO_ENTRY`/`sapp_run()` on Android** (stated
   in sokol_app.h). Android needs `sokol_main()` returning a descriptor;
   desktop needs a blocking `main()`. This is why the engine owns the entry
   point and a game defines `sdfkit::configure()` instead of `main()`.

## Your tasks, in order

### 1. Make the APK build (blocking, do this first)

```bash
cd ~/Projects/sdfkit/android
export ANDROID_HOME=$HOME/Android/Sdk ANDROID_SDK_ROOT=$HOME/Android/Sdk
./gradlew :app:assembleDebug
```

This currently fails and no APK is produced. The error has not been captured -
run it without filtering the output and read the actual message first.

Likely suspects, in order:
- `ndkVersion "29.0.14206865"` in `android/app/build.gradle` - confirm against
  `ls ~/Android/Sdk/ndk`.
- `externalNativeBuild { cmake { path "../../CMakeLists.txt" } }` reaches the
  root CMakeLists, which branches on `if(ANDROID)`. If the desktop-only
  targets (`sdfkit_parity`, the `sdfkit_shaders` copy target, `find_package(X11)`)
  are being configured anyway, that branch is wrong.
- The `syncShaders` Copy task may run before `src/main/assets` exists, or
  `${rootDir}/../shaders` may not resolve to `~/Projects/sdfkit/shaders`.

Toolchain versions that are known-good and already in the local caches, so a
build downloads nothing: **Gradle 8.14.3** (wrapper) with **AGP 8.7.3**. AGP
8.7.3 requires Gradle >= 8.9; an earlier attempt with the 8.7 wrapper copied
from `~/Projects/voxov/android` failed for exactly that reason.
`~/Projects/voxov/android` is a working sokol+gradle Android project on this
same machine - consult it when something does not add up.

On Android the demo builds as a **shared** library named `sdfkit_demo`, which
must match `android.app.lib_name` in the manifest, because the NativeActivity
dlopens it. The manifest uses a bare `android.app.NativeActivity` with
`android:hasCode="false"`: sokol_app implements `ANativeActivity_onCreate`
itself, so there is no Java of ours and no `native_app_glue`.

Shaders ship as APK assets and are read through `AAssetManager` via
`sapp_android_get_native_activity()` (see the `__ANDROID__` branch of
`read_file` in `src/app.cpp`). Hot-reload is desktop-only because an APK asset
has no mtime to watch.

### 2. Install, run, and prove it renders

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.sdfkit.demo/android.app.NativeActivity
adb logcat -d | grep -iE "sdfkit|sokol|sapp|GLES|libEGL|FATAL|AndroidRuntime" | tail -40
adb exec-out screencap -p > /tmp/phone.png
```

The device is an **itel L5503S**, Android 9, **armeabi-v7a**, PowerVR Rogue
GE8322 reporting **OpenGL ES 3.2**. It is a deliberately low-end target: the
frame time it gives is the number that decides whether this approach is
viable on mobile at all.

**Acceptance: you must look at `/tmp/phone.png` and confirm it shows the
scene** - undulating terrain, a sphere, a box and a tapered cone, soft
shadows, a pale sky. A black screen, a blank window, or "the process did not
crash" are all failures. Report the measured frame time.

### 3. Touch input

There is no keyboard on a phone and nothing currently reads touch. Add it to
the engine, not to the game:

- Handle `SAPP_EVENTTYPE_TOUCHES_BEGAN/MOVED/ENDED/CANCELLED` in
  `src/app.cpp`'s `event()`.
- Extend `Hooks` with a touch callback carrying normalised positions, ids and
  phase. Keep `KeyFn` for desktop.
- Prove it: make the demo's camera orbit follow a drag instead of a timer, and
  verify with two screenshots taken either side of
  `adb shell input swipe ...` showing a changed viewpoint.

### 4. App lifecycle

Handle `SAPP_EVENTTYPE_SUSPENDED` and `SAPP_EVENTTYPE_RESUMED`. On Android the
GL context can be lost on suspend; the pipeline and shader must be rebuilt on
resume. Prove it with `adb shell input keyevent KEYCODE_HOME` followed by
relaunching, then screenshot - a black screen after resume is the bug you are
looking for.

## How to verify anything on this machine

You cannot rely on a person looking at the screen: the keyboard on ukandi is
broken and its display is not being watched. Verify with screenshots you take
yourself.

Desktop window (X11, so it is in the X tree):

```bash
XA=$(ls /run/user/1000/.mutter-Xwaylandauth.* | head -1)
export DISPLAY=:0 XAUTHORITY=$XA XDG_RUNTIME_DIR=/run/user/1000
ID=$(xwininfo -root -tree | grep "sdfkit demo" | tail -1 | awk '{print $1}')
xwd -id $ID -out /tmp/w.xwd && ffmpeg -loglevel error -y -i /tmp/w.xwd /tmp/w.png
```

The Xwayland auth cookie **rotates when the GNOME session restarts**, so glob
for it every time rather than hard-coding the filename. `grim` does not work
(GNOME has no wlr-screencopy) and `gnome-screenshot` over D-Bus is denied.

Phone: `adb exec-out screencap -p > /tmp/phone.png`.

## Working agreement

- Commit per working increment, not per file. Write why in the message, not
  what - the diff already says what. Record causes of bugs you fix.
- Push to `trunk` after each increment: `git push origin trunk`. The remote is
  HTTPS and authenticates through the `gh` credential helper; the SSH key on
  this machine is passphrase-protected and cannot be used non-interactively.
- `./build/sdfkit_parity` must pass before every commit.
- Do not claim something works because it compiled or because the process
  stayed alive. Claim it when you have looked at a screenshot of it.
- If a task turns out to be blocked, finish the others and say plainly which
  one you left and why.

## After these tasks

The plan is four games, each in its own public repo, pulling sdfkit in as a
submodule, each adding exactly one capability to the engine:

1. **Tide** - a world that is a function of time; terrain that reshapes under
   the player.
2. **Prism** - reflection and refraction through glass solids.
3. **Descent** - fractal fields, which are poor distance bounds and need
   careful step clamping.
4. **Blob** - a deformable player character that splits, merges and squeezes
   through gaps.

Two further ideas (Burrow, Siege) were dropped because they need mutable
worlds, which need sparse brick structures, which need compute shaders this
GPU does not have.

Do not start these until the Android target above is finished and proven.
