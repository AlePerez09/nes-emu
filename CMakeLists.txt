cmake_minimum_required(VERSION 3.16)
project(nes_emu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
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

# ---- nestest trace-diff harness ----
add_executable(nestest_runner tests/nestest_runner.cpp)
target_link_libraries(nestest_runner PRIVATE nes_core)

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
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)

  add_executable(cpu_tests tests/cpu_tests.cpp)
  target_link_libraries(cpu_tests PRIVATE nes_core nlohmann_json::nlohmann_json)
  add_test(NAME cpu_opcodes COMMAND cpu_tests ${CMAKE_SOURCE_DIR}/tests/cpu)
endif()

# ---- SDL2 frontend (optional) ----
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(nes src/frontend/main.cpp)
  if(TARGET SDL2::SDL2)
    target_link_libraries(nes PRIVATE nes_core SDL2::SDL2)
  else()
    target_include_directories(nes PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(nes PRIVATE nes_core ${SDL2_LIBRARIES})
  endif()
else()
  message(STATUS "SDL2 not found: building core + tests only (no 'nes' window executable)")
endif()
