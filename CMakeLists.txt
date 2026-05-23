cmake_minimum_required(VERSION 3.20)
project(apple1emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── FetchContent dependencies ─────────────────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(nlohmann_json glfw imgui)

# ── Build ImGui as a library (GLFW + OpenGL3 backend) ────────────────────────
find_package(OpenGL REQUIRED)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# ── Core emulator library ─────────────────────────────────────────────────────
add_library(apple1core STATIC
    src/cpu/fake6502.cpp
    src/apple1/bus.cpp
    src/apple1/pia.cpp
    src/apple1/apple1.cpp
    src/frontend/terminal_frontend.cpp
    src/frontend/gui_frontend.cpp
)
target_include_directories(apple1core PUBLIC src)
target_link_libraries(apple1core PUBLIC nlohmann_json::nlohmann_json imgui_lib)

# ── Terminal emulator executable ──────────────────────────────────────────────
add_executable(apple1emu src/main.cpp)
target_link_libraries(apple1emu PRIVATE apple1core)

# ── Compiler warnings ─────────────────────────────────────────────────────────
target_compile_options(apple1core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(apple1emu  PRIVATE -Wall -Wextra -Wpedantic)
