#pragma once
#include <array>
#include "core/mappers/mapper.h"

// Mapper 0: NROM. No bank switching.
// PRG: 16 KB (mirrored into $8000 and $C000) or 32 KB. CHR: 8 KB ROM (or RAM).
// Games: Donkey Kong, Super Mario Bros., Balloon Fight, Ice Climber.
class Mapper000 : public Mapper {
public:
    using Mapper::Mapper;

    uint8_t cpu_read(uint16_t addr) override;
    void cpu_write(uint16_t addr, uint8_t value) override;
    uint8_t ppu_read(uint16_t addr) override;
    void ppu_write(uint16_t addr, uint8_t value) override;

private:
    std::array<uint8_t, 0x2000> prg_ram_{};  // $6000-$7FFF (Family BASIC); harmless elsewhere
};
