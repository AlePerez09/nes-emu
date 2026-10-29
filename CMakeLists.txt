cmake_minimum_required(VERSION 3.16)
project(nes_emu LANGUAGES C CXX)  # C is for SDL2 when it is built from source

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Downloaded dependencies (nlohmann/json for tests, SDL2 if it isn't installed)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)

# Strict warnings for our code only (not for downloaded dependencies like SDL2).
add_library(nes_warnings INTERFACE)
if(MSVC)
  target_compile_options(nes_warnings INTERFACE /W4)
else()
  target_compile_options(nes_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# ---- Core emulator (no SDL dependency, so tests can run headless) ----
add_library(nes_core
  src/core/cpu.cpp
  src/core/bus.cpp
  src/core/ppu.cpp
  src/core/cartridge.cpp
  src/core/nes.cpp
  src/core/mappers/mapper_000.cpp
)
target_include_directories(nes_core PUBLIC src)
target_link_libraries(nes_core PRIVATE nes_warnings)

# ---- nestest trace-diff harness ----
add_executable(nestest_runner tests/nestest_runner.cpp)
target_link_libraries(nestest_runner PRIVATE nes_core nes_warnings)

enable_testing()
set(TEST_ROM_DIR ${CMAKE_SOURCE_DIR}/tests/roms)
if(EXISTS ${TEST_ROM_DIR}/nestest.nes AND EXISTS ${TEST_ROM_DIR}/nestest.log)
  add_test(NAME nestest
           COMMAND nestest_runner ${TEST_ROM_DIR}/nestest.nes ${TEST_ROM_DIR}/nestest.log)
else()
  message(STATUS "nestest ROM/log not found in tests/roms (run tests/roms/fetch_test_roms.sh)")
endif()

# ---- Per-opcode CPU tests (SingleStepTests; downloads nlohmann/json at configure time) ----
option(NES_BUILD_CPU_TESTS "Build the per-opcode CPU test runner" ON)
if(NES_BUILD_CPU_TESTS)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)

  add_executable(cpu_tests tests/cpu_tests.cpp)
  target_link_libraries(cpu_tests PRIVATE nes_core nes_warnings nlohmann_json::nlohmann_json)
  add_test(NAME cpu_opcodes COMMAND cpu_tests ${CMAKE_SOURCE_DIR}/tests/cpu)
endif()

# ---- SDL2 frontend ----
# Uses an installed SDL2 if there is one; otherwise downloads and builds it statically
# (takes a minute on the first configure). Turn the window off with -DNES_BUILD_FRONTEND=OFF.
option(NES_BUILD_FRONTEND "Build the SDL2 window executable" ON)
if(NES_BUILD_FRONTEND)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    message(STATUS "Using installed SDL2")
    if(TARGET SDL2::SDL2)
      set(NES_SDL_TARGET SDL2::SDL2)
    else()
      set(NES_SDL_TARGET ${SDL2_LIBRARIES})
      set(NES_SDL_INCLUDES ${SDL2_INCLUDE_DIRS})
    endif()
  else()
    message(STATUS "SDL2 not installed: downloading and building it")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
      URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
    FetchContent_MakeAvailable(SDL2)
    set(NES_SDL_TARGET SDL2::SDL2-static)
  endif()

  add_executable(nes src/frontend/main.cpp)
  target_include_directories(nes PRIVATE ${NES_SDL_INCLUDES})
  target_link_libraries(nes PRIVATE nes_core nes_warnings ${NES_SDL_TARGET})
endif()
