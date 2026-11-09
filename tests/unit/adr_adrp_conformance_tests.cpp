#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper: encode ARM64 ADR instruction
// imm is signed 21-bit byte offset (-1048576 to +1048575)
std::uint32_t encode_adr(std::uint32_t rd, std::int64_t imm) {
    std::uint32_t imm21 = static_cast<std::uint32_t>(imm) & 0x1FFFFFu;
    std::uint32_t immlo = imm21 & 0x3u;
    std::uint32_t immhi = (imm21 >> 2u) & 0x7FFFFu;
    return 0x10000000u | (immlo << 29u) | (immhi << 5u) | (rd & 0x1Fu);
}

// Helper: encode ARM64 ADRP instruction
// imm_pages is signed 21-bit page offset (-1048576 to +1048575)
std::uint32_t encode_adrp(std::uint32_t rd, std::int64_t imm_pages) {
    std::uint32_t imm21 = static_cast<std::uint32_t>(imm_pages) & 0x1FFFFFu;
    std::uint32_t immlo = imm21 & 0x3u;
    std::uint32_t immhi = (imm21 >> 2u) & 0x7FFFFu;
    return 0x90000000u | (immlo << 29u) | (immhi << 5u) | (rd & 0x1Fu);
}

// Test 1: ADR with zero, positive, and negative offsets
bool test_adr_basic_offsets() {
    std::cout << "[Test 1] ADR basic offsets...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024); // 16 MB

    constexpr std::uint64_t kBasePC = 0x200000; // 2 MB

    // Case 1A: imm = 0 -> Rd == PC
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adr(0, 0));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(0) == kBasePC, "ADR with imm=0 must yield PC");
    TEST_ASSERT(cpu.get_program_counter() == kBasePC + 4, "PC must increment by 4");

    // Case 1B: imm = +8
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adr(1, 8));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(1) == kBasePC + 8, "ADR with imm=+8 must yield PC + 8");

    // Case 1C: imm = -16
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adr(2, -16));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(2) == kBasePC - 16, "ADR with imm=-16 must yield PC - 16");

    return true;
}

// Test 2: ADR across page boundaries
bool test_adr_page_boundary_crossing() {
    std::cout << "[Test 2] ADR page boundary crossing...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    // PC at end of a 4KB page: 0x100FFC
    constexpr std::uint64_t kNearPageEnd = 0x100FFC;

    // Cross into next page: +8 bytes -> 0x101004
    memory.write_u32(static_cast<std::size_t>(kNearPageEnd), encode_adr(5, 8));
    cpu.set_program_counter(kNearPageEnd);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(5) == 0x101004, "ADR must cross page boundary into next page");

    // Cross into previous page from 0x101000: -8 bytes -> 0x100FF8
    constexpr std::uint64_t kNearPageStart = 0x101000;
    memory.write_u32(static_cast<std::size_t>(kNearPageStart), encode_adr(6, -8));
    cpu.set_program_counter(kNearPageStart);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(6) == 0x100FF8, "ADR must cross page boundary into previous page");

    return true;
}

// Test 3: ADR extreme immediate ranges (+1MB - 1, -1MB)
bool test_adr_extreme_ranges() {
    std::cout << "[Test 3] ADR extreme immediate ranges...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    constexpr std::uint64_t kMidPC = 0x800000; // 8 MB

    // Max positive immediate: +1048575 (0x0FFFFF)
    constexpr std::int64_t kMaxPos = 1048575;
    memory.write_u32(static_cast<std::size_t>(kMidPC), encode_adr(10, kMaxPos));
    cpu.set_program_counter(kMidPC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(10) == kMidPC + static_cast<std::uint64_t>(kMaxPos),
                "ADR max positive range (+1,048,575) must match");

    // Min negative immediate: -1048576 (-0x100000)
    constexpr std::int64_t kMinNeg = -1048576;
    memory.write_u32(static_cast<std::size_t>(kMidPC), encode_adr(11, kMinNeg));
    cpu.set_program_counter(kMidPC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(11) == kMidPC + static_cast<std::uint64_t>(kMinNeg),
                "ADR min negative range (-1,048,576) must match");

    return true;
}

// Test 4: ADR with Rd = XZR (31) must NOT modify SP or any register
bool test_adr_xzr_destination() {
    std::cout << "[Test 4] ADR with Rd = XZR (31)...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    constexpr std::uint64_t kBasePC = 0x10000;
    cpu.set_stack_pointer(0x7FFF0000);

    // Encode ADR XZR, #0x100
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adr(31, 0x100));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_stack_pointer() == 0x7FFF0000, "SP must NOT be modified when Rd = 31");
    TEST_ASSERT(cpu.get_program_counter() == kBasePC + 4, "PC must increment normally");

    return true;
}

// Test 5: ADRP page alignment masking (PC low 12 bits zeroed)
bool test_adrp_page_alignment() {
    std::cout << "[Test 5] ADRP page alignment masking...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    // Test with non-zero page offsets in PC: 0x2003F8 -> base page is 0x200000
    constexpr std::uint64_t kArbitraryPC = 0x2003F8;

    // ADRP X3, #0 (page 0)
    memory.write_u32(static_cast<std::size_t>(kArbitraryPC), encode_adrp(3, 0));
    cpu.set_program_counter(kArbitraryPC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(3) == 0x200000, "ADRP imm=0 must mask off low 12 bits of PC");

    // At edge of page: 0x200FFF
    constexpr std::uint64_t kPageEndPC = 0x200FFC;
    memory.write_u32(static_cast<std::size_t>(kPageEndPC), encode_adrp(4, 0));
    cpu.set_program_counter(kPageEndPC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(4) == 0x200000, "ADRP at page end must still mask to base 0x200000");

    return true;
}

// Test 6: ADRP positive and negative page offsets
bool test_adrp_page_offsets() {
    std::cout << "[Test 6] ADRP positive and negative page offsets...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    constexpr std::uint64_t kBasePC = 0x400500; // Base page: 0x400000

    // ADRP X7, +1 page (+0x1000) -> 0x401000
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(7, 1));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(7) == 0x401000, "ADRP +1 page must yield base + 0x1000");

    // ADRP X8, +16 pages (+0x10000) -> 0x410000
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(8, 16));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(8) == 0x410000, "ADRP +16 pages must yield base + 0x10000");

    // ADRP X9, -1 page (-0x1000) -> 0x3FF000
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(9, -1));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(9) == 0x3FF000, "ADRP -1 page must yield base - 0x1000");

    // ADRP X12, -8 pages (-0x8000) -> 0x3F8000
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(12, -8));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.read_register(12) == 0x3F8000, "ADRP -8 pages must yield base - 0x8000");

    return true;
}

// Test 7: ADRP extreme page ranges (+/- 4GB)
bool test_adrp_extreme_ranges() {
    std::cout << "[Test 7] ADRP extreme page ranges...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    constexpr std::uint64_t kBasePC = 0x500000; // Base page: 0x500000

    // Max positive pages: +1048575 pages (+0xFFFFF000)
    constexpr std::int64_t kMaxPages = 1048575;
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(15, kMaxPages));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    std::uint64_t expected_pos = 0x500000ULL + (static_cast<std::uint64_t>(kMaxPages) << 12u);
    TEST_ASSERT(cpu.read_register(15) == expected_pos, "ADRP max positive pages must match");

    // Min negative pages: -1048576 pages (-4GB)
    constexpr std::int64_t kMinPages = -1048576;
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(16, kMinPages));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    std::uint64_t expected_neg = 0x500000ULL + static_cast<std::uint64_t>(kMinPages << 12u);
    TEST_ASSERT(cpu.read_register(16) == expected_neg, "ADRP min negative pages must match");

    return true;
}

// Test 8: ADRP with Rd = XZR (31) must NOT modify SP
bool test_adrp_xzr_destination() {
    std::cout << "[Test 8] ADRP with Rd = XZR (31)...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    constexpr std::uint64_t kBasePC = 0x20000;
    cpu.set_stack_pointer(0x80001000);

    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adrp(31, 4));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_stack_pointer() == 0x80001000, "SP must NOT be modified when Rd = 31 in ADRP");
    TEST_ASSERT(cpu.get_program_counter() == kBasePC + 4, "PC must increment normally");

    return true;
}

// Test 9: Flag preservation: NZCV flags must remain unmodified by ADR and ADRP
bool test_flags_unmodified() {
    std::cout << "[Test 9] Flag preservation (NZCV)...\n";
    CPU cpu;
    Memory memory(16 * 1024 * 1024);

    constexpr std::uint64_t kBasePC = 0x30000;

    // Set NZCV to 0b1010 (N=1, Z=0, C=1, V=0)
    cpu.set_nzcv(0b1010);

    // Execute ADR
    memory.write_u32(static_cast<std::size_t>(kBasePC), encode_adr(20, 12));
    cpu.set_program_counter(kBasePC);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_nzcv() == 0b1010, "NZCV flags must not change after ADR");

    // Execute ADRP
    memory.write_u32(static_cast<std::size_t>(kBasePC + 4), encode_adrp(21, -2));
    cpu.set_program_counter(kBasePC + 4);
    cpu.step(memory);
    TEST_ASSERT(cpu.get_nzcv() == 0b1010, "NZCV flags must not change after ADRP");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE S0: ARM64 ADR and ADRP Conformance & Edge Case Tests\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_adr_basic_offsets, "ADR Basic Offsets (Zero, Positive, Negative)");
    run_test(test_adr_page_boundary_crossing, "ADR Page Boundary Crossing");
    run_test(test_adr_extreme_ranges, "ADR Extreme Ranges (+1MB - 1, -1MB)");
    run_test(test_adr_xzr_destination, "ADR with Rd = XZR (31) Discard & SP Invariant");
    run_test(test_adrp_page_alignment, "ADRP Page Alignment Masking (PC Low 12 Bits)");
    run_test(test_adrp_page_offsets, "ADRP Positive & Negative Page Offsets");
    run_test(test_adrp_extreme_ranges, "ADRP Extreme Page Ranges (+/- 4GB)");
    run_test(test_adrp_xzr_destination, "ADRP with Rd = XZR (31) Discard & SP Invariant");
    run_test(test_flags_unmodified, "NZCV Flag Preservation for ADR and ADRP");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
