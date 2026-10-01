#include "core/mappers/mapper_000.h"

uint8_t Mapper000::cpu_read(uint16_t addr) {
    if (addr >= 0x8000) {
        // 16 KB carts mirror: $C000-$FFFF reads the same bytes as $8000-$BFFF.
        uint16_t mask = prg_.size() > 0x4000 ? 0x7FFF : 0x3FFF;
        return prg_[(addr - 0x8000) & mask];
    }
    if (addr >= 0x6000) return prg_ram_[addr - 0x6000];
    return 0;  // open bus; good enough for now
}

void Mapper000::cpu_write(uint16_t addr, uint8_t value) {
    if (addr >= 0x6000 && addr < 0x8000) prg_ram_[addr - 0x6000] = value;
    // Writes to ROM are ignored on NROM.
}

uint8_t Mapper000::ppu_read(uint16_t addr) {
    return chr_[addr & 0x1FFF];
}

void Mapper000::ppu_write(uint16_t addr, uint8_t value) {
    if (chr_is_ram_) chr_[addr & 0x1FFF] = value;
}
