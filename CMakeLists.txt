cmake_minimum_required(VERSION 3.20)
project(sdfkit CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(sdfkit STATIC src/app.cpp)
target_include_directories(sdfkit PUBLIC
    include
    dependencies/glm
    dependencies/sokol)

# sokol_app on Linux wants X11 and GL. Xwayland presents these windows
# correctly under GNOME, which a native Wayland surface did not.
find_package(OpenGL REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(sdfkit PUBLIC
    OpenGL::GL
    X11::X11
    X11::Xi
    X11::Xcursor
    Threads::Threads
    ${CMAKE_DL_LIBS})

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
