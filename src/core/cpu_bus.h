#pragma once
#include <cstdint>

// What the CPU needs from the outside world: somewhere to read and write bytes.
// The real NES Bus implements this, and so does the flat 64 KB memory the per-opcode
// tests use, so the CPU can be tested without any PPU/cartridge in the way.
class CpuBus {
public:
    virtual ~CpuBus() = default;
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;
};
