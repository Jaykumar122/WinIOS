// ==============================================================================
// WinIOS Campaign 202 Stage 19: EL2 Stage-2 Fault Architecture & Recovery Tests
// ==============================================================================
// Verifies:
// 1. Stage2FaultInjector translation, permission, and access-flag fault injection
// 2. Syndrome generation: ESR_EL2 (EC 0x24 Data Abort / 0x20 Instruction Abort),
//    FAR_EL2 (faulting VA), and HPFAR_EL2 (ipa >> 8)
// 3. Multi-level walk fault codes (levels 0..3) and address size faults
// 4. End-to-end guest EL1 fault -> EL2 trap -> hypervisor mapping fix -> ERET -> replay
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace winios;
using namespace winios::mmu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Stage-2 Fault Injection for Translation Fault (Levels 0..3)
// -----------------------------------------------------------------------------
void test_translation_fault_injection_and_syndromes() {
    std::cout << "[Test 1] Stage-2 Translation Fault Injection & Syndrome Generation...\n";

    Stage2TranslationEngine s2;
    s2.set_hcr(0x1ULL); // Enable Stage-2 (HCR_EL2.VM = 1)
    s2.set_vtcr(0x80003550ULL); // 48-bit IPA, 4KB default
    s2.set_vttbr(0x0001000000010000ULL); // VMID 1, table base 0x10000

    Stage2FaultInjector injector;
    s2.set_fault_injector(&injector);

    Memory mem(0x100000ULL);

    // Rule 1: Translation fault at Level 3 for IPA 0x40001000
    Stage2InjectedFault rule1{};
    rule1.ipa = 0x40001000ULL;
    rule1.ipa_mask = 0xFFFFFFFFFFFFF000ULL;
    rule1.fault_type = ::mmu::MmuFaultType::Translation;
    rule1.level = 3;
    rule1.active = true;
    rule1.max_triggers = 2;
    injector.add_rule(rule1);

    // 1. Data Read Fault
    Stage2Fault fault_read{};
    auto res_read = s2.translate(mem, 0x40001000ULL, Stage2AccessType::Read, &fault_read);
    TEST_CHECK(!res_read.has_value(), "Translation must fail on injected fault");
    TEST_CHECK(fault_read.type == ::mmu::MmuFaultType::Translation, "Fault type must be Translation");
    TEST_CHECK(fault_read.level == 3, "Fault level must be 3");
    TEST_CHECK(fault_read.fsc() == 0x07, "FSC for Translation Level 3 must be 0x07 (0x04 + 3)");

    const std::uint64_t faulting_va = 0x00007FFF40001000ULL;
    auto syn_read = make_stage2_syndrome_report(fault_read, faulting_va);

    // ESR_EL2 check:
    // EC = 0x24 (bits [31:26])
    // IL = 1 (bit 25)
    // WnR = 0 (bit 6) for read
    // DFSC = 0x07 (bits [5:0])
    const std::uint32_t expected_esr_read = (0x24u << 26u) | (1u << 25u) | 0x07u;
    TEST_CHECK(syn_read.esr_el2 == expected_esr_read,
               "ESR_EL2 for Data Read Abort must match expected: 0x" + std::to_string(syn_read.esr_el2));
    TEST_CHECK(syn_read.far_el2 == faulting_va, "FAR_EL2 must match faulting VA");
    const std::uint64_t expected_hpfar = (0x40001000ULL >> 8u) & 0x000000FFFFFFFFF0ULL;
    TEST_CHECK(syn_read.hpfar_el2 == expected_hpfar, "HPFAR_EL2 must match (ipa >> 8)");

    // 2. Data Write Fault
    Stage2Fault fault_write{};
    auto res_write = s2.translate(mem, 0x40001000ULL, Stage2AccessType::Write, &fault_write);
    TEST_CHECK(!res_write.has_value(), "Translation must fail on write");
    TEST_CHECK(fault_write.is_write, "is_write must be true");

    auto syn_write = make_stage2_syndrome_report(fault_write, faulting_va);
    // WnR = 1 (bit 6 set)
    const std::uint32_t expected_esr_write = (0x24u << 26u) | (1u << 25u) | (1u << 6u) | 0x07u;
    TEST_CHECK(syn_write.esr_el2 == expected_esr_write, "ESR_EL2 for Data Write Abort must have WnR=1");

    // 3. Instruction Fetch Fault
    injector.clear_rules();
    Stage2InjectedFault rule_exec{};
    rule_exec.ipa = 0x50000000ULL;
    rule_exec.ipa_mask = 0xFFFFFFFFFFFFF000ULL;
    rule_exec.fault_type = ::mmu::MmuFaultType::Translation;
    rule_exec.level = 2;
    rule_exec.active = true;
    rule_exec.max_triggers = 1;
    injector.add_rule(rule_exec);

    Stage2Fault fault_exec{};
    auto res_exec = s2.translate(mem, 0x50000000ULL, Stage2AccessType::Execute, &fault_exec);
    TEST_CHECK(!res_exec.has_value(), "Translation must fail on instruction fetch");
    TEST_CHECK(fault_exec.is_execute, "is_execute must be true");
    TEST_CHECK(fault_exec.fsc() == 0x06, "FSC for Translation Level 2 must be 0x06 (0x04 + 2)");

    auto syn_exec = make_stage2_syndrome_report(fault_exec, 0x00007FFF50000000ULL);
    // EC = 0x20 for Instruction Abort from lower EL
    const std::uint32_t expected_esr_exec = (0x20u << 26u) | (1u << 25u) | 0x06u;
    TEST_CHECK(syn_exec.esr_el2 == expected_esr_exec, "ESR_EL2 for Instruction Abort must have EC=0x20");

    std::cout << "  -> Translation fault injection & syndromes passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Stage-2 Permission Fault Injection (Levels 1..3)
// -----------------------------------------------------------------------------
void test_permission_fault_injection() {
    std::cout << "[Test 2] Stage-2 Permission Fault Injection & FSC Codes...\n";

    Stage2TranslationEngine s2;
    s2.set_hcr(0x1ULL);
    Stage2FaultInjector injector;
    s2.set_fault_injector(&injector);

    Memory mem(0x100000ULL);

    // Permission fault at Level 1, 2, 3: FSC is 0x0C + level
    for (unsigned lvl = 1; lvl <= 3; ++lvl) {
        injector.clear_rules();
        Stage2InjectedFault rule{};
        rule.ipa = 0x60000000ULL + (static_cast<std::uint64_t>(lvl) << 12u);
        rule.ipa_mask = 0xFFFFFFFFFFFFF000ULL;
        rule.fault_type = ::mmu::MmuFaultType::Permission;
        rule.level = lvl;
        rule.active = true;
        rule.max_triggers = 1;
        injector.add_rule(rule);

        Stage2Fault fault{};
        auto res = s2.translate(mem, rule.ipa, Stage2AccessType::Write, &fault);
        TEST_CHECK(!res.has_value(), "Must fail with permission fault");
        TEST_CHECK(fault.type == ::mmu::MmuFaultType::Permission, "Fault must be Permission");
        TEST_CHECK(fault.level == lvl, "Level must match");
        TEST_CHECK(fault.fsc() == (0x0Cu + lvl), "FSC must be 0x0C + level");

        auto syn = make_stage2_syndrome_report(fault, 0x70000000ULL + rule.ipa);
        const std::uint32_t expected_esr = (0x24u << 26u) | (1u << 25u) | (1u << 6u) | (0x0Cu + lvl);
        TEST_CHECK(syn.esr_el2 == expected_esr, "ESR_EL2 must encode Permission DFSC");
    }

    std::cout << "  -> Permission fault injection passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Stage-2 Access Flag Fault Injection (Levels 1..3)
// -----------------------------------------------------------------------------
void test_access_flag_fault_injection() {
    std::cout << "[Test 3] Stage-2 Access Flag Fault Injection & FSC Codes...\n";

    Stage2TranslationEngine s2;
    s2.set_hcr(0x1ULL);
    Stage2FaultInjector injector;
    s2.set_fault_injector(&injector);

    Memory mem(0x100000ULL);

    // Access Flag fault: FSC is 0x08 + level
    for (unsigned lvl = 1; lvl <= 3; ++lvl) {
        injector.clear_rules();
        Stage2InjectedFault rule{};
        rule.ipa = 0x70000000ULL + (static_cast<std::uint64_t>(lvl) << 12u);
        rule.ipa_mask = 0xFFFFFFFFFFFFF000ULL;
        rule.fault_type = ::mmu::MmuFaultType::AccessFlag;
        rule.level = lvl;
        rule.active = true;
        rule.max_triggers = 1;
        injector.add_rule(rule);

        Stage2Fault fault{};
        auto res = s2.translate(mem, rule.ipa, Stage2AccessType::Read, &fault);
        TEST_CHECK(!res.has_value(), "Must fail with access flag fault");
        TEST_CHECK(fault.type == ::mmu::MmuFaultType::AccessFlag, "Fault must be AccessFlag");
        TEST_CHECK(fault.level == lvl, "Level must match");
        TEST_CHECK(fault.fsc() == (0x08u + lvl), "FSC must be 0x08 + level");

        auto syn = make_stage2_syndrome_report(fault, 0x80000000ULL + rule.ipa);
        const std::uint32_t expected_esr = (0x24u << 26u) | (1u << 25u) | (0x08u + lvl);
        TEST_CHECK(syn.esr_el2 == expected_esr, "ESR_EL2 must encode AccessFlag DFSC");
    }

    std::cout << "  -> Access flag fault injection passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: End-to-End Guest EL1 Fault -> EL2 Trap -> Mapping Fix -> ERET -> Replay
// -----------------------------------------------------------------------------
void test_end_to_end_el2_fault_and_guest_recovery() {
    std::cout << "[Test 4] End-to-End EL1 Fault -> EL2 Trap -> Fix -> ERET -> Replay...\n";

    Memory mem(0x200000ULL); // 2 MB guest RAM
    Stage2TranslationEngine s2;

    // Configure 16KB granule Stage-2 translation
    // VTCR_EL2: TG0 = 0b10 (16KB), SL0 = 0b01 (Level 2 start), T0SZ = 16 (48-bit IPA)
    s2.set_vtcr((0b10ULL << 14u) | (0b01ULL << 6u) | 16ULL);
    const std::uint64_t vttbr_base = 0x10000ULL;
    s2.set_vttbr((1ULL << 48u) | vttbr_base); // VMID 1
    s2.set_hcr(0x1ULL); // Enable Stage 2

    // Set up Fault Injector with a single-shot translation fault on IPA 0x80000
    Stage2FaultInjector injector;
    s2.set_fault_injector(&injector);

    const std::uint64_t target_ipa = 0x80000ULL;
    const std::uint64_t target_va  = 0x0000700000080000ULL;
    const std::uint64_t backing_pa = 0x140000ULL;

    Stage2InjectedFault rule{};
    rule.ipa = target_ipa;
    rule.ipa_mask = 0xFFFFFFFFFFFFF000ULL;
    rule.fault_type = ::mmu::MmuFaultType::Translation;
    rule.level = 3;
    rule.active = true;
    rule.max_triggers = 1; // Single-shot trigger
    injector.add_rule(rule);

    // 1. Initial attempt: Guest EL1 issues memory read at target_va (mapped to target_ipa)
    Stage2Fault trapped_fault{};
    auto trans_res1 = s2.translate(mem, target_ipa, Stage2AccessType::Read, &trapped_fault);
    TEST_CHECK(!trans_res1.has_value(), "Step 1: First access must trap to EL2");

    // 2. EL2 Hypervisor catches the fault and processes syndrome registers
    auto syn_report = make_stage2_syndrome_report(trapped_fault, target_va);
    TEST_CHECK(syn_report.far_el2 == target_va, "Step 2: FAR_EL2 must match guest VA");
    TEST_CHECK(syn_report.hpfar_el2 == ((target_ipa >> 8u) & 0x000000FFFFFFFFF0ULL), "Step 2: HPFAR_EL2 must match ipa >> 8");
    TEST_CHECK((syn_report.esr_el2 >> 26u) == 0x24u, "Step 2: ESR_EL2 EC must be Data Abort");

    // 3. Hypervisor fixes mapping: Allocates page and constructs Stage-2 page table entry
    // Write test signature into physical RAM at backing_pa
    mem.write_u64(backing_pa, 0x123456789ABCDEF0ULL);

    bool map_ok = Stage2TranslationEngine::map_page_16k(
        mem, vttbr_base, target_ipa, backing_pa, true /* read */, true /* write */, true /* exec */);
    TEST_CHECK(map_ok, "Step 3: Hypervisor must successfully map Stage-2 page");

    // 4. Hypervisor issues ERET back to guest EL1:
    // Notice rule.max_triggers = 1 has expired, rule is inactive.
    TEST_CHECK(!injector.rules()[0].active, "Step 4: Injected rule must be auto-deactivated");

    // 5. Guest EL1 re-executes the faulted instruction:
    Stage2Fault replay_fault{};
    auto trans_res2 = s2.translate(mem, target_ipa, Stage2AccessType::Read, &replay_fault);
    TEST_CHECK(trans_res2.has_value(), "Step 5: Replay translation must succeed");
    TEST_CHECK(trans_res2->physical_address == backing_pa, "Step 5: Translated PA must match backing PA");
    TEST_CHECK(trans_res2->readable && trans_res2->writable, "Step 5: Permissions must be RW");

    // Read guest memory at physical address: signature verified
    const std::uint64_t read_data = mem.read_u64(trans_res2->physical_address);
    TEST_CHECK(read_data == 0x123456789ABCDEF0ULL, "Step 5: Memory content must match test signature");

    std::cout << "  -> End-to-end EL1 fault -> EL2 trap -> fix -> ERET -> replay passed.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Address Size Faults & Boundary Enforcement
// -----------------------------------------------------------------------------
void test_address_size_faults() {
    std::cout << "[Test 5] Stage-2 Address Size Faults & Boundary Enforcement...\n";

    Stage2TranslationEngine s2;
    s2.set_hcr(0x1ULL); // Enable Stage-2
    // VTCR_EL2 with T0SZ = 24 -> 40-bit IPA space (bits [63:40] must be zero)
    s2.set_vtcr(24ULL);
    s2.set_vttbr(0x20000ULL);

    Memory mem(0x100000ULL);

    // IPA with bit 40 set (exceeds 40-bit IPA space)
    const std::uint64_t oob_ipa = 1ULL << 40u;
    Stage2Fault fault{};
    auto res = s2.translate(mem, oob_ipa, Stage2AccessType::Read, &fault);
    TEST_CHECK(!res.has_value(), "Out-of-bounds IPA must fail");
    TEST_CHECK(fault.type == ::mmu::MmuFaultType::AddressSize, "Fault must be AddressSize");
    TEST_CHECK(fault.level == 0, "Address size fault level must be 0");
    TEST_CHECK(fault.fsc() == 0x00, "FSC for Address Size Fault Level 0 must be 0x00");

    auto syn = make_stage2_syndrome_report(fault, oob_ipa);
    const std::uint32_t expected_esr = (0x24u << 26u) | (1u << 25u) | 0x00u;
    TEST_CHECK(syn.esr_el2 == expected_esr, "ESR_EL2 must encode AddressSize DFSC");

    std::cout << "  -> Address size faults passed.\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Running Stage 202.19 EL2 Stage-2 Fault Architecture Tests ===\n";
    test_translation_fault_injection_and_syndromes();
    test_permission_fault_injection();
    test_access_flag_fault_injection();
    test_end_to_end_el2_fault_and_guest_recovery();
    test_address_size_faults();

    if (g_failures != 0) {
        std::cerr << "Stage 202.19 tests FAILED with " << g_failures << " error(s).\n";
        return 1;
    }
    std::cout << "All Stage 202.19 tests passed successfully!\n";
    return 0;
}
