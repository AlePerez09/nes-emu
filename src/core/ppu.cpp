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
    odd_frame_ = false;
    nmi_delay_ = false;
    bg_pattern_lo_ = bg_pattern_hi_ = bg_attrib_lo_ = bg_attrib_hi_ = 0;
    next_tile_ = next_attrib_ = next_pattern_lo_ = next_pattern_hi_ = 0;
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

bool Ppu::rendering_enabled() const { return (mask_ & 0x18) != 0; }  // background or sprites on

// ---- Scroll register updates ------------------------------------------------------------
// v holds the PPU's current position in the nametables, packed as  yyy NN YYYYY XXXXX:
//   XXXXX = coarse X (tile column 0-31)   YYYYY = coarse Y (tile row 0-29)
//   NN    = nametable select              yyy   = fine Y (pixel row within the tile, 0-7)
// During rendering the PPU walks v across and down the screen. When it runs off the edge of
// one nametable it flips the NN bit to continue into the neighbour, which is how scrolling
// across two screens works.

void Ppu::increment_coarse_x() {
    if ((v_ & 0x001F) == 31) {
        v_ &= uint16_t(~0x001F);  // coarse X = 0
        v_ ^= 0x0400;             // switch horizontal nametable
    } else {
        v_++;
    }
}

void Ppu::increment_y() {
    if ((v_ & 0x7000) != 0x7000) {
        v_ += 0x1000;             // next pixel row inside the same tile
        return;
    }
    v_ &= uint16_t(~0x7000);      // fine Y = 0, move to next tile row
    int coarse_y = (v_ & 0x03E0) >> 5;
    if (coarse_y == 29) {         // row 29 is the last row of tiles (30 rows x 8 = 240 pixels)
        coarse_y = 0;
        v_ ^= 0x0800;             // switch vertical nametable
    } else if (coarse_y == 31) {  // rows 30-31 are attribute data; a game scrolled there wraps without switching
        coarse_y = 0;
    } else {
        coarse_y++;
    }
    v_ = uint16_t((v_ & ~0x03E0) | (coarse_y << 5));
}

// At the end of each line, X position resets to the left edge stored in t.
void Ppu::copy_horizontal() { v_ = uint16_t((v_ & ~0x041F) | (t_ & 0x041F)); }
// At the start of each frame, Y position resets to the top stored in t.
void Ppu::copy_vertical()   { v_ = uint16_t((v_ & ~0x7BE0) | (t_ & 0x7BE0)); }

// ---- Background pipeline -------------------------------------------------------------------
// The PPU fetches one tile every 8 dots, always 2 tiles ahead of the pixel it is drawing.
// Fetched tiles go into 16-bit shift registers: the high byte is the tile being drawn now,
// the low byte is the next tile. Each dot shifts them left by one, and fine X picks which
// bit to read, which is what makes per-pixel horizontal scrolling possible.

void Ppu::load_background_shifters() {
    bg_pattern_lo_ = uint16_t((bg_pattern_lo_ & 0xFF00) | next_pattern_lo_);
    bg_pattern_hi_ = uint16_t((bg_pattern_hi_ & 0xFF00) | next_pattern_hi_);
    // The attribute (palette choice) is the same for all 8 pixels, so expand its 2 bits to 8.
    bg_attrib_lo_ = uint16_t((bg_attrib_lo_ & 0xFF00) | ((next_attrib_ & 0x01) ? 0xFF : 0x00));
    bg_attrib_hi_ = uint16_t((bg_attrib_hi_ & 0xFF00) | ((next_attrib_ & 0x02) ? 0xFF : 0x00));
}

void Ppu::shift_background() {
    bg_pattern_lo_ <<= 1;
    bg_pattern_hi_ <<= 1;
    bg_attrib_lo_ <<= 1;
    bg_attrib_hi_ <<= 1;
}

void Ppu::fetch_background(int step) {
    switch (step) {
        case 0:  // nametable byte: which tile to draw
            load_background_shifters();
            next_tile_ = vram_read(uint16_t(0x2000 | (v_ & 0x0FFF)));
            break;
        case 2: {  // attribute byte: one byte covers a 4x4-tile area, 2 bits per 2x2-tile quadrant
            uint16_t addr = uint16_t(0x23C0 | (v_ & 0x0C00) | ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07));
            uint8_t attrib = vram_read(addr);
            if (v_ & 0x0040) attrib >>= 4;  // bottom half of the 4x4 area (coarse Y bit 1)
            if (v_ & 0x0002) attrib >>= 2;  // right half (coarse X bit 1)
            next_attrib_ = attrib & 0x03;
            break;
        }
        case 4:  // pattern low bitplane: 16 bytes per tile, row = fine Y
            next_pattern_lo_ = vram_read(background_pattern_addr());
            break;
        case 6:  // pattern high bitplane, 8 bytes after the low plane
            next_pattern_hi_ = vram_read(uint16_t(background_pattern_addr() + 8));
            break;
        case 7:
            increment_coarse_x();
            break;
        default:
            break;
    }
}

uint16_t Ppu::background_pattern_addr() const {
    uint16_t table = (ctrl_ & 0x10) ? 0x1000 : 0x0000;  // PPUCTRL bit 4 picks the table
    return uint16_t(table + next_tile_ * 16 + ((v_ >> 12) & 0x07));
}

// ---- Pixel output ------------------------------------------------------------------------------

void Ppu::render_pixel() {
    const int x = dot_ - 1;
    uint8_t pixel = 0, palette = 0;

    const bool show_bg = mask_ & 0x08;
    const bool show_left_bg = mask_ & 0x02;  // games can hide the leftmost 8 pixels
    if (show_bg && (x >= 8 || show_left_bg)) {
        const uint16_t bit = uint16_t(0x8000 >> fine_x_);
        pixel = uint8_t(((bg_pattern_lo_ & bit) ? 1 : 0) | ((bg_pattern_hi_ & bit) ? 2 : 0));
        palette = uint8_t(((bg_attrib_lo_ & bit) ? 1 : 0) | ((bg_attrib_hi_ & bit) ? 2 : 0));
    }

    // TODO (Part 5): sprite pixels and priority get mixed in here.

    // Pixel value 0 is transparent and shows the universal background colour ($3F00).
    uint16_t color_addr = uint16_t(0x3F00 + (pixel ? (palette * 4 + pixel) : 0));
    // Hardware quirk: with rendering off, if v points into palette RAM the PPU outputs that
    // palette entry instead of the backdrop. Demos use it to show more colours on screen.
    if (!rendering_enabled() && (v_ & 0x3F00) == 0x3F00) color_addr = v_;
    uint8_t color = vram_read(color_addr);
    if (mask_ & 0x01) color &= 0x30;  // greyscale mode
    framebuffer_[scanline_ * kWidth + x] = argb(color);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

void Ppu::clock() {
    const bool visible_line = scanline_ < 240;
    const bool prerender_line = scanline_ == 261;
    const bool rendering = rendering_enabled();

    if (visible_line && dot_ >= 1 && dot_ <= 256) render_pixel();

    if (rendering && (visible_line || prerender_line)) {
        // Dots 2-257 fetch for this line; dots 321-336 prefetch the first two tiles of the next.
        if ((dot_ >= 2 && dot_ <= 257) || (dot_ >= 322 && dot_ <= 337)) {
            shift_background();
            fetch_background((dot_ - 1) % 8);
        }
        if (dot_ == 256) increment_y();
        if (dot_ == 257) copy_horizontal();
        if (prerender_line && dot_ >= 280 && dot_ <= 304) copy_vertical();
    }

    if (scanline_ == 241 && dot_ == 1) {
        status_ |= 0x80;                     // enter vblank
        if (ctrl_ & 0x80) nmi_pending_ = true;
    }
    if (prerender_line && dot_ == 1) {
        status_ &= uint8_t(~0xE0);           // clear vblank, sprite 0 hit, overflow
    }

    // Odd frames are one dot shorter when rendering is on (the pre-render line skips its last dot).
    // This keeps the NTSC picture's colour artifacts from crawling. Some test ROMs check it.
    if (prerender_line && dot_ == 339 && odd_frame_ && rendering) dot_ = 340;

    if (++dot_ > 340) {
        dot_ = 0;
        if (++scanline_ > 261) {
            scanline_ = 0;
            frame_complete = true;
            odd_frame_ = !odd_frame_;
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
            // The real CPU only notices it after finishing the instruction that follows the write.
            if (!(ctrl_ & 0x80) && (value & 0x80) && (status_ & 0x80)) {
                nmi_pending_ = true;
                nmi_delay_ = true;
            }
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
