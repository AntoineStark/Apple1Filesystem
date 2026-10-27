include(FetchContent)
find_package(OpenGL REQUIRED)
find_package(SDL2 2.0.18 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(apple1_sdl2
    URL https://github.com/libsdl-org/SDL/archive/5d249570393f7a37e037abf22cd6012a4cc56a71.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(apple1_sdl2)
endif()

FetchContent_Declare(apple1_imgui
  URL https://github.com/ocornut/imgui/archive/4806a1924ff6181180bf5e4b8b79ab4394118875.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(apple1_imgui)

add_executable(apple1-gui
  ${CMAKE_CURRENT_LIST_DIR}/../src/gui.cpp
  ${apple1_imgui_SOURCE_DIR}/imgui.cpp
  ${apple1_imgui_SOURCE_DIR}/imgui_draw.cpp
  ${apple1_imgui_SOURCE_DIR}/imgui_tables.cpp
  ${apple1_imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${apple1_imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${apple1_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(apple1-gui PRIVATE
  ${apple1_imgui_SOURCE_DIR} ${apple1_imgui_SOURCE_DIR}/backends)
target_compile_features(apple1-gui PRIVATE cxx_std_20)
target_compile_definitions(apple1-gui PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(apple1-gui PRIVATE apple1_core SDL2::SDL2 OpenGL::GL)
if(APPLE)
  target_compile_definitions(apple1-gui PRIVATE GL_SILENCE_DEPRECATION)
endif()

add_custom_target(apple1-gui-smoke
  COMMAND $<TARGET_FILE:apple1-gui> --smoke-test
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/apple1-gui-smoke.ppm
  DEPENDS apple1-gui
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  USES_TERMINAL)

add_custom_target(apple1-gui-input-smoke
  COMMAND $<TARGET_FILE:apple1-gui> --input-smoke-test
  COMMAND $<TARGET_FILE:apple1-gui> --profile standard --input-smoke-test
  DEPENDS apple1-gui
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  USES_TERMINAL)