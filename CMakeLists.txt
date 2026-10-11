cmake_minimum_required(VERSION 3.20)
project(sdfkit CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(sdfkit STATIC src/app.cpp)

# GLES3 by default: GLSL ES 300 reaches Android, iOS, WebGL2 and desktop Mesa
# from one shader body. Set -DSDFKIT_GLCORE=ON for a desktop core profile.
option(SDFKIT_GLCORE "Use a desktop GL core profile instead of GLES3" OFF)

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
    # sokol reaches GLES3 through EGL on Linux.
    target_link_libraries(sdfkit PUBLIC EGL GLESv2)
endif()
target_include_directories(sdfkit PUBLIC
    include
    dependencies/glm
    dependencies/sokol)

# Shaders are read at runtime, so a build only has to put them where the
# binary will look. Copying rather than symlinking keeps the build tree
# self-contained; hot-reload watches the source tree copy you actually edit.
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
