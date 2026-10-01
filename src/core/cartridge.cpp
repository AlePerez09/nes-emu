#include "core/cartridge.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "core/mappers/mapper_000.h"

// iNES header layout: https://www.nesdev.org/wiki/INES
//   0-3  "NES\x1A"
//   4    PRG ROM size in 16 KB units
//   5    CHR ROM size in 8 KB units (0 = board uses 8 KB CHR RAM)
//   6    flags: bit0 mirroring (1 = vertical), bit1 battery, bit2 trainer, bit3 four-screen,
//               bits4-7 low nibble of mapper number
//   7    flags: bits4-7 high nibble of mapper number (bits 2-3 == 0b10 means NES 2.0)
Cartridge::Cartridge(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open ROM: " + path);

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() < 16 || data[0] != 'N' || data[1] != 'E' || data[2] != 'S' || data[3] != 0x1A)
        throw std::runtime_error("Not an iNES file: " + path);

    const size_t prg_size = size_t(data[4]) * 0x4000;
    const size_t chr_size = size_t(data[5]) * 0x2000;
    const uint8_t flags6 = data[6];
    const uint8_t flags7 = data[7];
    mapper_id_ = uint8_t((flags7 & 0xF0) | (flags6 >> 4));

    Mirroring mirroring = (flags6 & 0x01) ? Mirroring::Vertical : Mirroring::Horizontal;
    if (flags6 & 0x08) mirroring = Mirroring::FourScreen;

    size_t offset = 16;
    if (flags6 & 0x04) offset += 512;  // skip trainer

    if (data.size() < offset + prg_size + chr_size)
        throw std::runtime_error("ROM file is truncated: " + path);

    prg_.assign(data.begin() + offset, data.begin() + offset + prg_size);
    offset += prg_size;

    const bool chr_is_ram = (chr_size == 0);
    if (chr_is_ram) chr_.assign(0x2000, 0);
    else chr_.assign(data.begin() + offset, data.begin() + offset + chr_size);

    switch (mapper_id_) {
        case 0: mapper_ = std::make_unique<Mapper000>(prg_, chr_, chr_is_ram, mirroring); break;
        // TODO: 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3) - together ~80% of the library.
        default:
            throw std::runtime_error("Mapper " + std::to_string(mapper_id_) + " not implemented yet");
    }
}
