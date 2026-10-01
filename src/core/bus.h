#pragma once
#include <array>
#include <cstdint>

#include "core/cpu_bus.h"

class Cartridge;
class Ppu;

// The CPU's view of the world. CPU memory map: https://www.nesdev.org/wiki/CPU_memory_map
//   $0000-$07FF  2 KB internal RAM, mirrored through $1FFF
//   $2000-$2007  PPU registers, mirrored every 8 bytes through $3FFF
//   $4000-$4017  APU and I/O ($4014 = OAM DMA, $4016/$4017 = controllers)
//   $4020-$FFFF  Cartridge (via the mapper)
class Bus : public CpuBus {
public:
    Bus(Cartridge& cart, Ppu& ppu) : cart_(cart), ppu_(ppu) {}

    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t value) override;

    // Button bits: 0 A, 1 B, 2 Select, 3 Start, 4 Up, 5 Down, 6 Left, 7 Right
    void set_controller(int port, uint8_t buttons) { controller_state_[port & 1] = buttons; }

    // OAM DMA ($4014) stalls the CPU for 513/514 cycles. The Nes class adds these.
    uint32_t take_dma_stall() { uint32_t s = dma_stall_; dma_stall_ = 0; return s; }

private:
    Cartridge& cart_;
    Ppu& ppu_;
    std::array<uint8_t, 0x800> ram_{};

    uint8_t controller_state_[2]{};
    uint8_t controller_shift_[2]{};
    bool controller_strobe_ = false;

    uint32_t dma_stall_ = 0;
};
