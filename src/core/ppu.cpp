#include "core/ppu.h"

#include "core/cartridge.h"

namespace {
// Standard 2C02 palette (RGB). Index with a 6-bit color from palette RAM.
constexpr uint32_t kSystemPalette[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
    0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
    0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
    0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
    0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000,
};

inline uint32_t argb(uint8_t palette_index) { return 0xFF000000u | kSystemPalette[palette_index & 0x3F]; }
}  // namespace

void Ppu::reset() {
    ctrl_ = mask_ = status_ = oam_addr_ = 0;
    v_ = t_ = 0;
    fine_x_ = 0;
    w_ = false;
    read_buffer_ = 0;
    scanline_ = dot_ = 0;
    nmi_pending_ = false;
    frame_complete = false;
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

void Ppu::clock() {
    // TODO (the big one): the rendering pipeline for scanlines 0-239 and 261.
    //   Background: fetch nametable byte, attribute byte, pattern low/high every 8 dots,
    //   feed 16-bit shift registers, increment coarse X at dots 8,16..256, increment Y at 256,
    //   copy horizontal bits t->v at 257, copy vertical bits t->v at dots 280-304 of line 261.
    //   Sprites: evaluate up to 8 per line, sprite 0 hit, sprite overflow.
    //   Start with a simpler per-scanline renderer if you like, then refine.

    if (scanline_ == 241 && dot_ == 1) {
        status_ |= 0x80;                     // enter vblank
        if (ctrl_ & 0x80) nmi_pending_ = true;
    }
    if (scanline_ == 261 && dot_ == 1) {
        status_ &= uint8_t(~0xE0);           // clear vblank, sprite 0 hit, overflow
    }

    if (++dot_ > 340) {
        dot_ = 0;
        if (++scanline_ > 261) {
            scanline_ = 0;
            frame_complete = true;
            // TODO: odd frames skip one dot on the pre-render line when rendering is enabled.
        }
    }
}

// ---------------------------------------------------------------------------
// CPU-facing registers
// ---------------------------------------------------------------------------

uint8_t Ppu::cpu_read(uint16_t reg) {
    switch (reg) {
        case 2: {  // PPUSTATUS
            uint8_t result = uint8_t((status_ & 0xE0) | (read_buffer_ & 0x1F));
            status_ &= uint8_t(~0x80);
            w_ = false;
            return result;
        }
        case 4:  // OAMDATA
            return oam_[oam_addr_];
        case 7: {  // PPUDATA - reads are buffered, except palette reads
            uint8_t result = read_buffer_;
            read_buffer_ = vram_read(v_);
            if ((v_ & 0x3FFF) >= 0x3F00) result = read_buffer_;
            v_ = uint16_t(v_ + ((ctrl_ & 0x04) ? 32 : 1));
            return result;
        }
        default:
            return 0;  // write-only registers read as open bus
    }
}

void Ppu::cpu_write(uint16_t reg, uint8_t value) {
    switch (reg) {
        case 0:  // PPUCTRL
            // Enabling NMI during vblank triggers one immediately.
            if (!(ctrl_ & 0x80) && (value & 0x80) && (status_ & 0x80)) nmi_pending_ = true;
            ctrl_ = value;
            t_ = uint16_t((t_ & 0xF3FF) | ((value & 0x03) << 10));  // nametable select
            break;
        case 1: mask_ = value; break;
        case 3: oam_addr_ = value; break;
        case 4: oam_[oam_addr_++] = value; break;
        case 5:  // PPUSCROLL
            if (!w_) {
                fine_x_ = value & 0x07;
                t_ = uint16_t((t_ & 0xFFE0) | (value >> 3));
            } else {
                t_ = uint16_t((t_ & 0x8C1F) | ((value & 0x07) << 12) | ((value & 0xF8) << 2));
            }
            w_ = !w_;
            break;
        case 6:  // PPUADDR
            if (!w_) {
                t_ = uint16_t((t_ & 0x00FF) | ((value & 0x3F) << 8));
            } else {
                t_ = uint16_t((t_ & 0xFF00) | value);
                v_ = t_;
            }
            w_ = !w_;
            break;
        case 7:  // PPUDATA
            vram_write(v_, value);
            v_ = uint16_t(v_ + ((ctrl_ & 0x04) ? 32 : 1));
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// PPU address space
// ---------------------------------------------------------------------------

uint16_t Ppu::nametable_index(uint16_t addr) const {
    uint16_t offset = (addr - 0x2000) & 0x0FFF;  // $2000-$2FFF, with $3000-$3EFF mirrored down
    uint16_t table = offset / 0x400;              // which logical nametable 0-3
    uint16_t inner = offset % 0x400;
    switch (cart_.mirroring()) {
        case Mirroring::Vertical:         return uint16_t((table & 1) * 0x400 + inner);  // 0,1,0,1
        case Mirroring::Horizontal:       return uint16_t((table >> 1) * 0x400 + inner); // 0,0,1,1
        case Mirroring::SingleScreenLow:  return inner;
        case Mirroring::SingleScreenHigh: return uint16_t(0x400 + inner);
        case Mirroring::FourScreen:       return uint16_t((table & 1) * 0x400 + inner);  // TODO: needs extra cart VRAM
    }
    return inner;
}

uint8_t Ppu::vram_read(uint16_t addr) {
    addr &= 0x3FFF;
    if (addr < 0x2000) return cart_.ppu_read(addr);
    if (addr < 0x3F00) return nametables_[nametable_index(addr)];
    uint16_t p = addr & 0x1F;
    if ((p & 0x13) == 0x10) p &= 0x0F;  // $3F10/$14/$18/$1C mirror $3F00/$04/$08/$0C
    return palette_[p];
}

void Ppu::vram_write(uint16_t addr, uint8_t value) {
    addr &= 0x3FFF;
    if (addr < 0x2000) { cart_.ppu_write(addr, value); return; }
    if (addr < 0x3F00) { nametables_[nametable_index(addr)] = value; return; }
    uint16_t p = addr & 0x1F;
    if ((p & 0x13) == 0x10) p &= 0x0F;
    palette_[p] = value & 0x3F;
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

void Ppu::render_pattern_tables_debug() {
    // Greys from the system palette: black, dark grey, light grey, white.
    const uint32_t shades[4] = {argb(0x0F), argb(0x00), argb(0x10), argb(0x30)};
    framebuffer_.fill(0xFF000000u);

    for (int table = 0; table < 2; ++table) {
        for (int tile = 0; tile < 256; ++tile) {
            int tx = tile % 16, ty = tile / 16;
            for (int row = 0; row < 8; ++row) {
                uint16_t base = uint16_t(table * 0x1000 + tile * 16 + row);
                uint8_t lo = vram_read(base);
                uint8_t hi = vram_read(uint16_t(base + 8));
                for (int col = 0; col < 8; ++col) {
                    int bit = 7 - col;
                    int pixel = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                    int x = table * 128 + tx * 8 + col;
                    int y = ty * 8 + row;
                    framebuffer_[y * kWidth + x] = shades[pixel];
                }
            }
        }
    }
}
