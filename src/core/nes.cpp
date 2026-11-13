#include "core/nes.h"

Nes::Nes(const std::string& rom_path) : cart(rom_path), ppu(cart), bus(cart, ppu), cpu(bus) {
    reset();
}

void Nes::reset() {
    ppu.reset();
    cpu.reset();
}

void Nes::step() {
    const uint64_t before = cpu.cycles;

    // An NMI raised by the PPU during the previous instruction is handled before the next one.
    // Handling it costs 7 CPU cycles, so it goes through the same PPU catch-up below; skipping
    // that made every frame 21 PPU dots too long (caught by blargg's branch_timing test 1).
    if (ppu.take_nmi()) cpu.nmi();
    else cpu.step();
    cpu.cycles += bus.take_dma_stall();

    // Catch the PPU up. Being instruction-granular (not cycle-granular) is a known
    // simplification; a few timing-sensitive games and test ROMs will want finer sync later.
    const uint64_t elapsed = cpu.cycles - before;
    for (uint64_t i = 0; i < elapsed * 3; ++i) {
        ppu.clock();
    }
}

void Nes::run_frame() {
    ppu.frame_complete = false;
    while (!ppu.frame_complete && !cpu.halted()) step();
}
