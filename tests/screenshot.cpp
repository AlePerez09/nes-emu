// Headless screenshot tool: runs a ROM for N frames with no window and saves the last frame
// as a .bmp (opens in any image viewer). Useful for checking rendering without a display,
// and later for image-comparison tests in CI.
//
// Usage: nes_screenshot game.nes out.bmp [frames=120] [--press-start-at=N]

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "core/nes.h"

namespace {
void put16(std::ofstream& f, uint16_t v) { f.put(char(v & 0xFF)); f.put(char(v >> 8)); }
void put32(std::ofstream& f, uint32_t v) { for (int i = 0; i < 4; ++i) f.put(char((v >> (8 * i)) & 0xFF)); }

bool write_bmp(const std::string& path, const Ppu& ppu) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t row = Ppu::kWidth * 3;  // 768 bytes, already a multiple of 4
    const uint32_t size = 54 + row * Ppu::kHeight;
    f.put('B'); f.put('M'); put32(f, size); put32(f, 0); put32(f, 54);
    put32(f, 40); put32(f, Ppu::kWidth); put32(f, Ppu::kHeight); put16(f, 1); put16(f, 24);
    put32(f, 0); put32(f, row * Ppu::kHeight); put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
    const auto& fb = ppu.framebuffer();
    for (int y = Ppu::kHeight - 1; y >= 0; --y) {  // BMP rows go bottom-up
        for (int x = 0; x < Ppu::kWidth; ++x) {
            uint32_t px = fb[y * Ppu::kWidth + x];
            f.put(char(px & 0xFF)); f.put(char((px >> 8) & 0xFF)); f.put(char((px >> 16) & 0xFF));
        }
    }
    return bool(f);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s game.nes out.bmp [frames] [--press-start-at=N]\n", argv[0]);
        return 2;
    }
    int frames = 120, press_start_at = -1;
    for (int i = 3; i < argc; ++i) {
        if (std::strncmp(argv[i], "--press-start-at=", 17) == 0) press_start_at = std::atoi(argv[i] + 17);
        else frames = std::atoi(argv[i]);
    }

    try {
        Nes nes(argv[1]);
        for (int i = 0; i < frames && !nes.cpu.halted(); ++i) {
            // Hold Start for a few frames if asked (many ROMs wait on a title screen).
            bool start = press_start_at >= 0 && i >= press_start_at && i < press_start_at + 5;
            nes.bus.set_controller(0, start ? 0x08 : 0x00);
            nes.run_frame();
        }
        if (nes.cpu.halted())
            std::fprintf(stderr, "CPU halted at $%04X: %s\n", nes.cpu.pc, nes.cpu.halt_reason().c_str());
        if (!write_bmp(argv[2], nes.ppu)) { std::fprintf(stderr, "could not write %s\n", argv[2]); return 1; }
        std::printf("wrote %s\n", argv[2]);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
