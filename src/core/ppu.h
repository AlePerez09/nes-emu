#pragma once
#include <array>
#include <cstdint>

class Cartridge;

// Ricoh 2C02 Picture Processing Unit.
// Reference: https://www.nesdev.org/wiki/PPU  (read "PPU rendering" and "PPU scrolling" before the pipeline)
//
// Timing: 341 dots per scanline, 262 scanlines per frame (0-239 visible, 240 idle,
// 241-260 vblank, 261 pre-render). The PPU runs 3 dots per CPU cycle.
class Ppu {
public:
    static constexpr int kWidth = 256;
    static constexpr int kHeight = 240;

    explicit Ppu(Cartridge& cart) : cart_(cart) {}

    void reset();
    void clock();  // advance one dot

    // CPU-facing registers $2000-$2007 (reg = addr & 7)
    uint8_t cpu_read(uint16_t reg);
    void cpu_write(uint16_t reg, uint8_t value);
    void oam_dma_write(uint8_t index, uint8_t value) { oam_[index] = value; }

    bool take_nmi() { bool n = nmi_pending_; nmi_pending_ = false; return n; }

    bool frame_complete = false;
    const std::array<uint32_t, kWidth * kHeight>& framebuffer() const { return framebuffer_; }

    // Debug view: draws both pattern tables (CHR) in greyscale into the top of the framebuffer.
    // Lets you confirm cartridge loading + PPU memory reads before rendering exists.
    void render_pattern_tables_debug();

    int scanline() const { return scanline_; }
    int dot() const { return dot_; }

private:
    Cartridge& cart_;

    // PPU address space ($0000-$3FFF): pattern tables (cartridge), nametables, palette.
    uint8_t vram_read(uint16_t addr);
    void vram_write(uint16_t addr, uint8_t value);
    uint16_t nametable_index(uint16_t addr) const;  // applies cartridge mirroring

    std::array<uint8_t, 0x800> nametables_{};  // 2 KB internal VRAM
    std::array<uint8_t, 32> palette_{};
    std::array<uint8_t, 256> oam_{};           // 64 sprites x 4 bytes

    uint8_t ctrl_ = 0;     // $2000
    uint8_t mask_ = 0;     // $2001
    uint8_t status_ = 0;   // $2002
    uint8_t oam_addr_ = 0; // $2003

    // "Loopy" internal registers - see NESdev wiki "PPU scrolling".
    uint16_t v_ = 0;        // current VRAM address (15 bits)
    uint16_t t_ = 0;        // temporary VRAM address / top-left of the screen
    uint8_t fine_x_ = 0;    // fine X scroll (3 bits)
    bool w_ = false;        // write toggle for $2005/$2006
    uint8_t read_buffer_ = 0;

    int scanline_ = 0;
    int dot_ = 0;
    bool nmi_pending_ = false;

    std::array<uint32_t, kWidth * kHeight> framebuffer_{};  // 0xAARRGGBB
};
