#include "core/cpu.h"

#include <cstdio>


// ---------------------------------------------------------------------------
// Plumbing
// ---------------------------------------------------------------------------

uint8_t Cpu::read(uint16_t addr) { return bus_.read(addr); }
void Cpu::write(uint16_t addr, uint8_t value) { bus_.write(addr, value); }

uint16_t Cpu::read16(uint16_t addr) {
    return uint16_t(read(addr) | (read(uint16_t(addr + 1)) << 8));
}

void Cpu::set_zn(uint8_t value) {
    set_flag(Z, value == 0);
    set_flag(N, value & 0x80);
}

// The stack lives in page 1 ($0100-$01FF) and grows downward.
void Cpu::push(uint8_t value) { write(uint16_t(0x0100 | sp--), value); }
uint8_t Cpu::pop() { return read(uint16_t(0x0100 | ++sp)); }

void Cpu::reset() {
    a = x = y = 0;
    sp = 0xFD;
    p = 0x24;  // I and U set
    pc = read16(0xFFFC);
    cycles = 7;  // reset takes 7 cycles; nestest.log starts at CYC:7
    halted_ = false;
    halt_reason_.clear();
}

void Cpu::nmi() {
    push(uint8_t(pc >> 8));
    push(uint8_t(pc & 0xFF));
    push(uint8_t((p & ~B) | U));  // hardware interrupts push B clear
    set_flag(I, true);
    pc = read16(0xFFFA);
    cycles += 7;
}

void Cpu::irq() {
    if (get_flag(I)) return;
    push(uint8_t(pc >> 8));
    push(uint8_t(pc & 0xFF));
    push(uint8_t((p & ~B) | U));
    set_flag(I, true);
    pc = read16(0xFFFE);
    cycles += 7;
}

std::string Cpu::trace_state() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu",
                  pc, a, x, y, p, sp, static_cast<unsigned long long>(cycles));
    return buf;
}

uint16_t Cpu::todo(const char* what) {
    if (!halted_) {
        halted_ = true;
        halt_reason_ = what;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Addressing modes
// ---------------------------------------------------------------------------

uint16_t Cpu::addr_imm() { return pc++; }

uint16_t Cpu::addr_zp() { return read(pc++); }

uint16_t Cpu::addr_abs() {
    uint16_t addr = read16(pc);
    pc += 2;
    return addr;
}

// Zero page indexed: the sum wraps inside page zero, so $FF + 1 is $00, never $0100.
uint16_t Cpu::addr_zpx() { return uint8_t(read(pc++) + x); }
uint16_t Cpu::addr_zpy() { return uint8_t(read(pc++) + y); }

// Absolute indexed: a full 16-bit add. If the high byte changes, the real chip needs an
// extra cycle to fix it up; loads pay that only when it happens (see page_crossed_),
// while stores and read-modify-write instructions always pay it (baked into their cycle counts).
uint16_t Cpu::addr_abx() {
    uint16_t base = read16(pc);
    pc += 2;
    uint16_t addr = uint16_t(base + x);
    page_crossed_ = (base & 0xFF00) != (addr & 0xFF00);
    return addr;
}

uint16_t Cpu::addr_aby() {
    uint16_t base = read16(pc);
    pc += 2;
    uint16_t addr = uint16_t(base + y);
    page_crossed_ = (base & 0xFF00) != (addr & 0xFF00);
    return addr;
}

// Reads a 16-bit pointer stored in zero page. The high byte comes from (ptr + 1) & $FF,
// so a pointer at $FF takes its high byte from $00.
static inline uint16_t zp_pointer(uint8_t lo, uint8_t hi) { return uint16_t(lo | (hi << 8)); }

// (zp,X): add X to the zero-page operand first, then dereference.
uint16_t Cpu::addr_izx() {
    uint8_t ptr = uint8_t(read(pc++) + x);
    return zp_pointer(read(ptr), read(uint8_t(ptr + 1)));
}

// (zp),Y: dereference first, then add Y to the resulting 16-bit address.
uint16_t Cpu::addr_izy() {
    uint8_t ptr = read(pc++);
    uint16_t base = zp_pointer(read(ptr), read(uint8_t(ptr + 1)));
    uint16_t addr = uint16_t(base + y);
    page_crossed_ = (base & 0xFF00) != (addr & 0xFF00);
    return addr;
}

// (abs): JMP only. Hardware bug: the pointer's low byte wraps without carrying into the
// high byte, so JMP ($10FF) reads its target from $10FF and $1000, not $10FF and $1100.
uint16_t Cpu::addr_ind() {
    uint16_t ptr = read16(pc);
    pc += 2;
    uint16_t hi_addr = uint16_t((ptr & 0xFF00) | uint8_t(ptr + 1));
    return uint16_t(read(ptr) | (read(hi_addr) << 8));
}

// ---------------------------------------------------------------------------
// Instructions
// ---------------------------------------------------------------------------

// Loads set Z and N from the value loaded.
void Cpu::lda(uint16_t addr) { a = read(addr); set_zn(a); }
void Cpu::ldx(uint16_t addr) { x = read(addr); set_zn(x); }
void Cpu::ldy(uint16_t addr) { y = read(addr); set_zn(y); }

// Read-modify-write on memory. (The real chip writes the old value back once before the
// new one; that dummy write matters for some mapper registers, so remember it for later.)
void Cpu::inc(uint16_t addr) { uint8_t v = uint8_t(read(addr) + 1); write(addr, v); set_zn(v); }
void Cpu::dec(uint16_t addr) { uint8_t v = uint8_t(read(addr) - 1); write(addr, v); set_zn(v); }

void Cpu::step() {
    if (halted_) return;
    page_crossed_ = false;
    uint8_t opcode = read(pc++);
    execute(opcode);
}

// One case per opcode, grouped by instruction. Cycle counts come from the opcode table
// (masswerk.at's is excellent). A table-driven decoder {name, mode, fn, cycles} is a fine
// alternative once you see the patterns - either is a good design to talk through in interviews.
void Cpu::execute(uint8_t opcode) {
    // Cycle counts: base count from the opcode table, plus page_crossed_ for indexed loads.
    // The page-cross penalty only applies to reads; stores and INC/DEC always take the long path.
    switch (opcode) {
        // ---- Loads ----------------------------------------------------------------
        case 0xA9: lda(addr_imm()); cycles += 2; break;                  // LDA #imm
        case 0xA5: lda(addr_zp());  cycles += 3; break;                  // LDA zp
        case 0xB5: lda(addr_zpx()); cycles += 4; break;                  // LDA zp,X
        case 0xAD: lda(addr_abs()); cycles += 4; break;                  // LDA abs
        case 0xBD: lda(addr_abx()); cycles += 4 + page_crossed_; break;  // LDA abs,X
        case 0xB9: lda(addr_aby()); cycles += 4 + page_crossed_; break;  // LDA abs,Y
        case 0xA1: lda(addr_izx()); cycles += 6; break;                  // LDA (zp,X)
        case 0xB1: lda(addr_izy()); cycles += 5 + page_crossed_; break;  // LDA (zp),Y

        case 0xA2: ldx(addr_imm()); cycles += 2; break;                  // LDX #imm
        case 0xA6: ldx(addr_zp());  cycles += 3; break;                  // LDX zp
        case 0xB6: ldx(addr_zpy()); cycles += 4; break;                  // LDX zp,Y
        case 0xAE: ldx(addr_abs()); cycles += 4; break;                  // LDX abs
        case 0xBE: ldx(addr_aby()); cycles += 4 + page_crossed_; break;  // LDX abs,Y

        case 0xA0: ldy(addr_imm()); cycles += 2; break;                  // LDY #imm
        case 0xA4: ldy(addr_zp());  cycles += 3; break;                  // LDY zp
        case 0xB4: ldy(addr_zpx()); cycles += 4; break;                  // LDY zp,X
        case 0xAC: ldy(addr_abs()); cycles += 4; break;                  // LDY abs
        case 0xBC: ldy(addr_abx()); cycles += 4 + page_crossed_; break;  // LDY abs,X

        // ---- Stores (never affect flags, never get a page-cross discount) ----------
        case 0x85: write(addr_zp(),  a); cycles += 3; break;             // STA zp
        case 0x95: write(addr_zpx(), a); cycles += 4; break;             // STA zp,X
        case 0x8D: write(addr_abs(), a); cycles += 4; break;             // STA abs
        case 0x9D: write(addr_abx(), a); cycles += 5; break;             // STA abs,X
        case 0x99: write(addr_aby(), a); cycles += 5; break;             // STA abs,Y
        case 0x81: write(addr_izx(), a); cycles += 6; break;             // STA (zp,X)
        case 0x91: write(addr_izy(), a); cycles += 6; break;             // STA (zp),Y

        case 0x86: write(addr_zp(),  x); cycles += 3; break;             // STX zp
        case 0x96: write(addr_zpy(), x); cycles += 4; break;             // STX zp,Y
        case 0x8E: write(addr_abs(), x); cycles += 4; break;             // STX abs

        case 0x84: write(addr_zp(),  y); cycles += 3; break;             // STY zp
        case 0x94: write(addr_zpx(), y); cycles += 4; break;             // STY zp,X
        case 0x8C: write(addr_abs(), y); cycles += 4; break;             // STY abs

        // ---- Register transfers ------------------------------------------------------
        case 0xAA: x = a; set_zn(x); cycles += 2; break;                 // TAX
        case 0xA8: y = a; set_zn(y); cycles += 2; break;                 // TAY
        case 0x8A: a = x; set_zn(a); cycles += 2; break;                 // TXA
        case 0x98: a = y; set_zn(a); cycles += 2; break;                 // TYA
        case 0xBA: x = sp; set_zn(x); cycles += 2; break;                // TSX
        case 0x9A: sp = x; cycles += 2; break;                           // TXS (the one transfer that sets no flags)

        // ---- Flag instructions ---------------------------------------------------------
        case 0x18: set_flag(C, false); cycles += 2; break;               // CLC
        case 0x38: set_flag(C, true);  cycles += 2; break;               // SEC
        case 0x58: set_flag(I, false); cycles += 2; break;               // CLI
        case 0x78: set_flag(I, true);  cycles += 2; break;               // SEI
        case 0xD8: set_flag(D, false); cycles += 2; break;               // CLD
        case 0xF8: set_flag(D, true);  cycles += 2; break;               // SED
        case 0xB8: set_flag(V, false); cycles += 2; break;               // CLV (there is no SEV)

        // ---- Increments / decrements ------------------------------------------------------
        case 0xE8: x++; set_zn(x); cycles += 2; break;                   // INX
        case 0xC8: y++; set_zn(y); cycles += 2; break;                   // INY
        case 0xCA: x--; set_zn(x); cycles += 2; break;                   // DEX
        case 0x88: y--; set_zn(y); cycles += 2; break;                   // DEY

        case 0xE6: inc(addr_zp());  cycles += 5; break;                  // INC zp
        case 0xF6: inc(addr_zpx()); cycles += 6; break;                  // INC zp,X
        case 0xEE: inc(addr_abs()); cycles += 6; break;                  // INC abs
        case 0xFE: inc(addr_abx()); cycles += 7; break;                  // INC abs,X

        case 0xC6: dec(addr_zp());  cycles += 5; break;                  // DEC zp
        case 0xD6: dec(addr_zpx()); cycles += 6; break;                  // DEC zp,X
        case 0xCE: dec(addr_abs()); cycles += 6; break;                  // DEC abs
        case 0xDE: dec(addr_abx()); cycles += 7; break;                  // DEC abs,X

        // ---- Jumps / misc -------------------------------------------------------------------
        case 0x4C: pc = addr_abs(); cycles += 3; break;                  // JMP abs
        case 0x6C: pc = addr_ind(); cycles += 5; break;                  // JMP (ind)
        case 0xEA: cycles += 2; break;                                   // NOP

        // ---- Part 2 --------------------------------------------------------------------------
        // Stack:       PHA PHP PLA PLP   (PHP pushes with B and U set; PLP ignores B, keeps U)
        // Flow:        JSR RTS RTI BRK
        // Branches:    BCC BCS BEQ BNE BMI BPL BVC BVS (+1 cycle if taken, +1 more on page cross)
        // Arithmetic:  ADC SBC (overflow flag: see "The 6502 overflow flag explained")
        // Compare:     CMP CPX CPY
        // Logic:       AND ORA EOR BIT
        // Shifts:      ASL LSR ROL ROR (accumulator + memory forms)
        // ---- Part 3: unofficial opcodes (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, NOPs) ----------

        default:
            pc--;  // leave pc on the offending opcode so traces point at it
            char msg[48];
            std::snprintf(msg, sizeof msg, "opcode $%02X not implemented", opcode);
            todo(msg);
            break;
    }
}
