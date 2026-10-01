#pragma once
#include <cstdint>
#include <vector>

enum class Mirroring { Horizontal, Vertical, SingleScreenLow, SingleScreenHigh, FourScreen };

// A mapper decides how the CPU and PPU address spaces map onto cartridge ROM/RAM.
// Each board type (NROM, MMC1, UxROM, CNROM, MMC3...) is one subclass.
//
// CPU side: $4020-$FFFF (in practice $6000-$FFFF)
// PPU side: $0000-$1FFF (pattern tables / CHR)
class Mapper {
public:
    Mapper(std::vector<uint8_t>& prg, std::vector<uint8_t>& chr, bool chr_is_ram, Mirroring m)
        : prg_(prg), chr_(chr), chr_is_ram_(chr_is_ram), mirroring_(m) {}
    virtual ~Mapper() = default;

    virtual uint8_t cpu_read(uint16_t addr) = 0;
    virtual void cpu_write(uint16_t addr, uint8_t value) = 0;
    virtual uint8_t ppu_read(uint16_t addr) = 0;
    virtual void ppu_write(uint16_t addr, uint8_t value) = 0;

    // Some mappers (MMC1, MMC3) change mirroring at runtime, so this is virtual.
    virtual Mirroring mirroring() const { return mirroring_; }

    // MMC3 raises IRQs from its scanline counter. Override when you get there.
    virtual bool irq_pending() const { return false; }

protected:
    std::vector<uint8_t>& prg_;
    std::vector<uint8_t>& chr_;
    bool chr_is_ram_;
    Mirroring mirroring_;
};
