// nestest trace-diff harness.
//
// nestest.nes run from $C000 exercises every CPU instruction without needing a PPU.
// nestest.log is the known-good trace from a real-hardware-accurate emulator.
// We compare PC, registers, and cycle count before every instruction and stop at the
// first mismatch, which points at exactly the instruction that's wrong.
//
// Usage: nestest_runner nestest.nes nestest.log [max_lines]
//   The official-opcode section is roughly the first 5,000 lines; unofficial opcodes follow.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "core/nes.h"

namespace {
// Reduce a nestest.log line to the same shape as Cpu::trace_state():
//   "C000  4C F5 C5  JMP $C5F5   A:00 X:00 Y:00 P:24 SP:FD PPU:  0, 21 CYC:7"
//   -> "C000 A:00 X:00 Y:00 P:24 SP:FD CYC:7"
bool normalize(const std::string& line, std::string& out) {
    auto regs = line.find("A:");
    auto ppu = line.find(" PPU:");
    auto cyc = line.find("CYC:");
    if (line.size() < 4 || regs == std::string::npos || ppu == std::string::npos || cyc == std::string::npos)
        return false;
    std::string tail = line.substr(cyc);
    while (!tail.empty() && (tail.back() == '\r' || tail.back() == ' ')) tail.pop_back();
    out = line.substr(0, 4) + " " + line.substr(regs, ppu - regs) + " " + tail;
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s nestest.nes nestest.log [max_lines]\n", argv[0]);
        return 2;
    }
    const long max_lines = argc > 3 ? std::atol(argv[3]) : 0;

    std::ifstream log(argv[2]);
    if (!log) {
        std::fprintf(stderr, "could not open %s\n", argv[2]);
        return 2;
    }

    try {
        Nes nes(argv[1]);
        nes.cpu.pc = 0xC000;  // "automation" entry point: runs all tests without a display

        std::string line, expected, previous;
        long line_no = 0;
        while (std::getline(log, line)) {
            if (max_lines && line_no >= max_lines) break;
            ++line_no;
            if (!normalize(line, expected)) continue;

            const std::string got = nes.cpu.trace_state();
            if (got != expected) {
                std::printf("MISMATCH at log line %ld\n", line_no);
                if (!previous.empty()) std::printf("  previous instruction:\n    %s\n", previous.c_str());
                std::printf("  expected: %s\n  got:      %s\n", expected.c_str(), got.c_str());
                return 1;
            }
            previous = line;

            nes.step();
            if (nes.cpu.halted()) {
                std::printf("Matched %ld line(s). Can't execute log line %ld: %s\n",
                            line_no - 1, line_no, nes.cpu.halt_reason().c_str());
                std::printf("    %s\n", line.c_str());
                return 1;
            }
        }

        // nestest also writes result codes: $02 = official opcode failures, $03 = unofficial.
        const uint8_t official = nes.bus.read(0x0002);
        const uint8_t unofficial = nes.bus.read(0x0003);
        std::printf("All %ld line(s) matched. Result bytes: $02=%02X $03=%02X%s\n",
                    line_no, official, unofficial,
                    (official == 0 && unofficial == 0) ? " (pass)" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}
