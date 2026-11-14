// tests/unit/stage197_7_lse_matrix_tests.cpp
// Campaign 197: Acceptance Test E
// Verifies ARMv8.1-A LSE (Large System Extensions) atomic instructions:
// CAS, SWP, LDADD, LDCLR, LDEOR, LDSET across all 4 widths (8, 16, 32, 64-bit)
// and all 4 ordering variants (Relaxed, Acquire, Release, Acquire-Release).

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <iomanip>
#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

static void write_u32(Memory& mem, std::uint64_t addr, std::uint32_t val) {
    mem.write_u32(addr, val);
}

// Encode CAS: size(31:30) 001000 1 A 1 Rs L 11111 Rn Rt
static std::uint32_t encode_cas(std::uint32_t size, bool acquire, bool release,
                                std::uint32_t expected, std::uint32_t base, std::uint32_t desired) {
    return ((size & 3u) << 30) | 0x08A07C00u
        | (acquire ? (1u << 22) : 0)
        | (release ? (1u << 15) : 0)
        | ((expected & 0x1Fu) << 16)
        | ((base & 0x1Fu) << 5)
        | (desired & 0x1Fu);
}

// Encode LSE Atomic: size(31:30) 111000 A R 1 Rs opc(15:12) 00 Rn Rt
static std::uint32_t encode_lse_atomic(std::uint32_t size, std::uint32_t opc, bool acquire, bool release,
                                       std::uint32_t source, std::uint32_t base, std::uint32_t target) {
    return ((size & 3u) << 30) | (0x38u << 24)
        | (acquire ? (1u << 23) : 0)
        | (release ? (1u << 22) : 0)
        | (1u << 21)
        | ((source & 0x1Fu) << 16)
        | ((opc & 0xFu) << 12)
        | ((base & 0x1Fu) << 5)
        | (target & 0x1Fu);
}

struct LseTestRecord {
    std::string mnemonic;
    std::string width;
    std::string ordering;
    std::uint32_t opcode;
    bool passed;
};

static std::uint64_t get_mask(std::uint32_t size) {
    switch (size) {
    case 0: return 0xFFULL;
    case 1: return 0xFFFFULL;
    case 2: return 0xFFFFFFFFULL;
    case 3: return 0xFFFFFFFFFFFFFFFFULL;
    default: return 0;
    }
}

int main() {
    std::cout << "[STAGE 197.7] Running Full LSE Atomic Instruction Matrix Tests (Acceptance Test E)..." << std::endl;

    Memory mem(0x40000);
    CPU cpu;
    cpu.set_current_el(1);
    GuestMemoryOrderModel model;
    cpu.set_memory_order_model(&model);

    std::vector<LseTestRecord> records;
    const std::uint64_t base_addr = 0x10000;
    cpu.write_register(0, base_addr); // X0 = base_addr

    const std::vector<std::string> width_names = {"8-bit (B)", "16-bit (H)", "32-bit (W)", "64-bit (X)"};
    const std::vector<std::pair<bool, bool>> orderings = {
        {false, false}, // Relaxed
        {true, false},  // Acquire (A)
        {false, true},  // Release (L)
        {true, true}    // Acquire-Release (AL)
    };
    const std::vector<std::string> ordering_names = {"Relaxed", "Acquire", "Release", "Acquire-Release"};

    std::uint64_t pc = 0x2000;
    std::size_t passed_count = 0;

    // 1. Test CAS family (CAS, CASA, CASL, CASAL across 4 widths = 16 tests)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (std::size_t ord = 0; ord < orderings.size(); ++ord) {
            bool acq = orderings[ord].first;
            bool rel = orderings[ord].second;
            std::string name = "CAS";
            if (acq) name += "A";
            if (rel) name += "L";

            // Initial memory state: 0x42
            mem.write_u64(base_addr, 0x42ULL & mask);
            // X1 = expected (0x42), X2 = desired (0x99)
            cpu.write_register(1, 0x42ULL & mask);
            cpu.write_register(2, 0x99ULL & mask);

            std::uint32_t opcode = encode_cas(size, acq, rel, 1, 0, 2);
            write_u32(mem, pc, opcode);
            cpu.set_program_counter(pc);

            cpu.step(mem);
            pc += 4;

            bool ok = (mem.read_u64(base_addr) & mask) == (0x99ULL & mask) &&
                      (cpu.read_register(1) & mask) == (0x42ULL & mask);
            records.push_back({name, width_names[size], ordering_names[ord], opcode, ok});
            if (ok) ++passed_count;
        }
    }

    // 2. Test SWP family (SWP, SWPA, SWPL, SWPAL across 4 widths = 16 tests)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (std::size_t ord = 0; ord < orderings.size(); ++ord) {
            bool acq = orderings[ord].first;
            bool rel = orderings[ord].second;
            std::string name = "SWP";
            if (acq) name += "A";
            if (rel) name += "L";

            mem.write_u64(base_addr, 0x55ULL & mask);
            cpu.write_register(1, 0xAAULL & mask); // X1 = new value to swap in
            cpu.write_register(2, 0);              // X2 = target to receive old value

            std::uint32_t opcode = encode_lse_atomic(size, 8, acq, rel, 1, 0, 2);
            write_u32(mem, pc, opcode);
            cpu.set_program_counter(pc);

            cpu.step(mem);
            pc += 4;

            bool ok = (mem.read_u64(base_addr) & mask) == (0xAAULL & mask) &&
                      (cpu.read_register(2) & mask) == (0x55ULL & mask);
            records.push_back({name, width_names[size], ordering_names[ord], opcode, ok});
            if (ok) ++passed_count;
        }
    }

    // 3. Test LDADD family (LDADD, LDADDA, LDADDL, LDADDAL across 4 widths = 16 tests)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (std::size_t ord = 0; ord < orderings.size(); ++ord) {
            bool acq = orderings[ord].first;
            bool rel = orderings[ord].second;
            std::string name = "LDADD";
            if (acq) name += "A";
            if (rel) name += "L";

            mem.write_u64(base_addr, 0x10ULL & mask);
            cpu.write_register(1, 0x25ULL & mask); // addend
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 0, acq, rel, 1, 0, 2);
            write_u32(mem, pc, opcode);
            cpu.set_program_counter(pc);

            cpu.step(mem);
            pc += 4;

            bool ok = (mem.read_u64(base_addr) & mask) == (0x35ULL & mask) &&
                      (cpu.read_register(2) & mask) == (0x10ULL & mask);
            records.push_back({name, width_names[size], ordering_names[ord], opcode, ok});
            if (ok) ++passed_count;
        }
    }

    // 4. Test LDCLR family (LDCLR, LDCLRA, LDCLRL, LDCLRAL across 4 widths = 16 tests)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (std::size_t ord = 0; ord < orderings.size(); ++ord) {
            bool acq = orderings[ord].first;
            bool rel = orderings[ord].second;
            std::string name = "LDCLR";
            if (acq) name += "A";
            if (rel) name += "L";

            mem.write_u64(base_addr, 0xFFULL & mask);
            cpu.write_register(1, 0x0FULL & mask); // bits to clear
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 1, acq, rel, 1, 0, 2);
            write_u32(mem, pc, opcode);
            cpu.set_program_counter(pc);

            cpu.step(mem);
            pc += 4;

            bool ok = (mem.read_u64(base_addr) & mask) == (0xF0ULL & mask) &&
                      (cpu.read_register(2) & mask) == (0xFFULL & mask);
            records.push_back({name, width_names[size], ordering_names[ord], opcode, ok});
            if (ok) ++passed_count;
        }
    }

    // 5. Test LDEOR family (LDEOR, LDEORA, LDEORL, LDEORAL across 4 widths = 16 tests)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (std::size_t ord = 0; ord < orderings.size(); ++ord) {
            bool acq = orderings[ord].first;
            bool rel = orderings[ord].second;
            std::string name = "LDEOR";
            if (acq) name += "A";
            if (rel) name += "L";

            mem.write_u64(base_addr, 0xA5ULL & mask);
            cpu.write_register(1, 0x5AULL & mask); // bits to XOR
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 2, acq, rel, 1, 0, 2);
            write_u32(mem, pc, opcode);
            cpu.set_program_counter(pc);

            cpu.step(mem);
            pc += 4;

            bool ok = (mem.read_u64(base_addr) & mask) == (0xFFULL & mask) &&
                      (cpu.read_register(2) & mask) == (0xA5ULL & mask);
            records.push_back({name, width_names[size], ordering_names[ord], opcode, ok});
            if (ok) ++passed_count;
        }
    }

    // 6. Test LDSET family (LDSET, LDSETA, LDSETL, LDSETAL across 4 widths = 16 tests)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (std::size_t ord = 0; ord < orderings.size(); ++ord) {
            bool acq = orderings[ord].first;
            bool rel = orderings[ord].second;
            std::string name = "LDSET";
            if (acq) name += "A";
            if (rel) name += "L";

            mem.write_u64(base_addr, 0x12ULL & mask);
            cpu.write_register(1, 0x84ULL & mask); // bits to set
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 3, acq, rel, 1, 0, 2);
            write_u32(mem, pc, opcode);
            cpu.set_program_counter(pc);

            cpu.step(mem);
            pc += 4;

            bool ok = (mem.read_u64(base_addr) & mask) == (0x96ULL & mask) &&
                      (cpu.read_register(2) & mask) == (0x12ULL & mask);
            records.push_back({name, width_names[size], ordering_names[ord], opcode, ok});
            if (ok) ++passed_count;
        }
    }

    std::cout << "  Total LSE atomic variants tested: " << records.size() << std::endl;
    std::cout << "  Passed variants: " << passed_count << " / " << records.size() << std::endl;

    TEST_ASSERT(records.size() == 96, "Exactly 96 LSE instruction variants evaluated");
    TEST_ASSERT(passed_count == 96, "All 96 LSE instruction variants PASSED");

    // Export coverage report to docs/campaign197/lse_instruction_coverage.json
    std::ofstream out("docs/campaign197/lse_instruction_coverage.json");
    if (out.is_open()) {
        out << "{\n"
            << "  \"campaign\": \"197\",\n"
            << "  \"acceptance_test\": \"E\",\n"
            << "  \"status\": \"PASSED\",\n"
            << "  \"total_variants\": " << records.size() << ",\n"
            << "  \"passed_variants\": " << passed_count << ",\n"
            << "  \"families\": [\"CAS\", \"SWP\", \"LDADD\", \"LDCLR\", \"LDEOR\", \"LDSET\"],\n"
            << "  \"widths\": [\"8-bit\", \"16-bit\", \"32-bit\", \"64-bit\"],\n"
            << "  \"orderings\": [\"Relaxed\", \"Acquire\", \"Release\", \"Acquire-Release\"],\n"
            << "  \"results\": [\n";
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& r = records[i];
            out << "    {\n"
                << "      \"mnemonic\": \"" << r.mnemonic << "\",\n"
                << "      \"width\": \"" << r.width << "\",\n"
                << "      \"ordering\": \"" << r.ordering << "\",\n"
                << "      \"opcode\": \"0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << r.opcode << std::dec << "\",\n"
                << "      \"passed\": " << (r.passed ? "true" : "false") << "\n"
                << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        out.close();
        std::cout << "  Exported coverage to docs/campaign197/lse_instruction_coverage.json" << std::endl;
    }

    std::cout << "[STAGE 197.7] All 96 LSE Atomic Instruction Variants PASSED." << std::endl;
    return 0;
}
