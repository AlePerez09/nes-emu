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

// ---- Part 2 helpers -----------------------------------------------------------

// ADC: A = A + M + C.
//  C is set when the unsigned result doesn't fit in 8 bits (> 255).
//  V is set when the *signed* result is wrong: two inputs with the same sign produced
//  a result with the other sign (e.g. 100 + 100 = -56). The bit trick below checks exactly that:
//  ~(a ^ m) has bit 7 set if a and m share a sign; (a ^ sum) has bit 7 set if the sign changed.
void Cpu::add_with_carry(uint8_t m) {
    uint16_t sum = uint16_t(a + m + (get_flag(C) ? 1 : 0));
    set_flag(C, sum > 0xFF);
    set_flag(V, (~(a ^ m) & (a ^ sum) & 0x80) != 0);
    a = uint8_t(sum);
    set_zn(a);
}

void Cpu::adc(uint16_t addr) { add_with_carry(read(addr)); }

// SBC is ADC with the operand's bits inverted: A - M - (1 - C) == A + ~M + C.
// That's why the 6502 carry flag means "no borrow" during subtraction.
void Cpu::sbc(uint16_t addr) { add_with_carry(uint8_t(~read(addr))); }

// CMP/CPX/CPY: subtract without storing. C = reg >= M, Z = equal, N = bit 7 of the difference.
void Cpu::compare(uint8_t reg, uint16_t addr) {
    uint8_t m = read(addr);
    set_flag(C, reg >= m);
    set_zn(uint8_t(reg - m));
}

void Cpu::and_(uint16_t addr) { a &= read(addr); set_zn(a); }
void Cpu::ora(uint16_t addr)  { a |= read(addr); set_zn(a); }
void Cpu::eor(uint16_t addr)  { a ^= read(addr); set_zn(a); }

// BIT: Z comes from A & M, but N and V are copied straight from bits 7 and 6 of M.
// Games use it to test a hardware status bit without touching A.
void Cpu::bit(uint16_t addr) {
    uint8_t m = read(addr);
    set_flag(Z, (a & m) == 0);
    set_flag(N, m & 0x80);
    set_flag(V, m & 0x40);
}

// Shifts and rotates work on a value and return the result, so the same code serves
// the accumulator form (ASL A) and the memory form (ASL $10). The bit shifted out goes to C.
uint8_t Cpu::asl(uint8_t v) { set_flag(C, v & 0x80); v = uint8_t(v << 1); set_zn(v); return v; }
uint8_t Cpu::lsr(uint8_t v) { set_flag(C, v & 0x01); v = uint8_t(v >> 1); set_zn(v); return v; }
uint8_t Cpu::rol(uint8_t v) {
    bool old_c = get_flag(C);
    set_flag(C, v & 0x80);
    v = uint8_t((v << 1) | (old_c ? 0x01 : 0));
    set_zn(v);
    return v;
}
uint8_t Cpu::ror(uint8_t v) {
    bool old_c = get_flag(C);
    set_flag(C, v & 0x01);
    v = uint8_t((v >> 1) | (old_c ? 0x80 : 0));
    set_zn(v);
    return v;
}

// Memory form: read, modify, write back. The address is computed once.
void Cpu::modify(uint16_t addr, uint8_t (Cpu::*op)(uint8_t)) {
    write(addr, (this->*op)(read(addr)));
}

// Branches: signed 8-bit offset relative to the *next* instruction.
// 2 cycles if not taken, 3 if taken, 4 if taken into a different page.
void Cpu::branch(bool condition) {
    int8_t offset = int8_t(read(pc++));
    cycles += 2;
    if (!condition) return;
    uint16_t target = uint16_t(pc + offset);
    cycles += ((pc & 0xFF00) != (target & 0xFF00)) ? 2 : 1;
    pc = target;
}

// Status byte as pushed by PHP/BRK: the B and U bits are forced to 1.
// (NMI/IRQ push with B = 0; that's the only way software can tell them apart.)
uint8_t Cpu::status_for_push() const { return uint8_t(p | B | U); }

// Status byte pulled by PLP/RTI: B doesn't exist in the real register, U always reads 1.
void Cpu::set_status_from_pull(uint8_t v) { p = uint8_t((v & ~B) | U); }

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

        // ==== Part 2 ==================================================================

        // ---- Stack -------------------------------------------------------------------
        case 0x48: push(a); cycles += 3; break;                               // PHA
        case 0x08: push(status_for_push()); cycles += 3; break;               // PHP
        case 0x68: a = pop(); set_zn(a); cycles += 4; break;                  // PLA
        case 0x28: set_status_from_pull(pop()); cycles += 4; break;           // PLP

        // ---- Subroutines and interrupts ------------------------------------------------
        case 0x20: {                                                          // JSR abs
            uint16_t target = addr_abs();
            uint16_t ret = uint16_t(pc - 1);  // pushes the address of JSR's LAST byte, not the next instruction
            push(uint8_t(ret >> 8));
            push(uint8_t(ret & 0xFF));
            pc = target;
            cycles += 6;
            break;
        }
        case 0x60: {                                                          // RTS
            uint8_t lo = pop();
            uint8_t hi = pop();
            pc = uint16_t(((hi << 8) | lo) + 1);  // ...which is why RTS adds 1
            cycles += 6;
            break;
        }
        case 0x40: {                                                          // RTI
            set_status_from_pull(pop());
            uint8_t lo = pop();
            uint8_t hi = pop();
            pc = uint16_t((hi << 8) | lo);        // no +1: interrupts push the exact return address
            cycles += 6;
            break;
        }
        case 0x00: {                                                          // BRK
            pc++;  // BRK has a padding byte, so the return address skips it
            push(uint8_t(pc >> 8));
            push(uint8_t(pc & 0xFF));
            push(status_for_push());
            set_flag(I, true);
            pc = read16(0xFFFE);
            cycles += 7;
            break;
        }

        // ---- Branches --------------------------------------------------------------------
        case 0x10: branch(!get_flag(N)); break;                               // BPL
        case 0x30: branch(get_flag(N));  break;                               // BMI
        case 0x50: branch(!get_flag(V)); break;                               // BVC
        case 0x70: branch(get_flag(V));  break;                               // BVS
        case 0x90: branch(!get_flag(C)); break;                               // BCC
        case 0xB0: branch(get_flag(C));  break;                               // BCS
        case 0xD0: branch(!get_flag(Z)); break;                               // BNE
        case 0xF0: branch(get_flag(Z));  break;                               // BEQ

        // ---- ADC / SBC ---------------------------------------------------------------------
        case 0x69: adc(addr_imm()); cycles += 2; break;                       // ADC #imm
        case 0x65: adc(addr_zp());  cycles += 3; break;                       // ADC zp
        case 0x75: adc(addr_zpx()); cycles += 4; break;                       // ADC zp,X
        case 0x6D: adc(addr_abs()); cycles += 4; break;                       // ADC abs
        case 0x7D: adc(addr_abx()); cycles += 4 + page_crossed_; break;       // ADC abs,X
        case 0x79: adc(addr_aby()); cycles += 4 + page_crossed_; break;       // ADC abs,Y
        case 0x61: adc(addr_izx()); cycles += 6; break;                       // ADC (zp,X)
        case 0x71: adc(addr_izy()); cycles += 5 + page_crossed_; break;       // ADC (zp),Y

        case 0xE9: sbc(addr_imm()); cycles += 2; break;                       // SBC #imm
        case 0xE5: sbc(addr_zp());  cycles += 3; break;                       // SBC zp
        case 0xF5: sbc(addr_zpx()); cycles += 4; break;                       // SBC zp,X
        case 0xED: sbc(addr_abs()); cycles += 4; break;                       // SBC abs
        case 0xFD: sbc(addr_abx()); cycles += 4 + page_crossed_; break;       // SBC abs,X
        case 0xF9: sbc(addr_aby()); cycles += 4 + page_crossed_; break;       // SBC abs,Y
        case 0xE1: sbc(addr_izx()); cycles += 6; break;                       // SBC (zp,X)
        case 0xF1: sbc(addr_izy()); cycles += 5 + page_crossed_; break;       // SBC (zp),Y

        // ---- Compares ----------------------------------------------------------------------
        case 0xC9: compare(a, addr_imm()); cycles += 2; break;                // CMP #imm
        case 0xC5: compare(a, addr_zp());  cycles += 3; break;                // CMP zp
        case 0xD5: compare(a, addr_zpx()); cycles += 4; break;                // CMP zp,X
        case 0xCD: compare(a, addr_abs()); cycles += 4; break;                // CMP abs
        case 0xDD: compare(a, addr_abx()); cycles += 4 + page_crossed_; break;// CMP abs,X
        case 0xD9: compare(a, addr_aby()); cycles += 4 + page_crossed_; break;// CMP abs,Y
        case 0xC1: compare(a, addr_izx()); cycles += 6; break;                // CMP (zp,X)
        case 0xD1: compare(a, addr_izy()); cycles += 5 + page_crossed_; break;// CMP (zp),Y

        case 0xE0: compare(x, addr_imm()); cycles += 2; break;                // CPX #imm
        case 0xE4: compare(x, addr_zp());  cycles += 3; break;                // CPX zp
        case 0xEC: compare(x, addr_abs()); cycles += 4; break;                // CPX abs

        case 0xC0: compare(y, addr_imm()); cycles += 2; break;                // CPY #imm
        case 0xC4: compare(y, addr_zp());  cycles += 3; break;                // CPY zp
        case 0xCC: compare(y, addr_abs()); cycles += 4; break;                // CPY abs

        // ---- Logic -------------------------------------------------------------------------
        case 0x29: and_(addr_imm()); cycles += 2; break;                      // AND #imm
        case 0x25: and_(addr_zp());  cycles += 3; break;                      // AND zp
        case 0x35: and_(addr_zpx()); cycles += 4; break;                      // AND zp,X
        case 0x2D: and_(addr_abs()); cycles += 4; break;                      // AND abs
        case 0x3D: and_(addr_abx()); cycles += 4 + page_crossed_; break;      // AND abs,X
        case 0x39: and_(addr_aby()); cycles += 4 + page_crossed_; break;      // AND abs,Y
        case 0x21: and_(addr_izx()); cycles += 6; break;                      // AND (zp,X)
        case 0x31: and_(addr_izy()); cycles += 5 + page_crossed_; break;      // AND (zp),Y

        case 0x09: ora(addr_imm()); cycles += 2; break;                       // ORA #imm
        case 0x05: ora(addr_zp());  cycles += 3; break;                       // ORA zp
        case 0x15: ora(addr_zpx()); cycles += 4; break;                       // ORA zp,X
        case 0x0D: ora(addr_abs()); cycles += 4; break;                       // ORA abs
        case 0x1D: ora(addr_abx()); cycles += 4 + page_crossed_; break;       // ORA abs,X
        case 0x19: ora(addr_aby()); cycles += 4 + page_crossed_; break;       // ORA abs,Y
        case 0x01: ora(addr_izx()); cycles += 6; break;                       // ORA (zp,X)
        case 0x11: ora(addr_izy()); cycles += 5 + page_crossed_; break;       // ORA (zp),Y

        case 0x49: eor(addr_imm()); cycles += 2; break;                       // EOR #imm
        case 0x45: eor(addr_zp());  cycles += 3; break;                       // EOR zp
        case 0x55: eor(addr_zpx()); cycles += 4; break;                       // EOR zp,X
        case 0x4D: eor(addr_abs()); cycles += 4; break;                       // EOR abs
        case 0x5D: eor(addr_abx()); cycles += 4 + page_crossed_; break;       // EOR abs,X
        case 0x59: eor(addr_aby()); cycles += 4 + page_crossed_; break;       // EOR abs,Y
        case 0x41: eor(addr_izx()); cycles += 6; break;                       // EOR (zp,X)
        case 0x51: eor(addr_izy()); cycles += 5 + page_crossed_; break;       // EOR (zp),Y

        case 0x24: bit(addr_zp());  cycles += 3; break;                       // BIT zp
        case 0x2C: bit(addr_abs()); cycles += 4; break;                       // BIT abs

        // ---- Shifts and rotates (accumulator form, then memory forms) ----------------------
        case 0x0A: a = asl(a); cycles += 2; break;                            // ASL A
        case 0x06: modify(addr_zp(),  &Cpu::asl); cycles += 5; break;         // ASL zp
        case 0x16: modify(addr_zpx(), &Cpu::asl); cycles += 6; break;         // ASL zp,X
        case 0x0E: modify(addr_abs(), &Cpu::asl); cycles += 6; break;         // ASL abs
        case 0x1E: modify(addr_abx(), &Cpu::asl); cycles += 7; break;         // ASL abs,X

        case 0x4A: a = lsr(a); cycles += 2; break;                            // LSR A
        case 0x46: modify(addr_zp(),  &Cpu::lsr); cycles += 5; break;         // LSR zp
        case 0x56: modify(addr_zpx(), &Cpu::lsr); cycles += 6; break;         // LSR zp,X
        case 0x4E: modify(addr_abs(), &Cpu::lsr); cycles += 6; break;         // LSR abs
        case 0x5E: modify(addr_abx(), &Cpu::lsr); cycles += 7; break;         // LSR abs,X

        case 0x2A: a = rol(a); cycles += 2; break;                            // ROL A
        case 0x26: modify(addr_zp(),  &Cpu::rol); cycles += 5; break;         // ROL zp
        case 0x36: modify(addr_zpx(), &Cpu::rol); cycles += 6; break;         // ROL zp,X
        case 0x2E: modify(addr_abs(), &Cpu::rol); cycles += 6; break;         // ROL abs
        case 0x3E: modify(addr_abx(), &Cpu::rol); cycles += 7; break;         // ROL abs,X

        case 0x6A: a = ror(a); cycles += 2; break;                            // ROR A
        case 0x66: modify(addr_zp(),  &Cpu::ror); cycles += 5; break;         // ROR zp
        case 0x76: modify(addr_zpx(), &Cpu::ror); cycles += 6; break;         // ROR zp,X
        case 0x6E: modify(addr_abs(), &Cpu::ror); cycles += 6; break;         // ROR abs
        case 0x7E: modify(addr_abx(), &Cpu::ror); cycles += 7; break;         // ROR abs,X

        // ---- Part 3: unofficial opcodes (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, NOPs) ------

        default:
            pc--;  // leave pc on the offending opcode so traces point at it
            char msg[48];
            std::snprintf(msg, sizeof msg, "opcode $%02X not implemented", opcode);
            todo(msg);
            break;
    }
}
