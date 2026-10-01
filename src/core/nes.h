#pragma once
#include <string>

#include "core/bus.h"
#include "core/cartridge.h"
#include "core/cpu.h"
#include "core/ppu.h"

// Owns every component and keeps the CPU and PPU in step (3 PPU dots per CPU cycle).
// Member order matters: each component is constructed from the ones declared before it.
class Nes {
public:
    explicit Nes(const std::string& rom_path);

    void reset();
    void step();       // one CPU instruction, then catch the PPU up
    void run_frame();  // step until the PPU finishes a frame (or the CPU halts)

    Cartridge cart;
    Ppu ppu;
    Bus bus;
    Cpu cpu;
};
