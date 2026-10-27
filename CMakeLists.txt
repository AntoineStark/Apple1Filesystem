cmake_minimum_required(VERSION 3.24)
project(apple1_emulator VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(APPLE1_GUI "Build the Dear ImGui frontend" OFF)
add_library(apple1_core src/cpu.cpp src/machine.cpp src/ramfs_view.cpp)
target_include_directories(apple1_core PUBLIC include)
target_compile_options(apple1_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(apple1-terminal src/terminal.cpp)
target_link_libraries(apple1-terminal PRIVATE apple1_core)
if(APPLE1_GUI)
  include(cmake/Gui.cmake)
endif()
include(CTest)
option(APPLE1_OS "Build guest 6502 operating-system components" ON)
if(APPLE1_OS)
  find_program(APPLE1_CA65 ca65 REQUIRED)
  find_program(APPLE1_LD65 ld65 REQUIRED)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ramfs.bin
    COMMAND ${APPLE1_CA65} ${CMAKE_CURRENT_SOURCE_DIR}/os/ramfs.s -o ${CMAKE_CURRENT_BINARY_DIR}/ramfs.o
    COMMAND ${APPLE1_LD65} -C ${CMAKE_CURRENT_SOURCE_DIR}/os/ramfs.cfg
      ${CMAKE_CURRENT_BINARY_DIR}/ramfs.o -o ${CMAKE_CURRENT_BINARY_DIR}/ramfs.bin
      -m ${CMAKE_CURRENT_BINARY_DIR}/ramfs.map
    DEPENDS os/ramfs.s os/ramfs.cfg
    VERBATIM)
  add_custom_target(apple1-ramfs ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ramfs.bin)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/a1os.rom
    COMMAND ${APPLE1_CA65} ${CMAKE_CURRENT_SOURCE_DIR}/os/shell.s -o ${CMAKE_CURRENT_BINARY_DIR}/shell.o
    COMMAND ${APPLE1_LD65} -C ${CMAKE_CURRENT_SOURCE_DIR}/os/shell.cfg
      ${CMAKE_CURRENT_BINARY_DIR}/ramfs.o ${CMAKE_CURRENT_BINARY_DIR}/shell.o
      -o ${CMAKE_CURRENT_BINARY_DIR}/a1os.rom -m ${CMAKE_CURRENT_BINARY_DIR}/a1os.map
    DEPENDS apple1-ramfs ${CMAKE_CURRENT_BINARY_DIR}/ramfs.bin os/shell.s os/shell.cfg
    VERBATIM)
  add_custom_target(apple1-os ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/a1os.rom)
  if(TARGET apple1-gui)
    add_dependencies(apple1-gui apple1-os)
    target_compile_definitions(apple1-gui PRIVATE APPLE1_OS_ROM="${CMAKE_CURRENT_BINARY_DIR}/a1os.rom")
  endif()
endif()
if(BUILD_TESTING)
  if(APPLE1_OS)
    add_executable(ramfs_tests tests/ramfs_tests.cpp)
    target_link_libraries(ramfs_tests PRIVATE apple1_core)
    target_compile_definitions(ramfs_tests PRIVATE APPLE1_RAMFS_IMAGE="${CMAKE_CURRENT_BINARY_DIR}/ramfs.bin")
    add_dependencies(ramfs_tests apple1-ramfs)
    add_test(NAME ramfs COMMAND ramfs_tests)
    add_executable(shell_tests tests/shell_tests.cpp)
    target_link_libraries(shell_tests PRIVATE apple1_core)
    target_compile_definitions(shell_tests PRIVATE APPLE1_SHELL_ROM="${CMAKE_CURRENT_BINARY_DIR}/a1os.rom")
    add_dependencies(shell_tests apple1-os)
    add_test(NAME shell COMMAND shell_tests)
  endif()
  add_executable(cpu_tests tests/cpu_tests.cpp)
  target_link_libraries(cpu_tests PRIVATE apple1_core)
  add_test(NAME cpu COMMAND cpu_tests)
  add_executable(machine_tests tests/machine_tests.cpp)
  target_link_libraries(machine_tests PRIVATE apple1_core)
  target_compile_definitions(machine_tests PRIVATE APPLE1_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME machine COMMAND machine_tests)
  add_executable(boot_tests tests/boot_tests.cpp)
  target_link_libraries(boot_tests PRIVATE apple1_core)
  target_compile_definitions(boot_tests PRIVATE APPLE1_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME boot COMMAND boot_tests)
  add_test(NAME terminal COMMAND ${CMAKE_COMMAND}
    -DTERMINAL=$<TARGET_FILE:apple1-terminal>
    -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/terminal_tests.cmake)
  set(APPLE1_FUNCTIONAL_ROM "" CACHE FILEPATH "Optional Klaus 6502 functional test image")
  if(APPLE1_FUNCTIONAL_ROM)
    add_test(NAME cpu_functional COMMAND cpu_tests "${APPLE1_FUNCTIONAL_ROM}")
  endif()
endif()
