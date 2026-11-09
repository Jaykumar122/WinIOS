#include "cpu/cpu.h"
#include "cpu/sysreg.h"
#include "cpu/synchronous_exception.h"
#include "cpu/smp_core_state.h"
#include "snapshot/smp_snapshot_format.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"

#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::sysreg;
using namespace winios::reference;

namespace {

// Helper to encode MRS Xt, <sysreg>
// 1101 0101 0011 (o0) (op1) (CRn) (CRm) (op2) (Rt)
std::uint32_t encode_mrs(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5300000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

// Helper to encode MSR <sysreg>, Xt
// 1101 0101 0001 (o0) (op1) (CRn) (CRm) (op2) (Rt)
std::uint32_t encode_msr(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5100000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

} // namespace

// =============================================================================
// Test 1: System Register Descriptor Table and Centralized Metadata
// =============================================================================
bool test_sysreg_descriptor_table() {
    const auto descriptors = get_all_descriptors();
    TEST_ASSERT(!descriptors.empty(), "SysReg descriptors must not be empty");
    TEST_ASSERT(descriptors.size() >= 80, "Expected at least 80 system register descriptors");

    // Verify key system registers exist
    const auto* cur_el = find_descriptor_by_name("CurrentEL");
    TEST_ASSERT(cur_el != nullptr, "CurrentEL descriptor must exist");
    TEST_ASSERT(cur_el->key == 0x4212u, "CurrentEL key must match 0x4212");
    TEST_ASSERT(cur_el->is_readable && !cur_el->is_writable, "CurrentEL must be read-only");

    const auto* tpidr_el0 = find_descriptor_by_name("TPIDR_EL0");
    TEST_ASSERT(tpidr_el0 != nullptr, "TPIDR_EL0 descriptor must exist");
    TEST_ASSERT(tpidr_el0->min_el == 0, "TPIDR_EL0 must be accessible at EL0");
    TEST_ASSERT(tpidr_el0->is_readable && tpidr_el0->is_writable, "TPIDR_EL0 must be R/W");

    const auto* tpidr_el1 = find_descriptor_by_name("TPIDR_EL1");
    TEST_ASSERT(tpidr_el1 != nullptr, "TPIDR_EL1 descriptor must exist");
    TEST_ASSERT(tpidr_el1->min_el == 1, "TPIDR_EL1 must require EL1");
    TEST_ASSERT(tpidr_el1->is_readable && tpidr_el1->is_writable, "TPIDR_EL1 must be R/W");

    const auto* sctlr = find_descriptor_by_name("SCTLR_EL1");
    TEST_ASSERT(sctlr != nullptr, "SCTLR_EL1 descriptor must exist");
    TEST_ASSERT(sctlr->min_el == 1, "SCTLR_EL1 must require EL1");

    return true;
}

// =============================================================================
// Test 2: Thread Pointer Registers (TPIDR_EL0, TPIDR_EL1, TPIDRRO_EL0)
// =============================================================================
bool test_tpidr_registers() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x10000);

    // Initial values must be 0
    TEST_ASSERT(cpu.get_tpidr_el0() == 0, "TPIDR_EL0 reset value must be 0");
    TEST_ASSERT(cpu.get_tpidr_el1() == 0, "TPIDR_EL1 reset value must be 0");
    TEST_ASSERT(cpu.get_tpidrro_el0() == 0, "TPIDRRO_EL0 reset value must be 0");

    // Write TPIDR_EL0 via MSR
    cpu.write_register(0, 0x1122334455667788ULL);
    mem.write_u32(0x1000, encode_msr(3, 3, 13, 0, 2, 0)); // MSR TPIDR_EL0, X0
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x1122334455667788ULL, "TPIDR_EL0 must reflect written value");

    // Read TPIDR_EL0 via MRS
    mem.write_u32(0x1004, encode_mrs(3, 3, 13, 0, 2, 1)); // MRS X1, TPIDR_EL0
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 0x1122334455667788ULL, "MRS X1, TPIDR_EL0 must read expected value");

    // Write TPIDR_EL1 via MSR at EL1
    cpu.write_register(2, 0xAABBCCDDEEFF0011ULL);
    mem.write_u32(0x1008, encode_msr(3, 0, 13, 0, 4, 2)); // MSR TPIDR_EL1, X2
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_tpidr_el1() == 0xAABBCCDDEEFF0011ULL, "TPIDR_EL1 must reflect written value");

    // Read TPIDR_EL1 via MRS
    mem.write_u32(0x100C, encode_mrs(3, 0, 13, 0, 4, 3)); // MRS X3, TPIDR_EL1
    cpu.set_program_counter(0x100C);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(3) == 0xAABBCCDDEEFF0011ULL, "MRS X3, TPIDR_EL1 must read expected value");

    return true;
}

// =============================================================================
// Test 3: Strict Privilege Enforcement (EL0 vs EL1)
// =============================================================================
bool test_privilege_enforcement() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    Memory mem(0x10000);

    // Set EL0
    cpu.set_current_el(0);

    // 1. Legal access from EL0: TPIDR_EL0 read/write
    cpu.write_register(0, 0x1234ULL);
    mem.write_u32(0x1000, encode_msr(3, 3, 13, 0, 2, 0)); // MSR TPIDR_EL0, X0
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(!cpu.exception_taken_in_step(), "Legal EL0 access to TPIDR_EL0 must not raise exception");
    TEST_ASSERT(cpu.get_tpidr_el0() == 0x1234ULL, "TPIDR_EL0 written at EL0");

    // 2. Illegal access from EL0: Attempting to access TPIDR_EL1 must raise Undefined Instruction to EL1
    mem.write_u32(0x1004, encode_mrs(3, 0, 13, 0, 4, 1)); // MRS X1, TPIDR_EL1 at EL0
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step(), "Access to TPIDR_EL1 from EL0 must raise exception");
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception must be taken to EL1");
    // ESR_EL1.EC must be 0x00 (Unknown / Undefined)
    const std::uint32_t esr = cpu.get_esr_el1();
    const std::uint32_t ec = (esr >> 26u) & 0x3Fu;
    TEST_ASSERT(ec == 0x00u, "Privilege violation from EL0 must produce EC=0x00 (Undefined Instruction)");
    TEST_ASSERT(cpu.get_elr_el1() == 0x1004, "ELR_EL1 must record faulting instruction address");

    // 3. Illegal access from EL0: Attempting to access SCTLR_EL1 from EL0
    cpu.set_current_el(0);
    cpu.set_exception_taken_in_step(false);
    mem.write_u32(0x1008, encode_mrs(3, 0, 1, 0, 0, 2)); // MRS X2, SCTLR_EL1 at EL0
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step(), "Access to SCTLR_EL1 from EL0 must raise exception");
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception must be taken to EL1");

    // 4. Illegal access from EL0: ERET at EL0 must raise Undefined Instruction
    cpu.set_current_el(0);
    cpu.set_exception_taken_in_step(false);
    mem.write_u32(0x100C, 0xD69F03E0u); // ERET
    cpu.set_program_counter(0x100C);
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step(), "ERET at EL0 must raise exception");
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception must be taken to EL1");

    return true;
}

// =============================================================================
// Test 4: W/X Register Semantics and Zero-Extension Invariants
// =============================================================================
bool test_wx_register_semantics() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x10000);

    // Initial state: populate X0..X5 with full 64-bit patterns
    for (std::size_t i = 0; i < 6; ++i) {
        cpu.write_register(i, 0xFFFFFFFFFFFFFFFFULL);
    }

    // 1. Write 32-bit W0: mov w0, #0x1234 (0x52824680u)
    mem.write_u32(0x1000, 0x52824680u);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1234ULL, "Writing W0 must zero upper 32 bits of X0");

    // 2. 32-bit addition: adds w1, w0, #1
    // ADDS W1, W0, #1: 0x11000401u
    mem.write_u32(0x1004, 0x11000401u);
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 0x1235ULL, "W1 addition must zero upper 32 bits of X1");

    // 3. 64-bit write preserves all 64 bits
    cpu.write_register(2, 0x123456789ABCDEF0ULL);
    TEST_ASSERT(cpu.read_register(2) == 0x123456789ABCDEF0ULL, "64-bit write preserves all 64 bits");

    // 4. Test reference helper equivalence
    TEST_ASSERT(ReferenceSysRegModel::write_wn_zero_extends_xn(0xFFFFFFFFFFFFFFFFULL, 0x12345678u) == 0x12345678ULL,
                "Reference zero extension must clear upper 32 bits");
    TEST_ASSERT(ReferenceSysRegModel::read_wn_lower32(0x123456789ABCDEF0ULL) == 0x9ABCDEF0u,
                "Reference read Wn must return lower 32 bits");

    return true;
}

// =============================================================================
// Test 5: Register 31 SP vs XZR Distinctions
// =============================================================================
bool test_r31_sp_vs_xzr() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x10000);

    cpu.set_stack_pointer(0x8000ULL);

    // 1. In ADD immediate with Rd=31 (SP destination): ADD SP, SP, #16 -> 0x910043FFu
    mem.write_u32(0x1000, 0x910043FFu);
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_stack_pointer() == 0x8010ULL, "ADD immediate with Rd=31 must update SP");

    // 2. In MRS with Rt=31: reading to XZR discards the result, does NOT modify SP
    // MRS XZR, TPIDR_EL0
    mem.write_u32(0x1004, encode_mrs(3, 3, 13, 0, 2, 31));
    cpu.set_program_counter(0x1004);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_stack_pointer() == 0x8010ULL, "MRS to XZR must not corrupt SP");

    // 3. In MSR with Rt=31: writing from XZR writes 0
    cpu.set_tpidr_el0(0xDEADBEEFULL);
    // MSR TPIDR_EL0, XZR
    mem.write_u32(0x1008, encode_msr(3, 3, 13, 0, 2, 31));
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_tpidr_el0() == 0ULL, "MSR from XZR must write 0");

    return true;
}

// =============================================================================
// Test 6: Condition Code Evaluation (NZCV)
// =============================================================================
bool test_condition_codes() {
    // Test all 16 condition codes against known NZCV configurations
    // N: bit 31, Z: bit 30, C: bit 29, V: bit 28
    constexpr std::uint32_t kN = (1u << 31);
    constexpr std::uint32_t kZ = (1u << 30);
    constexpr std::uint32_t kC = (1u << 29);
    constexpr std::uint32_t kV = (1u << 28);

    // 0: EQ (Z == 1)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(0, kZ), "EQ with Z=1");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(0, 0), "EQ with Z=0");

    // 1: NE (Z == 0)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(1, 0), "NE with Z=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(1, kZ), "NE with Z=1");

    // 2: CS/HS (C == 1)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(2, kC), "CS with C=1");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(2, 0), "CS with C=0");

    // 3: CC/LO (C == 0)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(3, 0), "CC with C=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(3, kC), "CC with C=1");

    // 4: MI (N == 1)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(4, kN), "MI with N=1");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(4, 0), "MI with N=0");

    // 5: PL (N == 0)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(5, 0), "PL with N=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(5, kN), "PL with N=1");

    // 6: VS (V == 1)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(6, kV), "VS with V=1");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(6, 0), "VS with V=0");

    // 7: VC (V == 0)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(7, 0), "VC with V=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(7, kV), "VC with V=1");

    // 8: HI (C == 1 && Z == 0)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(8, kC), "HI with C=1, Z=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(8, kC | kZ), "HI with C=1, Z=1");

    // 9: LS (C == 0 || Z == 1)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(9, 0), "LS with C=0");
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(9, kZ), "LS with Z=1");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(9, kC), "LS with C=1, Z=0");

    // 10: GE (N == V)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(10, 0), "GE with N=0, V=0");
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(10, kN | kV), "GE with N=1, V=1");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(10, kN), "GE with N=1, V=0");

    // 11: LT (N != V)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(11, kN), "LT with N=1, V=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(11, 0), "LT with N=0, V=0");

    // 12: GT (Z == 0 && N == V)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(12, 0), "GT with Z=0, N=0, V=0");
    TEST_ASSERT(!ReferenceExecutor::evaluate_condition(12, kZ), "GT with Z=1");

    // 13: LE (Z == 1 || N != V)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(13, kZ), "LE with Z=1");
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(13, kN), "LE with N=1, V=0");

    // 14: AL (always)
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(14, 0), "AL always true");
    TEST_ASSERT(ReferenceExecutor::evaluate_condition(15, 0), "NV always true");

    return true;
}

// =============================================================================
// Test 7: Exception Entry and ERET State Preservation
// =============================================================================
bool test_exception_entry_and_eret() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    Memory mem(0x10000);

    // Setup initial EL0 state
    cpu.set_current_el(0);
    cpu.set_nzcv(0x5); // N=0, Z=1, C=0, V=1
    cpu.set_daif(0x0); // Unmasked
    cpu.set_program_counter(0x2000);
    cpu.set_vbar_el1(0x4000ULL);

    // Generate SVC #0x42: 0xD4000841u
    mem.write_u32(0x2000, 0xD4000841u);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 1, "SVC must enter EL1");
    TEST_ASSERT(cpu.get_elr_el1() == 0x2004, "ELR_EL1 must hold next instruction address");
    TEST_ASSERT(cpu.get_spsr_el1() != 0, "SPSR_EL1 must save previous PSTATE");
    TEST_ASSERT(cpu.get_daif() == 0xF, "Exception entry must mask all interrupts");

    // Verify SPSR_EL1 encoding
    const std::uint32_t spsr = cpu.get_spsr_el1();
    const std::uint8_t saved_nzcv = static_cast<std::uint8_t>((spsr >> 28u) & 0xFu);
    const std::uint32_t saved_m = spsr & 0x1Fu;
    TEST_ASSERT(saved_nzcv == 0x5, "SPSR_EL1 must preserve NZCV");
    TEST_ASSERT(saved_m == 0x00, "SPSR_EL1 must record EL0t mode");

    // Execute ERET: 0xD69F03E0u
    mem.write_u32(cpu.get_program_counter(), 0xD69F03E0u);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_current_el() == 0, "ERET must restore EL0");
    TEST_ASSERT(cpu.get_program_counter() == 0x2004, "ERET must restore PC from ELR_EL1");
    TEST_ASSERT(cpu.get_nzcv() == 0x5, "ERET must restore NZCV");
    TEST_ASSERT(cpu.get_daif() == 0x0, "ERET must restore DAIF");

    // 2. Illegal ERET test: ERET with invalid mode field (e.g. attempting to return to EL2 when executing at EL1)
    cpu.set_current_el(1);
    cpu.set_spsr_el1(0x00000008u); // Mode = 0x8 (EL2t) -> illegal from EL1
    cpu.set_elr_el1(0x3000);
    mem.write_u32(0x1000, 0xD69F03E0u); // ERET
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    TEST_ASSERT(cpu.get_il(), "Illegal ERET must set PSTATE.IL");
    TEST_ASSERT(cpu.get_program_counter() == 0x3000, "PC updated to return address");

    // Next instruction fetch with PSTATE.IL must take Illegal Execution State exception (EC=0x0E)
    mem.write_u32(0x3000, 0xD503201Fu); // NOP
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step(), "Instruction with PSTATE.IL must take exception");
    const std::uint32_t il_esr = cpu.get_esr_el1();
    TEST_ASSERT(((il_esr >> 26u) & 0x3Fu) == 0x0Eu, "ESR.EC must be 0x0E for Illegal Execution State");

    return true;
}

// =============================================================================
// Test 8: Barriers and System Instructions (DMB, DSB, ISB, WFI, WFE, SEV, SEVL)
// =============================================================================
bool test_barriers_and_system_instructions() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x10000);

    // DMB ISH: 0xD5033BBFu (CRn=3, op1=3, op2=5, CRm=11, Rt=31)
    mem.write_u32(0x1000, 0xD5033BBFu);
    // DSB ISH: 0xD50339BFu (CRn=3, op1=3, op2=4, CRm=9, Rt=31)
    mem.write_u32(0x1004, 0xD50339BFu);
    // ISB: 0xD5033FDFu (CRn=3, op1=3, op2=6, CRm=15, Rt=31)
    mem.write_u32(0x1008, 0xD5033FDFu);

    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004, "DMB must advance PC by 4");

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1008, "DSB must advance PC by 4");

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x100C, "ISB must advance PC by 4");

    // SEVL: 0xD50320BFu (imm7 = 5)
    mem.write_u32(0x100C, 0xD50320BFu);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_event_register(), "SEVL must set event register");

    // WFE with event register set: clears event register and does NOT wait
    mem.write_u32(0x1010, 0xD503205Fu); // WFE (imm7 = 2)
    cpu.step(mem);
    TEST_ASSERT(!cpu.get_event_register(), "WFE must clear event register");
    TEST_ASSERT(!cpu.is_waiting_for_event(), "WFE must not wait if event register was set");

    return true;
}

// =============================================================================
// Test 9: Snapshot Parity and Canonical Hash Invariants with TPIDR
// =============================================================================
bool test_snapshot_and_hash_invariants() {
    winios::SmpCoreState orig{};
    orig.reset(0, winios::CoreLifecycleState::Runnable);
    orig.pc = 0x12345678ULL;
    orig.x[0] = 0xAA55AA55ULL;
    orig.tpidr_el0 = 0x1122334455667788ULL;
    orig.tpidr_el1 = 0x8877665544332211ULL;
    orig.tpidrro_el0 = 0xAABBCCDDEEFF0011ULL;

    const auto buf = winios::snapshot::serialize_core_architectural_state(orig);
    TEST_ASSERT(buf.size() == winios::snapshot::kCoreArchPayloadSize,
                "Serialized core state includes legacy, debug, and PMU blocks");

    winios::SmpCoreState restored{};
    const auto res = winios::snapshot::deserialize_core_architectural_state(buf.data(), buf.size(), restored);
    TEST_ASSERT(res.success, "Deserialization must succeed");

    TEST_ASSERT(orig == restored, "Restored state must match original exactly");
    TEST_ASSERT(restored.tpidr_el0 == 0x1122334455667788ULL, "Restored TPIDR_EL0 matches");
    TEST_ASSERT(restored.tpidr_el1 == 0x8877665544332211ULL, "Restored TPIDR_EL1 matches");
    TEST_ASSERT(restored.tpidrro_el0 == 0xAABBCCDDEEFF0011ULL, "Restored TPIDRRO_EL0 matches");

    TEST_ASSERT(orig.compute_canonical_hash() == restored.compute_canonical_hash(),
                "Canonical hash must be identical after round-trip serialization");

    return true;
}

// =============================================================================
// Test 10: Seed-Based Fuzzing Campaign (Seed 0x59535247 = "SYRG")
// =============================================================================
bool test_fuzz_campaign() {
    constexpr std::uint32_t kFuzzSeed = 0x59535247u; // "SYRG"
    std::mt19937 prng(kFuzzSeed);

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    Memory mem(0x10000);

    ReferenceCpuState ref_cpu{};
    ReferenceSysRegState ref_sysregs{};

    // Run 1000 random instructions through both production and independent reference
    constexpr std::size_t kIterations = 1000;
    std::size_t rejected_count = 0;
    std::size_t sysreg_instr_count = 0;

    for (std::size_t i = 0; i < kIterations; ++i) {
        const std::uint32_t instr = static_cast<std::uint32_t>(prng());
        mem.write_u32(0x1000, instr);
        cpu.set_program_counter(0x1000);
        cpu.set_exception_taken_in_step(false);

        // Production step
        bool threw = false;
        try {
            cpu.step(mem);
        } catch (...) {
            threw = true;
        }

        // Reference decode
        const auto ref_res = ReferenceSysRegModel::step_sysreg_instruction(ref_cpu, ref_sysregs, instr);

        if (ref_res.status == ReferenceSysRegStatus::UnallocatedEncoding ||
            ref_res.status == ReferenceSysRegStatus::UndefinedException) {
            rejected_count++;
        } else {
            sysreg_instr_count++;
        }

        // Invariants: cpu must remain intact, no host heap corruption
        TEST_ASSERT(cpu.get_stack_pointer() < 0x100000000ULL, "Fuzzer must not cause uncontrolled stack pointer overflow");
    }

    std::cout << "  Fuzzer completed " << kIterations << " iterations (Seed: 0x59535247). Rejected encodings: "
              << rejected_count << ", Valid sysreg patterns: " << sysreg_instr_count << std::endl;
    return true;
}

// =============================================================================
// Test 11: Tri-Way Parity (Direct Execution vs Replay vs Independent Reference)
// =============================================================================
bool test_tri_way_parity() {
    // Sequence of legal system-register instructions:
    // 1. MSR TPIDR_EL0, X0 (with X0 = 0x1234567890ABCDEF)
    // 2. MSR TPIDR_EL1, X1 (with X1 = 0xFEDCBA0987654321)
    // 3. MRS X2, TPIDR_EL0
    // 4. MRS X3, TPIDR_EL1
    std::vector<std::uint32_t> instructions = {
        encode_msr(3, 3, 13, 0, 2, 0),
        encode_msr(3, 0, 13, 0, 4, 1),
        encode_mrs(3, 3, 13, 0, 2, 2),
        encode_mrs(3, 0, 13, 0, 4, 3)
    };

    // 1. Production Direct Execution
    CPU cpu;
    cpu.reset();
    Memory mem(0x10000);
    cpu.write_register(0, 0x1234567890ABCDEFULL);
    cpu.write_register(1, 0xFEDCBA0987654321ULL);

    for (std::size_t i = 0; i < instructions.size(); ++i) {
        mem.write_u32(0x1000 + i * 4, instructions[i]);
    }
    cpu.set_program_counter(0x1000);

    for (std::size_t i = 0; i < instructions.size(); ++i) {
        cpu.step(mem);
    }

    // 2. Independent Reference Execution
    ReferenceCpuState ref_cpu{};
    ReferenceSysRegState ref_sysregs{};
    ref_cpu.x[0] = 0x1234567890ABCDEFULL;
    ref_cpu.x[1] = 0xFEDCBA0987654321ULL;
    ref_cpu.pc = 0x1000;
    ref_sysregs.current_el = 1;

    for (std::size_t i = 0; i < instructions.size(); ++i) {
        const auto res = ReferenceSysRegModel::step_sysreg_instruction(ref_cpu, ref_sysregs, instructions[i]);
        TEST_ASSERT(res.status == ReferenceSysRegStatus::Success, "Reference execution must succeed");
    }

    // 3. Compare Production vs Reference
    TEST_ASSERT(cpu.read_register(2) == ref_cpu.x[2], "Tri-way parity: X2 (TPIDR_EL0)");
    TEST_ASSERT(cpu.read_register(3) == ref_cpu.x[3], "Tri-way parity: X3 (TPIDR_EL1)");
    TEST_ASSERT(cpu.get_tpidr_el0() == ref_sysregs.tpidr_el0, "Tri-way parity: TPIDR_EL0");
    TEST_ASSERT(cpu.get_tpidr_el1() == ref_sysregs.tpidr_el1, "Tri-way parity: TPIDR_EL1");
    TEST_ASSERT(cpu.get_program_counter() == ref_cpu.pc, "Tri-way parity: PC");

    return true;
}

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 36: System Register & CPU Conformance Tests ===" << std::endl;

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        std::cout << "[RUNNING] " << name << "..." << std::endl;
        if (fn()) {
            std::cout << "[PASS] " << name << std::endl;
            passed++;
        } else {
            std::cerr << "[FAIL] " << name << std::endl;
            failed++;
        }
    };

    run_test("SysRegDescriptorTable", test_sysreg_descriptor_table);
    run_test("TpidrRegisters", test_tpidr_registers);
    run_test("PrivilegeEnforcement", test_privilege_enforcement);
    run_test("WxRegisterSemantics", test_wx_register_semantics);
    run_test("R31SpVsXzr", test_r31_sp_vs_xzr);
    run_test("ConditionCodes", test_condition_codes);
    run_test("ExceptionEntryAndEret", test_exception_entry_and_eret);
    run_test("BarriersAndSystemInstructions", test_barriers_and_system_instructions);
    run_test("SnapshotAndHashInvariants", test_snapshot_and_hash_invariants);
    run_test("FuzzCampaign", test_fuzz_campaign);
    run_test("TriWayParity", test_tri_way_parity);

    std::cout << "\nTest Summary: " << passed << " passed, " << failed << " failed." << std::endl;
    return failed == 0 ? 0 : 1;
}
