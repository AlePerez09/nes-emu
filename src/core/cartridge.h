#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "core/mappers/mapper.h"

// Loads an iNES (.nes) file and owns its PRG/CHR data plus the right Mapper.
class Cartridge {
public:
    explicit Cartridge(const std::string& path);  // throws std::runtime_error

    // The mapper holds references into prg_/chr_, so a Cartridge must never move.
    Cartridge(const Cartridge&) = delete;
    Cartridge& operator=(const Cartridge&) = delete;

    uint8_t cpu_read(uint16_t addr) { return mapper_->cpu_read(addr); }
    void cpu_write(uint16_t addr, uint8_t v) { mapper_->cpu_write(addr, v); }
    uint8_t ppu_read(uint16_t addr) { return mapper_->ppu_read(addr); }
    void ppu_write(uint16_t addr, uint8_t v) { mapper_->ppu_write(addr, v); }
    Mirroring mirroring() const { return mapper_->mirroring(); }

    uint8_t mapper_id() const { return mapper_id_; }
    size_t prg_size() const { return prg_.size(); }
    size_t chr_size() const { return chr_.size(); }

private:
    std::vector<uint8_t> prg_;
    std::vector<uint8_t> chr_;
    std::unique_ptr<Mapper> mapper_;
    uint8_t mapper_id_ = 0;
};
