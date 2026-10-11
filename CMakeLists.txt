cmake_minimum_required(VERSION 3.20)
project(sdfkit CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# GLES3 by default: GLSL ES 300 reaches Android, iOS, WebGL2 and desktop Mesa
# from one shader body. -DSDFKIT_GLCORE=ON selects a desktop core profile.
option(SDFKIT_GLCORE "Use a desktop GL core profile instead of GLES3" OFF)

add_library(sdfkit STATIC src/app.cpp)
target_include_directories(sdfkit PUBLIC
    include
    dependencies/glm
    dependencies/sokol)

if(ANDROID)
    # sokol_app is the whole application here: it implements
    # ANativeActivity_onCreate itself, so there is no Java of ours and no
    # native_app_glue. No X11, and GLES is the only option.
    target_link_libraries(sdfkit PUBLIC android log EGL GLESv2)
else()
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(sdfkit PUBLIC
        X11::X11
        X11::Xi
        X11::Xcursor
        Threads::Threads
        ${CMAKE_DL_LIBS})
    if(SDFKIT_GLCORE)
        target_compile_definitions(sdfkit PUBLIC SOKOL_GLCORE)
        find_package(OpenGL REQUIRED)
        target_link_libraries(sdfkit PUBLIC OpenGL::GL)
    else()
        # sokol reaches GLES3 through EGL on Linux too.
        target_link_libraries(sdfkit PUBLIC EGL GLESv2)
    endif()
endif()

if(ANDROID)
    # The manifest names this library and the NativeActivity dlopens it, so
    # it must be SHARED and must match android.app.lib_name.
    add_library(sdfkit_demo SHARED examples/demo.cpp)
    target_link_libraries(sdfkit_demo PRIVATE sdfkit)
else()
    # Shaders are read at runtime, so a desktop build only has to put them
    # where the binary looks. The Android build ships them as APK assets.
    add_custom_target(sdfkit_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMENT "Staging shaders")
    add_dependencies(sdfkit sdfkit_shaders)

    add_executable(sdfkit_demo examples/demo.cpp)
    target_link_libraries(sdfkit_demo PRIVATE sdfkit)

    enable_testing()
    add_executable(sdfkit_parity tests/parity.cpp)
    target_link_libraries(sdfkit_parity PRIVATE sdfkit)
    add_test(NAME parity COMMAND sdfkit_parity)
endif()
