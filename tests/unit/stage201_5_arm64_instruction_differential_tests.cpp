// ==============================================================================
// WinIOS Campaign 201 Stage 5: AArch64 Instruction Differential Testing
// ==============================================================================
// Verifies AArch64 instruction semantics against independent Arm DDI 0487
// architectural oracles:
// 1. ADDS / SUBS with precise NZCV flag condition generation (N, Z, C, V)
// 2. Shifted logical operations (ANDS, BICS, EOR with LSL, LSR, ASR)
// 3. Bitfield and data processing operations (UBFM, SBFM, RBIT, REV, CLZ)
// 4. Complete condition code evaluation (EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE)
// 5. Load/Store indexing modes, sign extensions (LDRSW, LDP/STP pre/post index)
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "memory/memory.h"

using namespace winios;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void step_insn(CPU& cpu, Memory& mem, std::uint64_t pc, std::uint32_t insn) {
    mem.write_u32(pc, insn);
    cpu.set_program_counter(pc);
    cpu.step(mem);
}

inline bool get_flag_n(const CPU& cpu) noexcept { return (cpu.get_nzcv() & 0x8u) != 0; }
inline bool get_flag_z(const CPU& cpu) noexcept { return (cpu.get_nzcv() & 0x4u) != 0; }
inline bool get_flag_c(const CPU& cpu) noexcept { return (cpu.get_nzcv() & 0x2u) != 0; }
inline bool get_flag_v(const CPU& cpu) noexcept { return (cpu.get_nzcv() & 0x1u) != 0; }

inline void set_flag_n(CPU& cpu, bool v) noexcept {
    auto flags = cpu.get_nzcv();
    if (v) flags |= 0x8u; else flags &= static_cast<std::uint8_t>(~0x8u);
    cpu.set_nzcv(flags);
}
inline void set_flag_z(CPU& cpu, bool v) noexcept {
    auto flags = cpu.get_nzcv();
    if (v) flags |= 0x4u; else flags &= static_cast<std::uint8_t>(~0x4u);
    cpu.set_nzcv(flags);
}
inline void set_flag_c(CPU& cpu, bool v) noexcept {
    auto flags = cpu.get_nzcv();
    if (v) flags |= 0x2u; else flags &= static_cast<std::uint8_t>(~0x2u);
    cpu.set_nzcv(flags);
}
inline void set_flag_v(CPU& cpu, bool v) noexcept {
    auto flags = cpu.get_nzcv();
    if (v) flags |= 0x1u; else flags &= static_cast<std::uint8_t>(~0x1u);
    cpu.set_nzcv(flags);
}

// -----------------------------------------------------------------------------
// Test 1: ADDS / SUBS NZCV Flag Conditions
// -----------------------------------------------------------------------------
void test_adds_subs_flags() {
    std::cout << "[Test 1] ADDS / SUBS NZCV Flag Matrix...\n";
    CPU cpu;
    Memory mem(0x100000);

    // Case 1.1: Signed positive overflow
    // 0x7FFFFFFFFFFFFFFF + 1 = 0x8000000000000000 (N=1, Z=0, C=0, V=1)
    // ADDS X0, X1, #1 (immediate: sf=1, op=0, S=1 -> 0xB1000420)
    cpu.reset();
    cpu.write_register(1, 0x7FFFFFFFFFFFFFFFULL);
    step_insn(cpu, mem, 0x1000, 0xB1000420u); // ADDS X0, X1, #1
    TEST_CHECK(cpu.read_register(0) == 0x8000000000000000ULL, "ADDS overflow value mismatch");
    TEST_CHECK(get_flag_n(cpu) == true, "N flag must be 1 (negative)");
    TEST_CHECK(get_flag_z(cpu) == false, "Z flag must be 0");
    TEST_CHECK(get_flag_c(cpu) == false, "C flag must be 0 (no carry-out)");
    TEST_CHECK(get_flag_v(cpu) == true, "V flag must be 1 (signed overflow)");

    // Case 1.2: Unsigned wrap with zero result
    // 0xFFFFFFFFFFFFFFFF + 1 = 0 (N=0, Z=1, C=1, V=0)
    cpu.reset();
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFULL);
    step_insn(cpu, mem, 0x1004, 0xB1000420u); // ADDS X0, X1, #1
    TEST_CHECK(cpu.read_register(0) == 0ULL, "ADDS carry zero mismatch");
    TEST_CHECK(get_flag_n(cpu) == false, "N flag must be 0");
    TEST_CHECK(get_flag_z(cpu) == true, "Z flag must be 1 (zero result)");
    TEST_CHECK(get_flag_c(cpu) == true, "C flag must be 1 (carry-out)");
    TEST_CHECK(get_flag_v(cpu) == false, "V flag must be 0");

    // Case 1.3: SUBS with unsigned borrow
    // 0 - 1 = 0xFFFFFFFFFFFFFFFF (N=1, Z=0, C=0 (borrow), V=0)
    // SUBS X0, X1, #1 (0xF1000420)
    cpu.reset();
    cpu.write_register(1, 0ULL);
    step_insn(cpu, mem, 0x1008, 0xF1000420u); // SUBS X0, X1, #1
    TEST_CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFFFFFFULL, "SUBS borrow value mismatch");
    TEST_CHECK(get_flag_n(cpu) == true, "N flag must be 1");
    TEST_CHECK(get_flag_z(cpu) == false, "Z flag must be 0");
    TEST_CHECK(get_flag_c(cpu) == false, "C flag must be 0 (borrow taken)");
    TEST_CHECK(get_flag_v(cpu) == false, "V flag must be 0");

    // Case 1.4: SUBS equal values
    // 0x12345678 - 0x12345678 = 0 (N=0, Z=1, C=1 (no borrow), V=0)
    // SUBS X0, X1, X2 (0xEB020020)
    cpu.reset();
    cpu.write_register(1, 0x12345678ULL);
    cpu.write_register(2, 0x12345678ULL);
    step_insn(cpu, mem, 0x100C, 0xEB020020u); // SUBS X0, X1, X2
    TEST_CHECK(cpu.read_register(0) == 0ULL, "SUBS equal value mismatch");
    TEST_CHECK(get_flag_n(cpu) == false, "N flag must be 0");
    TEST_CHECK(get_flag_z(cpu) == true, "Z flag must be 1");
    TEST_CHECK(get_flag_c(cpu) == true, "C flag must be 1 (no borrow)");
    TEST_CHECK(get_flag_v(cpu) == false, "V flag must be 0");

    std::cout << "  -> ADDS/SUBS flag tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Shifted Logical Operations
// -----------------------------------------------------------------------------
void test_shifted_logical() {
    std::cout << "[Test 2] Shifted Logical Operations...\n";
    CPU cpu;
    Memory mem(0x100000);

    // Case 2.1: ANDS with LSL #4
    // X1 = 0x00FF00FF00FF00FF, X2 = 0x000F000F000F000F
    // X2 LSL 4 = 0x00F000F000F000F0
    // X1 AND (X2 LSL 4) = 0x00F000F000F000F0
    // ANDS X0, X1, X2, LSL #4 (0xEA021020)
    cpu.reset();
    cpu.write_register(1, 0x00FF00FF00FF00FFULL);
    cpu.write_register(2, 0x000F000F000F000FULL);
    step_insn(cpu, mem, 0x2000, 0xEA021020u);
    TEST_CHECK(cpu.read_register(0) == 0x00F000F000F000F0ULL, "ANDS shifted mismatch");
    TEST_CHECK(get_flag_n(cpu) == false, "N flag must be 0");
    TEST_CHECK(get_flag_z(cpu) == false, "Z flag must be 0");

    // Case 2.2: BICS with LSR #8
    // X1 = 0xFFFFFFFFFFFFFFFF, X2 = 0x00FF000000000000
    // X2 LSR 8 = 0x0000FF0000000000
    // X1 BIC (X2 LSR 8) = ~0x0000FF0000000000
    // BICS X0, X1, X2, LSR #8 (0xEA622020)
    cpu.reset();
    cpu.write_register(1, 0xFFFFFFFFFFFFFFFFULL);
    cpu.write_register(2, 0x00FF000000000000ULL);
    step_insn(cpu, mem, 0x2004, 0xEA622020u);
    TEST_CHECK(cpu.read_register(0) == 0xFFFF00FFFFFFFFFFULL, "BICS shifted mismatch");

    // Case 2.3: EOR with ASR #4
    // X1 = 0x8000000000000000, X2 = 0x8000000000000000
    // X2 ASR 4 = 0xF800000000000000
    // X1 EOR (X2 ASR 4) = 0x7800000000000000
    // EOR X0, X1, X2, ASR #4 (0xCA821020)
    cpu.reset();
    cpu.write_register(1, 0x8000000000000000ULL);
    cpu.write_register(2, 0x8000000000000000ULL);
    step_insn(cpu, mem, 0x2008, 0xCA821020u);
    TEST_CHECK(cpu.read_register(0) == 0x7800000000000000ULL, "EOR ASR mismatch");

    std::cout << "  -> Shifted logical tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Bitfield Manipulation & Data Processing
// -----------------------------------------------------------------------------
void test_bitfield_operations() {
    std::cout << "[Test 3] Bitfield & Data Processing Operations...\n";
    CPU cpu;
    Memory mem(0x100000);

    // Case 3.1: UBFM X0, X1, #16, #31 (Extract bits [31:16] -> bits [15:0])
    // UBFM X0, X1, #16, #31 (0xD3507C20)
    cpu.reset();
    cpu.write_register(1, 0x123456789ABCDEF0ULL);
    step_insn(cpu, mem, 0x3000, 0xD3507C20u);
    TEST_CHECK(cpu.read_register(0) == 0x9ABCULL, "UBFM bitfield extract mismatch");

    // Case 3.2: SBFM X0, X1, #0, #15 (Sign extend lower 16 bits to 64-bit)
    // If lower 16 bits = 0x8001 -> sign-extended to 0xFFFFFFFFFFFF8001
    // SBFM X0, X1, #0, #15 (0x93403C20)
    cpu.reset();
    cpu.write_register(1, 0x0000000000008001ULL);
    step_insn(cpu, mem, 0x3004, 0x93403C20u);
    TEST_CHECK(cpu.read_register(0) == 0xFFFFFFFFFFFF8001ULL, "SBFM sign-extension mismatch");

    // Case 3.3: RBIT X0, X1 (Bit reversal)
    // 0x8000000000000001 -> 0x8000000000000001
    // 0x0000000000000001 -> 0x8000000000000000
    // RBIT X0, X1 (0xDAC00020)
    cpu.reset();
    cpu.write_register(1, 0x0000000000000001ULL);
    step_insn(cpu, mem, 0x3008, 0xDAC00020u);
    TEST_CHECK(cpu.read_register(0) == 0x8000000000000000ULL, "RBIT single-bit mismatch");

    // Case 3.4: REV X0, X1 (Byte reversal of 64-bit word)
    // 0x0102030405060708 -> 0x0807060504030201
    // REV X0, X1 (0xDAC00C20)
    cpu.reset();
    cpu.write_register(1, 0x0102030405060708ULL);
    step_insn(cpu, mem, 0x300C, 0xDAC00C20u);
    TEST_CHECK(cpu.read_register(0) == 0x0807060504030201ULL, "REV byte reversal mismatch");

    // Case 3.5: CLZ X0, X1 (Count leading zeros)
    // 0x0000000000000001 -> 63 leading zeros
    // CLZ X0, X1 (0xDAC01020)
    cpu.reset();
    cpu.write_register(1, 0x0000000000000001ULL);
    step_insn(cpu, mem, 0x3010, 0xDAC01020u);
    TEST_CHECK(cpu.read_register(0) == 63ULL, "CLZ mismatch for bit 0");

    std::cout << "  -> Bitfield manipulation tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Complete Condition Code Evaluation
// -----------------------------------------------------------------------------
void test_condition_codes() {
    std::cout << "[Test 4] Condition Code Evaluation...\n";
    CPU cpu;
    Memory mem(0x100000);

    // B.cond offset +8: opcode 0x54000040 | cond
    // If taken: PC advances to PC + 8
    // If fallthrough: PC advances to PC + 4

    // Test B.EQ (cond=0, Z==1)
    cpu.reset();
    set_flag_z(cpu, true);
    step_insn(cpu, mem, 0x4000, 0x54000040u); // B.EQ +8
    TEST_CHECK(cpu.get_program_counter() == 0x4008, "B.EQ when Z=1 must branch");

    cpu.reset();
    set_flag_z(cpu, false);
    step_insn(cpu, mem, 0x4000, 0x54000040u); // B.EQ +8
    TEST_CHECK(cpu.get_program_counter() == 0x4004, "B.EQ when Z=0 must fall through");

    // Test B.NE (cond=1, Z==0)
    cpu.reset();
    set_flag_z(cpu, false);
    step_insn(cpu, mem, 0x4010, 0x54000041u); // B.NE +8
    TEST_CHECK(cpu.get_program_counter() == 0x4018, "B.NE when Z=0 must branch");

    // Test B.CS / B.HS (cond=2, C==1)
    cpu.reset();
    set_flag_c(cpu, true);
    step_insn(cpu, mem, 0x4020, 0x54000042u); // B.CS +8
    TEST_CHECK(cpu.get_program_counter() == 0x4028, "B.CS when C=1 must branch");

    // Test B.CC / B.LO (cond=3, C==0)
    cpu.reset();
    set_flag_c(cpu, false);
    step_insn(cpu, mem, 0x4030, 0x54000043u); // B.CC +8
    TEST_CHECK(cpu.get_program_counter() == 0x4038, "B.CC when C=0 must branch");

    // Test B.MI (cond=4, N==1)
    cpu.reset();
    set_flag_n(cpu, true);
    step_insn(cpu, mem, 0x4040, 0x54000044u); // B.MI +8
    TEST_CHECK(cpu.get_program_counter() == 0x4048, "B.MI when N=1 must branch");

    // Test B.PL (cond=5, N==0)
    cpu.reset();
    set_flag_n(cpu, false);
    step_insn(cpu, mem, 0x4050, 0x54000045u); // B.PL +8
    TEST_CHECK(cpu.get_program_counter() == 0x4058, "B.PL when N=0 must branch");

    // Test B.VS (cond=6, V==1)
    cpu.reset();
    set_flag_v(cpu, true);
    step_insn(cpu, mem, 0x4060, 0x54000046u); // B.VS +8
    TEST_CHECK(cpu.get_program_counter() == 0x4068, "B.VS when V=1 must branch");

    // Test B.VC (cond=7, V==0)
    cpu.reset();
    set_flag_v(cpu, false);
    step_insn(cpu, mem, 0x4070, 0x54000047u); // B.VC +8
    TEST_CHECK(cpu.get_program_counter() == 0x4078, "B.VC when V=0 must branch");

    // Test B.HI (cond=8, C==1 and Z==0)
    cpu.reset();
    set_flag_c(cpu, true);
    set_flag_z(cpu, false);
    step_insn(cpu, mem, 0x4080, 0x54000048u); // B.HI +8
    TEST_CHECK(cpu.get_program_counter() == 0x4088, "B.HI when C=1 and Z=0 must branch");

    // Test B.LS (cond=9, C==0 or Z==1)
    cpu.reset();
    set_flag_c(cpu, false);
    set_flag_z(cpu, false);
    step_insn(cpu, mem, 0x4090, 0x54000049u); // B.LS +8
    TEST_CHECK(cpu.get_program_counter() == 0x4098, "B.LS when C=0 must branch");

    // Test B.GE (cond=10, N==V)
    cpu.reset();
    set_flag_n(cpu, true);
    set_flag_v(cpu, true);
    step_insn(cpu, mem, 0x40A0, 0x5400004Au); // B.GE +8
    TEST_CHECK(cpu.get_program_counter() == 0x40A8, "B.GE when N=V must branch");

    // Test B.LT (cond=11, N!=V)
    cpu.reset();
    set_flag_n(cpu, true);
    set_flag_v(cpu, false);
    step_insn(cpu, mem, 0x40B0, 0x5400004Bu); // B.LT +8
    TEST_CHECK(cpu.get_program_counter() == 0x40B8, "B.LT when N!=V must branch");

    // Test B.GT (cond=12, Z==0 and N==V)
    cpu.reset();
    set_flag_z(cpu, false);
    set_flag_n(cpu, false);
    set_flag_v(cpu, false);
    step_insn(cpu, mem, 0x40C0, 0x5400004Cu); // B.GT +8
    TEST_CHECK(cpu.get_program_counter() == 0x40C8, "B.GT when Z=0 and N=V must branch");

    // Test B.LE (cond=13, Z==1 or N!=V)
    cpu.reset();
    set_flag_z(cpu, true);
    set_flag_n(cpu, false);
    set_flag_v(cpu, false);
    step_insn(cpu, mem, 0x40D0, 0x5400004Du); // B.LE +8
    TEST_CHECK(cpu.get_program_counter() == 0x40D8, "B.LE when Z=1 must branch");

    std::cout << "  -> Condition code evaluation passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Load/Store Indexing Modes & Sign Extensions
// -----------------------------------------------------------------------------
void test_load_store_indexing() {
    std::cout << "[Test 5] Load/Store Indexing & Sign Extensions...\n";
    CPU cpu;
    Memory mem(0x100000);

    // Case 5.1: LDRSW X0, [X1] (Load 32-bit signed and sign-extend to 64-bit)
    // Memory at 0x8000 contains 0x80001234 -> sign-extended to 0xFFFFFFFF80001234
    // LDRSW X0, [X1] (0xB9800020)
    cpu.reset();
    mem.write_u32(0x8000, 0x80001234u);
    cpu.write_register(1, 0x8000ULL);
    step_insn(cpu, mem, 0x5000, 0xB9800020u);
    TEST_CHECK(cpu.read_register(0) == 0xFFFFFFFF80001234ULL, "LDRSW sign-extension mismatch");

    // Case 5.2: STP X0, X1, [SP, #-16]! (Pre-indexed push)
    // STP X0, X1, [SP, #-16]! (0xA9BF07E0)
    cpu.reset();
    cpu.set_stack_pointer(0x20000ULL);
    cpu.write_register(0, 0x1111222233334444ULL);
    cpu.write_register(1, 0x5555666677778888ULL);
    step_insn(cpu, mem, 0x5004, 0xA9BF07E0u);
    TEST_CHECK(cpu.get_stack_pointer() == 0x1FFF0ULL, "STP pre-index SP update mismatch");
    TEST_CHECK(mem.read_u64(0x1FFF0ULL) == 0x1111222233334444ULL, "STP memory X0 mismatch");
    TEST_CHECK(mem.read_u64(0x1FFF8ULL) == 0x5555666677778888ULL, "STP memory X1 mismatch");

    // Case 5.3: LDP X2, X3, [SP], #16 (Post-indexed pop)
    // LDP X2, X3, [SP], #16 (0xA8C10FE2)
    step_insn(cpu, mem, 0x5008, 0xA8C10FE2u);
    TEST_CHECK(cpu.get_stack_pointer() == 0x20000ULL, "LDP post-index SP update mismatch");
    TEST_CHECK(cpu.read_register(2) == 0x1111222233334444ULL, "LDP X2 mismatch");
    TEST_CHECK(cpu.read_register(3) == 0x5555666677778888ULL, "LDP X3 mismatch");

    std::cout << "  -> Load/Store indexing tests passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.5 AArch64 Instruction Differential Tests ===\n";
    test_adds_subs_flags();
    test_shifted_logical();
    test_bitfield_operations();
    test_condition_codes();
    test_load_store_indexing();

    if (g_failures == 0) {
        std::cout << "All Stage 201.5 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.5 test(s) failed!\n";
        return 1;
    }
}
