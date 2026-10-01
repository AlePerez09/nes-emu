// Per-opcode CPU tests using Tom Harte's SingleStepTests (nes6502 set):
//   https://github.com/SingleStepTests/65x02/tree/main/nes6502
// Each opcode file holds 10,000 randomized cases: an initial CPU + RAM state, and the
// expected state after executing exactly one instruction (plus its cycle count).
//
// Usage: cpu_tests <dir-with-json-files> [opcode-hex ...]
//   With no opcodes listed, every *.json in the directory is run.
//   Opcodes the CPU doesn't implement yet are reported as "not implemented", not failures.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>

#include <nlohmann/json.hpp>

#include "core/cpu.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// Flat 64 KB of RAM. Tracks touched addresses so it can be cleared cheaply between cases.
class FlatMemory : public CpuBus {
public:
    FlatMemory() : mem_(0x10000, 0) {}
    uint8_t read(uint16_t addr) override { return mem_[addr]; }
    void write(uint16_t addr, uint8_t value) override { mem_[addr] = value; touched_.push_back(addr); }
    void poke(uint16_t addr, uint8_t value) { mem_[addr] = value; touched_.push_back(addr); }
    uint8_t peek(uint16_t addr) const { return mem_[addr]; }
    void clear() { for (uint16_t a : touched_) mem_[a] = 0; touched_.clear(); }
private:
    std::vector<uint8_t> mem_;
    std::vector<uint16_t> touched_;
};

enum class Result { Pass, Fail, NotImplemented };

Result run_case(const json& t, FlatMemory& mem, std::string& why) {
    mem.clear();
    const json& in = t["initial"];
    for (const auto& cell : in["ram"]) mem.poke(cell[0].get<uint16_t>(), cell[1].get<uint8_t>());

    Cpu cpu(mem);
    cpu.pc = in["pc"];
    cpu.sp = in["s"];
    cpu.a = in["a"];
    cpu.x = in["x"];
    cpu.y = in["y"];
    cpu.p = in["p"];
    cpu.cycles = 0;

    cpu.step();
    if (cpu.halted()) { why = cpu.halt_reason(); return Result::NotImplemented; }

    const json& out = t["final"];
    char buf[160];
    auto check = [&](const char* name, unsigned got, unsigned want) {
        if (got == want) return true;
        std::snprintf(buf, sizeof buf, "%s: got $%X, want $%X", name, got, want);
        why = buf;
        return false;
    };
    if (!check("PC", cpu.pc, out["pc"])) return Result::Fail;
    if (!check("A", cpu.a, out["a"])) return Result::Fail;
    if (!check("X", cpu.x, out["x"])) return Result::Fail;
    if (!check("Y", cpu.y, out["y"])) return Result::Fail;
    if (!check("SP", cpu.sp, out["s"])) return Result::Fail;
    if (!check("P", cpu.p, out["p"])) return Result::Fail;
    for (const auto& cell : out["ram"]) {
        uint16_t addr = cell[0];
        std::snprintf(buf, sizeof buf, "RAM[$%04X]", addr);
        std::string label = buf;
        if (!check(label.c_str(), mem.peek(addr), cell[1])) return Result::Fail;
    }
    if (!check("cycles", unsigned(cpu.cycles), unsigned(t["cycles"].size()))) return Result::Fail;
    return Result::Pass;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <dir> [opcode-hex ...]\n", argv[0]);
        return 2;
    }
    const fs::path dir = argv[1];

    std::vector<fs::path> files;
    if (argc > 2) {
        for (int i = 2; i < argc; ++i) {
            std::string op = argv[i];
            std::transform(op.begin(), op.end(), op.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
        }
    } else if (fs::is_directory(dir)) {
        for (const auto& e : fs::directory_iterator(dir))
            if (e.path().extension() == ".json") files.push_back(e.path());
        std::sort(files.begin(), files.end());
    }
    if (files.empty()) {
        std::fprintf(stderr, "no test files found in %s (run tests/cpu/fetch_cpu_tests.sh)\n", dir.string().c_str());
        return 2;
    }

    FlatMemory mem;
    int passed_ops = 0, failed_ops = 0, missing_ops = 0;

    for (const auto& file : files) {
        std::ifstream in(file);
        if (!in) { std::printf("%s  missing file\n", file.filename().string().c_str()); ++failed_ops; continue; }
        const json cases = json::parse(in);
        const std::string op = file.stem().string();

        int pass = 0, fail = 0;
        std::string first_failure, reason;
        bool not_impl = false;
        for (const auto& t : cases) {
            switch (run_case(t, mem, reason)) {
                case Result::Pass: ++pass; break;
                case Result::Fail:
                    if (fail++ == 0) first_failure = t["name"].get<std::string>() + "  ->  " + reason;
                    break;
                case Result::NotImplemented: not_impl = true; break;
            }
            if (not_impl) break;
        }

        if (not_impl) {
            std::printf("%s  not implemented\n", op.c_str());
            ++missing_ops;
        } else if (fail == 0) {
            std::printf("%s  pass (%d cases)\n", op.c_str(), pass);
            ++passed_ops;
        } else {
            std::printf("%s  FAIL %d/%d  first: [%s]\n", op.c_str(), fail, pass + fail, first_failure.c_str());
            ++failed_ops;
        }
    }

    std::printf("\n%d opcode(s) pass, %d fail, %d not implemented\n", passed_ops, failed_ops, missing_ops);
    return failed_ops == 0 ? 0 : 1;
}
