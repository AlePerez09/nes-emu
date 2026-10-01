#pragma once
#include <cstdint>
#include <string>

#include "core/cpu_bus.h"

// Ricoh 2A03 = MOS 6502 core without decimal mode.
// Reference: https://www.nesdev.org/wiki/CPU  and  https://www.masswerk.at/6502/6502_instruction_set.html
//
// This is an instruction-stepped interpreter: step() runs one whole instruction and
// adds its cycle count. That's accurate enough for most games; cycle-stepping can come later.
class Cpu {
public:
    enum Flag : uint8_t {
        C = 1 << 0,  // carry
        Z = 1 << 1,  // zero
        I = 1 << 2,  // interrupt disable
        D = 1 << 3,  // decimal (flag exists, but the 2A03 ignores it in ADC/SBC)
        B = 1 << 4,  // "break" - only exists in the copy pushed to the stack
        U = 1 << 5,  // unused, always reads as 1
        V = 1 << 6,  // overflow
        N = 1 << 7,  // negative
    };

    explicit Cpu(CpuBus& bus) : bus_(bus) {}

    void reset();
    void step();  // execute one instruction
    void nmi();
    void irq();

    // Registers are public so the test harness and debugger can inspect/poke them.
    uint8_t a = 0, x = 0, y = 0;
    uint8_t sp = 0xFD;
    uint8_t p = 0x24;
    uint16_t pc = 0;
    uint64_t cycles = 0;

    // "C000 A:00 X:00 Y:00 P:24 SP:FD CYC:7" - matches the register columns of nestest.log
    std::string trace_state() const;

    bool halted() const { return halted_; }
    const std::string& halt_reason() const { return halt_reason_; }

private:
    CpuBus& bus_;
    bool halted_ = false;
    std::string halt_reason_;
    bool page_crossed_ = false;  // set by indexed addressing modes; some opcodes add a cycle

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t value);
    uint16_t read16(uint16_t addr);

    bool get_flag(Flag f) const { return (p & f) != 0; }
    void set_flag(Flag f, bool on) { p = on ? uint8_t(p | f) : uint8_t(p & ~f); }
    void set_zn(uint8_t value);

    void push(uint8_t value);
    uint8_t pop();

    // Addressing modes: each consumes its operand bytes from pc and returns the effective address.
    uint16_t addr_imm();  // #$nn
    uint16_t addr_zp();   // $nn
    uint16_t addr_zpx();  // $nn,X   (wraps within zero page!)
    uint16_t addr_zpy();  // $nn,Y
    uint16_t addr_abs();  // $nnnn
    uint16_t addr_abx();  // $nnnn,X (sets page_crossed_)
    uint16_t addr_aby();  // $nnnn,Y (sets page_crossed_)
    uint16_t addr_izx();  // ($nn,X)
    uint16_t addr_izy();  // ($nn),Y (sets page_crossed_)
    uint16_t addr_ind();  // ($nnnn) - JMP only, has the famous page-wrap bug

    void execute(uint8_t opcode);
    uint16_t todo(const char* what);  // marks unimplemented work and halts cleanly

    // Instruction helpers (add more as you go: adc, sbc, cmp, asl, branch, ...)
    void lda(uint16_t addr);
    void ldx(uint16_t addr);
    void ldy(uint16_t addr);
    void inc(uint16_t addr);
    void dec(uint16_t addr);
};
