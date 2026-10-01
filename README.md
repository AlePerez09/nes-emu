# nes-emu

A Nintendo Entertainment System emulator written in C++17.

**Status:** Part 1 of the CPU is done: all 10 addressing modes plus loads, stores, transfers, flags, increments/decrements, JMP, and NOP (59 opcodes), each passing 10,000 SingleStepTests cases. Stack, branches, arithmetic, logic, and shifts are next, followed by PPU rendering.

## Building

Requires CMake 3.16+ and a C++17 compiler. SDL2 is optional: without it you get the core library and test harnesses only. The first configure downloads nlohmann/json for the CPU tests (turn off with `-DNES_BUILD_CPU_TESTS=OFF`).

```sh
# Linux:   sudo apt install libsdl2-dev
# macOS:   brew install sdl2
# Windows: vcpkg install sdl2, then add -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

cmake -S . -B build
cmake --build build
```

Run a ROM (SDL2 build only):

```sh
./build/nes path/to/game.nes
```

Controls: arrows = D-pad, X = A, Z = B, Enter = Start, Right Shift = Select, Tab = pattern-table debug view, Esc = quit.

## Testing

Two complementary CPU test suites:

**Per-opcode tests** ([SingleStepTests](https://github.com/SingleStepTests/65x02/tree/main/nes6502)): 10,000 randomized before/after cases per opcode, checking registers, flags, memory, and cycle count. Best for building the CPU one instruction at a time.

```sh
sh tests/cpu/fetch_cpu_tests.sh          # fetches the implemented opcodes (~3 MB each, gitignored)
sh tests/cpu/fetch_cpu_tests.sh 20 60    # fetch more by opcode
./build/cpu_tests tests/cpu              # run everything present
./build/cpu_tests tests/cpu a9 bd        # or just some opcodes
```

```
bd  FAIL 4983/10000  first: [bd f5 ba  ->  cycles: got $4, want $5]
```

**nestest** runs a whole test program and diffs a trace against a known-good log. It stops at `JSR` (line 6) until Part 2 is in.

```sh
sh tests/roms/fetch_test_roms.sh     # downloads nestest.nes + nestest.log (gitignored)
cmake -S . -B build                  # re-run so CTest picks up the ROM
./build/nestest_runner tests/roms/nestest.nes tests/roms/nestest.log
ctest --test-dir build
```

The runner starts nestest at `$C000`, compares PC, registers, flags, and cycle count against the golden log before every instruction, and stops at the first mismatch or unimplemented opcode:

```
Matched 5 line(s). Can't execute log line 6: opcode $20 not implemented
    C5FD  20 2D C7  JSR $C72D   A:00 X:00 Y:00 P:26 SP:FD PPU:  0, 63 CYC:21
```

Pass an optional third argument to stop after N lines (the official-opcode section is roughly the first 5,000; unofficial opcodes follow).

## Architecture

```
src/core/
  nes.h/.cpp          Owns all components; runs CPU instructions and catches the PPU up (3 dots per CPU cycle)
  cpu_bus.h           Read/write interface the CPU talks to (real Bus, or flat RAM in tests)
  cpu.h/.cpp          2A03 (6502 without decimal mode), instruction-stepped interpreter
  bus.h/.cpp          CPU memory map: RAM mirroring, PPU registers, OAM DMA, controllers, cartridge
  ppu.h/.cpp          2C02: registers, loopy v/t/x/w, VRAM + nametable mirroring, palette, vblank/NMI
  cartridge.h/.cpp    iNES parsing, PRG/CHR ownership, mapper factory
  mappers/            Mapper base class + one subclass per board (NROM so far)
src/frontend/main.cpp SDL2 window, input, framebuffer presentation
tests/                nestest trace-diff harness, per-opcode CPU test runner
```

The core has no SDL dependency, so tests run headless and in CI.

## Roadmap

- [x] iNES loading, mapper 0 (NROM)
- [x] Bus, RAM/register mirroring, OAM DMA, controllers
- [x] nestest trace-diff harness
- [x] All addressing modes; loads, stores, transfers, flags, inc/dec, JMP (per-opcode tests passing)
- [ ] Stack, JSR/RTS/RTI/BRK, branches, ADC/SBC, compares, logic, shifts: nestest official section passing
- [ ] Unofficial opcodes: full nestest passing
- [ ] PPU background rendering, then sprites, sprite 0 hit, scrolling
- [ ] Donkey Kong, then Super Mario Bros. playable
- [ ] Mappers 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3)
- [ ] APU (2 pulse, triangle, noise, DMC)
- [ ] blargg test ROMs in CI, save states, battery saves, debugger

## References

- [NESdev Wiki](https://www.nesdev.org/wiki/Nesdev_Wiki): CPU, PPU, mappers, everything
- [6502 instruction set](https://www.masswerk.at/6502/6502_instruction_set.html): opcodes, modes, cycles
- [Tom Harte's ProcessorTests](https://github.com/SingleStepTests/ProcessorTests): per-opcode JSON tests
- [nes-test-roms](https://github.com/christopherpow/nes-test-roms): nestest, blargg, and more

ROMs are not included. Use test ROMs, homebrew, or dumps of cartridges you own.
