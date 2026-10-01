#include "core/bus.h"

#include "core/cartridge.h"
#include "core/ppu.h"

uint8_t Bus::read(uint16_t addr) {
    if (addr < 0x2000) return ram_[addr & 0x07FF];
    if (addr < 0x4000) return ppu_.cpu_read(addr & 0x0007);

    if (addr == 0x4016 || addr == 0x4017) {
        int port = addr & 1;
        uint8_t bit;
        if (controller_strobe_) {
            bit = controller_state_[port] & 1;  // while strobing, always reports A
        } else {
            bit = controller_shift_[port] & 1;
            controller_shift_[port] = uint8_t((controller_shift_[port] >> 1) | 0x80);  // 1s after 8 reads
        }
        return uint8_t(0x40 | bit);  // upper bits are open bus; $40 is what most games see
    }

    if (addr < 0x4020) return 0;  // TODO: APU status ($4015)

    return cart_.cpu_read(addr);
}

void Bus::write(uint16_t addr, uint8_t value) {
    if (addr < 0x2000) { ram_[addr & 0x07FF] = value; return; }
    if (addr < 0x4000) { ppu_.cpu_write(addr & 0x0007, value); return; }

    if (addr == 0x4014) {
        // OAM DMA: copy page $XX00-$XXFF into sprite memory.
        uint16_t base = uint16_t(value) << 8;
        for (int i = 0; i < 256; ++i) ppu_.oam_dma_write(uint8_t(i), read(uint16_t(base + i)));
        dma_stall_ += 513;  // +1 on odd CPU cycles; refine later if a test ROM cares
        return;
    }

    if (addr == 0x4016) {
        controller_strobe_ = value & 1;
        if (controller_strobe_) {
            controller_shift_[0] = controller_state_[0];
            controller_shift_[1] = controller_state_[1];
        }
        return;
    }

    if (addr < 0x4020) return;  // TODO: APU registers

    cart_.cpu_write(addr, value);
}
